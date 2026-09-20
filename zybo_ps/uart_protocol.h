#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

// Stage 6: the wire protocol between the ESP32 (esp32_mpu6050_dashboard/)
// and this Zybo PS bare-metal app. If you change anything here, mirror the
// change in esp32_mpu6050_dashboard.ino's matching #defines — there's no
// shared build system between the Arduino and Vitis toolchains, so these
// have to be kept in sync by hand.
//
// Physical link: ESP32 Serial2 (GPIO17 TX2 / GPIO16 RX2) <-> Zybo PS UART1
// (EMIO-routed to a Pmod, see README's Stage 6 section for pin assignment).
// Both sides are 3.3V logic — no level shifting needed, unlike the HC-SR04
// experiment earlier in this project's history.

#define UART_BAUD_RATE 115200

// Must match train_classifier.py's CLASSES list order exactly — the
// class_idx byte in RESULT_PACKET indexes into this.
//   0 = stationary, 1 = tilt, 2 = freefall, 3 = impact, 4 = shake
#define NUM_CLASSES 5

// Must match the feature column order collect_training_data.py /
// dashboard_html.h's logger produce: for each axis in
// [ax, ay, az, gx, gy, gz], five stats in order
// [mean, std, min, max, fft_peak_hz] — 6 axes * 5 stats = 30.
#define NUM_FEATURES 30

// ESP32 -> Zybo: a completed feature window, sent as raw float32 in
// natural units (g, deg/s, Hz) — NOT yet scaler-normalized. The Zybo side
// applies the training StandardScaler (see scaler_params.h) and converts
// to the IP's ap_fixed<16,6> format itself; keeps the ESP32 side simple
// and means only the Zybo firmware needs updating if the model is
// ever retrained (new scaler parameters), not both sides.
#define FEATURE_PACKET_START 0xA5
// start byte + 30 * 4-byte floats + 1 XOR-checksum byte
#define FEATURE_PACKET_SIZE (1 + NUM_FEATURES * 4 + 1)

// Zybo -> ESP32: the classification result.
#define RESULT_PACKET_START 0x5A
// start byte + class_idx + confidence_pct (0-100) + 1 XOR-checksum byte
#define RESULT_PACKET_SIZE 4

#endif
