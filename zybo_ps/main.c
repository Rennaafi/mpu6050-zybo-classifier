// Stage 6 bare-metal app for the Zybo Z7-10's Zynq-7010 PS: receives a
// feature-window packet from the ESP32 over UART, runs it through the
// Stage 5 HLS motion classifier IP, sends the result back.
//
// Build as a standalone (no-OS) Vitis application project against a
// platform generated from your block design (see README's Stage 6
// section). Requires the standalone BSP's xuartps driver — pulled in
// automatically if your block design has a PS UART instance in it.
//
// Confirmed against this project's actual generated xparameters.h — if you
// re-do the block design with different instance names, re-check these.
#include "xparameters.h"
#define MOTION_CLASSIFIER_BASEADDR XPAR_MYPROJECT_0_BASEADDR
// UART0 was left disabled in this design (MIO Configuration), so UART1
// (EMIO) is the *only* enabled UART instance — Xilinx numbers enabled
// driver instances sequentially from 0, so it became "_0" here despite
// being physically UART1. Not a typo.
// This BSP is built with -DSDT (Xilinx's System Device Tree flow), under
// which XUartPs_LookupConfig() takes a base address, not a device ID
// (see xuartps.h's "#ifndef SDT ... #else" around its prototype) — the
// classic numeric DEVICE_ID macros aren't even generated in this mode.
#define ESP32_UART_BASEADDR XPAR_XUARTPS_0_BASEADDR

#include <string.h>
#include "xuartps.h"
#include "motion_classifier.h"
#include "uart_protocol.h"

static XUartPs uart;

static int uart_init(void) {
    XUartPs_Config *cfg = XUartPs_LookupConfig(ESP32_UART_BASEADDR);
    if (!cfg) {
        return -1;
    }
    if (XUartPs_CfgInitialize(&uart, cfg, cfg->BaseAddress) != XST_SUCCESS) {
        return -1;
    }
    XUartPs_SetBaudRate(&uart, UART_BAUD_RATE);
    return 0;
}

static void uart_read_blocking(uint8_t *buf, int len) {
    int got = 0;
    while (got < len) {
        got += XUartPs_Recv(&uart, buf + got, len - got);
    }
}

static void uart_write_blocking(const uint8_t *buf, int len) {
    int sent = 0;
    while (sent < len) {
        sent += XUartPs_Send(&uart, (u8 *)(buf + sent), len - sent);
    }
}

static uint8_t xor_checksum(const uint8_t *buf, int len) {
    uint8_t c = 0;
    for (int i = 0; i < len; i++) {
        c ^= buf[i];
    }
    return c;
}

int main(void) {
    if (uart_init() != 0) {
        return -1; // xparameters.h ID likely wrong — see the macros above
    }
    motion_classifier_init(MOTION_CLASSIFIER_BASEADDR);

    uint8_t rx[FEATURE_PACKET_SIZE];
    for (;;) {
        // Byte-at-a-time resync on the start marker so one dropped/garbled
        // byte can't desync the framing permanently.
        uint8_t b;
        do {
            uart_read_blocking(&b, 1);
        } while (b != FEATURE_PACKET_START);
        rx[0] = b;
        uart_read_blocking(rx + 1, FEATURE_PACKET_SIZE - 1);

        if (xor_checksum(rx + 1, NUM_FEATURES * 4) != rx[FEATURE_PACKET_SIZE - 1]) {
            continue; // bad packet — drop it and resync on the next start byte
        }

        float features[NUM_FEATURES];
        memcpy(features, rx + 1, sizeof(features));

        int confidence;
        int class_idx = motion_classifier_infer(features, &confidence);

        uint8_t tx[RESULT_PACKET_SIZE];
        tx[0] = RESULT_PACKET_START;
        tx[1] = (uint8_t)class_idx;
        tx[2] = (uint8_t)confidence;
        tx[3] = xor_checksum(tx + 1, 2);
        uart_write_blocking(tx, RESULT_PACKET_SIZE);
    }
}
