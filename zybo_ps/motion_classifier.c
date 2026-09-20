#include "motion_classifier.h"
#include "xmyproject_hw.h"
#include "scaler_params.h"

// ap_fixed<16,6>: 16 total bits, 6 integer bits (including the sign bit)
// per Xilinx's convention, so 10 fractional bits. Scale factor 2^10.
#define FIXED_FRAC_BITS 10
#define FIXED_SCALE (1 << FIXED_FRAC_BITS)
#define FIXED_MAX 32767
#define FIXED_MIN (-32768)

static uint32_t base;

// Round-half-away-from-zero without linking libm — this bare-metal BSP
// doesn't have libm wired into its link step by default, and lroundf()
// pulled in an undefined reference at link time. Plain float arithmetic
// needs no library, and round-half-up precision is plenty for quantizing
// into a fixed-point register.
static int round_to_int(float x) {
    return (x >= 0.0f) ? (int)(x + 0.5f) : (int)(x - 0.5f);
}

static inline void reg_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}

static inline uint32_t reg_read(uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static int16_t float_to_fixed(float v) {
    float scaled = v * (float)FIXED_SCALE;
    if (scaled > FIXED_MAX) scaled = FIXED_MAX;
    if (scaled < FIXED_MIN) scaled = FIXED_MIN;
    // Saturate here in software: the IP's ap_fixed ports default to
    // AP_WRAP on overflow, which silently corrupts the value instead of
    // clamping — an out-of-range feature (e.g. an extreme shake) would
    // otherwise wrap to a wildly wrong number instead of just saturating.
    return (int16_t)round_to_int(scaled);
}

static float fixed_to_float(int16_t v) {
    return (float)v / (float)FIXED_SCALE;
}

void motion_classifier_init(uint32_t base_addr) {
    base = base_addr;
}

int motion_classifier_infer(const float features[NUM_FEATURES], int *confidence_pct) {
    // input_layer is packed 2 values per 32-bit register (low half =
    // features[2n], high half = features[2n+1]) across 15 registers
    // starting at XMYPROJECT_CTRL_ADDR_INPUT_LAYER_DATA — see
    // xmyproject_hw.h's generated comment block for the exact layout.
    for (int i = 0; i < NUM_FEATURES; i += 2) {
        float norm_lo = (features[i] - SCALER_MEAN[i]) / SCALER_SCALE[i];
        float norm_hi = (features[i + 1] - SCALER_MEAN[i + 1]) / SCALER_SCALE[i + 1];
        uint16_t lo = (uint16_t)float_to_fixed(norm_lo);
        uint16_t hi = (uint16_t)float_to_fixed(norm_hi);
        uint32_t packed = ((uint32_t)hi << 16) | lo;
        reg_write(XMYPROJECT_CTRL_ADDR_INPUT_LAYER_DATA + (i / 2) * 4, packed);
    }

    // ap_start (bit 0), then poll ap_done (bit 1). At ~29-57 HLS-estimated
    // cycles this returns almost immediately — a busy poll is fine here,
    // no need for the interrupt path this register also exposes.
    reg_write(XMYPROJECT_CTRL_ADDR_AP_CTRL, 0x1);
    uint32_t ctrl;
    do {
        ctrl = reg_read(XMYPROJECT_CTRL_ADDR_AP_CTRL);
    } while (!(ctrl & 0x2));

    static const uint32_t out_offsets[NUM_CLASSES] = {
        XMYPROJECT_CTRL_ADDR_LAYER5_OUT_0_DATA,
        XMYPROJECT_CTRL_ADDR_LAYER5_OUT_1_DATA,
        XMYPROJECT_CTRL_ADDR_LAYER5_OUT_2_DATA,
        XMYPROJECT_CTRL_ADDR_LAYER5_OUT_3_DATA,
        XMYPROJECT_CTRL_ADDR_LAYER5_OUT_4_DATA,
    };

    int best = 0;
    float best_prob = -1.0f;
    for (int c = 0; c < NUM_CLASSES; c++) {
        int16_t raw = (int16_t)(reg_read(out_offsets[c]) & 0xFFFF);
        float prob = fixed_to_float(raw);
        if (prob > best_prob) {
            best_prob = prob;
            best = c;
        }
    }

    if (confidence_pct) {
        int pct = round_to_int(best_prob * 100.0f);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        *confidence_pct = pct;
    }
    return best;
}
