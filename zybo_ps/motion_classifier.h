#ifndef MOTION_CLASSIFIER_H
#define MOTION_CLASSIFIER_H

#include <stdint.h>
#include "uart_protocol.h"

// Thin bare-metal driver for the myproject_0 HLS IP (Stage 5's motion
// classifier), talking to it directly over its AXI-Lite s_axi_CTRL
// interface. See zybo_ps/xmyproject_hw.h for the register map this is
// built from, and the README's Stage 6 section for where base_addr comes
// from (XPAR_MYPROJECT_0_S_AXI_CTRL_BASEADDR in your generated
// xparameters.h, once the block design + BSP exist).

// base_addr: the IP's AXI-Lite base address from xparameters.h.
void motion_classifier_init(uint32_t base_addr);

// Runs one inference. features[30] are RAW values in natural units (this
// function applies the training StandardScaler itself — see
// scaler_params.h), in the same column order train_classifier.py's CSV
// uses (see uart_protocol.h). Returns the predicted class index (0-4) and
// writes a 0-100 confidence (the winning class's softmax output) to
// *confidence_pct.
int motion_classifier_infer(const float features[NUM_FEATURES], int *confidence_pct);

#endif
