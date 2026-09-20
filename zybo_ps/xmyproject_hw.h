// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.1 (64-bit)
// Tool Version Limit: 2025.05
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
// CTRL
// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read/COR)
//        bit 7  - auto_restart (Read/Write)
//        bit 9  - interrupt (Read)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0 - enable ap_done interrupt (Read/Write)
//        bit 1 - enable ap_ready interrupt (Read/Write)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0 - ap_done (Read/TOW)
//        bit 1 - ap_ready (Read/TOW)
//        others - reserved
// 0x10 : Data signal of input_layer
//        bit 31~0 - input_layer[31:0] (Read/Write)
// 0x14 : Data signal of input_layer
//        bit 31~0 - input_layer[63:32] (Read/Write)
// 0x18 : Data signal of input_layer
//        bit 31~0 - input_layer[95:64] (Read/Write)
// 0x1c : Data signal of input_layer
//        bit 31~0 - input_layer[127:96] (Read/Write)
// 0x20 : Data signal of input_layer
//        bit 31~0 - input_layer[159:128] (Read/Write)
// 0x24 : Data signal of input_layer
//        bit 31~0 - input_layer[191:160] (Read/Write)
// 0x28 : Data signal of input_layer
//        bit 31~0 - input_layer[223:192] (Read/Write)
// 0x2c : Data signal of input_layer
//        bit 31~0 - input_layer[255:224] (Read/Write)
// 0x30 : Data signal of input_layer
//        bit 31~0 - input_layer[287:256] (Read/Write)
// 0x34 : Data signal of input_layer
//        bit 31~0 - input_layer[319:288] (Read/Write)
// 0x38 : Data signal of input_layer
//        bit 31~0 - input_layer[351:320] (Read/Write)
// 0x3c : Data signal of input_layer
//        bit 31~0 - input_layer[383:352] (Read/Write)
// 0x40 : Data signal of input_layer
//        bit 31~0 - input_layer[415:384] (Read/Write)
// 0x44 : Data signal of input_layer
//        bit 31~0 - input_layer[447:416] (Read/Write)
// 0x48 : Data signal of input_layer
//        bit 31~0 - input_layer[479:448] (Read/Write)
// 0x4c : reserved
// 0x50 : Data signal of layer5_out_0
//        bit 15~0 - layer5_out_0[15:0] (Read)
//        others   - reserved
// 0x54 : Control signal of layer5_out_0
//        bit 0  - layer5_out_0_ap_vld (Read/COR)
//        others - reserved
// 0x60 : Data signal of layer5_out_1
//        bit 15~0 - layer5_out_1[15:0] (Read)
//        others   - reserved
// 0x64 : Control signal of layer5_out_1
//        bit 0  - layer5_out_1_ap_vld (Read/COR)
//        others - reserved
// 0x70 : Data signal of layer5_out_2
//        bit 15~0 - layer5_out_2[15:0] (Read)
//        others   - reserved
// 0x74 : Control signal of layer5_out_2
//        bit 0  - layer5_out_2_ap_vld (Read/COR)
//        others - reserved
// 0x80 : Data signal of layer5_out_3
//        bit 15~0 - layer5_out_3[15:0] (Read)
//        others   - reserved
// 0x84 : Control signal of layer5_out_3
//        bit 0  - layer5_out_3_ap_vld (Read/COR)
//        others - reserved
// 0x90 : Data signal of layer5_out_4
//        bit 15~0 - layer5_out_4[15:0] (Read)
//        others   - reserved
// 0x94 : Control signal of layer5_out_4
//        bit 0  - layer5_out_4_ap_vld (Read/COR)
//        others - reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XMYPROJECT_CTRL_ADDR_AP_CTRL           0x00
#define XMYPROJECT_CTRL_ADDR_GIE               0x04
#define XMYPROJECT_CTRL_ADDR_IER               0x08
#define XMYPROJECT_CTRL_ADDR_ISR               0x0c
#define XMYPROJECT_CTRL_ADDR_INPUT_LAYER_DATA  0x10
#define XMYPROJECT_CTRL_BITS_INPUT_LAYER_DATA  480
#define XMYPROJECT_CTRL_ADDR_INPUT_LAYER_DATA_ 0x38
#define XMYPROJECT_CTRL_BITS_INPUT_LAYER_DATA  480
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_0_DATA 0x50
#define XMYPROJECT_CTRL_BITS_LAYER5_OUT_0_DATA 16
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_0_CTRL 0x54
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_1_DATA 0x60
#define XMYPROJECT_CTRL_BITS_LAYER5_OUT_1_DATA 16
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_1_CTRL 0x64
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_2_DATA 0x70
#define XMYPROJECT_CTRL_BITS_LAYER5_OUT_2_DATA 16
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_2_CTRL 0x74
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_3_DATA 0x80
#define XMYPROJECT_CTRL_BITS_LAYER5_OUT_3_DATA 16
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_3_CTRL 0x84
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_4_DATA 0x90
#define XMYPROJECT_CTRL_BITS_LAYER5_OUT_4_DATA 16
#define XMYPROJECT_CTRL_ADDR_LAYER5_OUT_4_CTRL 0x94

