/*
  ESP32 + GY-521 (MPU6050) Live Web Dashboard
  ---------------------------------------------
  Reads accelerometer + gyroscope data from an MPU6050 over I2C,
  fuses it into pitch/roll angles with a complementary filter,
  and serves a live-updating web dashboard over WiFi.

  WIRING (GY-521 -> ESP32):
    VCC -> 3.3V
    GND -> GND
    SCL -> GPIO22
    SDA -> GPIO21
    AD0 -> GND (or leave floating) -> I2C address 0x68

  LIBRARIES REQUIRED (install via Arduino Library Manager):
    - "Adafruit MPU6050"
    - "Adafruit Unified Sensor"
    - "Adafruit BusIO"
  (ESP32 board support must also be installed via Boards Manager)

  WIRING (Zybo PS UART1 -> ESP32, Stage 6 motion classifier link):
    Zybo PS UART1 TX (Pmod, EMIO) -> ESP32 GPIO16 (RX2)
    Zybo PS UART1 RX (Pmod, EMIO) -> ESP32 GPIO17 (TX2)
    GND -> GND
    Both sides are 3.3V logic — no level shifting needed. See
    zybo_ps/uart_protocol.h for the packet format both ends must agree on.

  HOW TO USE:
    1. Copy secrets.h.example to secrets.h and set the WiFi network name/
       password the ESP32 will broadcast (password must be 8+ characters).
    2. Flash to your ESP32.
    3. On your phone/laptop, join the WiFi network the ESP32 just created.
    4. Open http://192.168.4.1 in a browser. No router or internet needed.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <string.h>

#include "secrets.h"
#include "dashboard_html.h"

// ---------- CONFIG ----------
// AP_SSID/AP_PASSWORD are the network *this device creates* — not a
// network it joins. No router or internet access required.
const char* AP_SSID       = SECRET_WIFI_SSID;
const char* AP_PASSWORD   = SECRET_WIFI_PASSWORD;
const IPAddress AP_IP(192, 168, 4, 1);

const int SDA_PIN = 21;
const int SCL_PIN = 22;

// Complementary filter weight (how much to trust the gyro vs accel)
const float ALPHA = 0.96;

// A handful of one-off I2C read failures is normal on a noisy bus and gets
// skipped harmlessly. This many consecutive failures (~0.5s at the current
// loop delay) means the bus is likely wedged and needs active recovery.
const int MAX_CONSECUTIVE_FAILURES = 20;

// ---------- MOTION CLASSIFIER CONFIG (Stage 6) ----------
// Must match zybo_ps/uart_protocol.h exactly — see that file's comments.
#define FEATURE_PACKET_START 0xA5
#define NUM_FEATURES 30
#define FEATURE_PACKET_SIZE (1 + NUM_FEATURES * 4 + 1)
#define RESULT_PACKET_START 0x5A
#define RESULT_PACKET_SIZE 4
const int MOTION_UART_BAUD = 115200;
const int MOTION_RX_PIN = 16; // ESP32 GPIO16 (RX2) <- Zybo PS UART1 TX
const int MOTION_TX_PIN = 17; // ESP32 GPIO17 (TX2) -> Zybo PS UART1 RX

// Feature window size and sample cadence must match what actually trained
// the deployed model: the Stage 3 dashboard logger's WINDOW_SIZE (12) at
// its ~20Hz browser poll rate — see dashboard_html.h's LOG_WINDOW_SIZE.
const int MOTION_WINDOW_SIZE = 12;
const unsigned long MOTION_SAMPLE_INTERVAL_MS = 50;

// Must match train_classifier.py's CLASSES list order exactly.
const char* MOTION_CLASSES[5] = {"stationary", "tilt", "freefall", "impact", "shake"};

// ---------- GLOBALS ----------
Adafruit_MPU6050 mpu;
WebServer server(80);

float pitch = 0, roll = 0, yaw = 0;
float ax, ay, az, gx, gy, gz, tempC;
unsigned long lastUpdate = 0;

// axis order: 0=ax 1=ay 2=az 3=gx 4=gy 5=gz, matching zybo_ps/uart_protocol.h
float motionWindow[6][MOTION_WINDOW_SIZE];
unsigned long motionWindowT[MOTION_WINDOW_SIZE];
int motionWindowLen = 0;
unsigned long lastMotionSample = 0;

char motionState[16] = "";  // empty until the first classification arrives
int motionConfidence = 0;

// ---------- HANDLERS ----------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  char json[320];
  snprintf(json, sizeof(json),
    "{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
    "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,"
    "\"pitch\":%.2f,\"roll\":%.2f,\"yaw\":%.2f,\"temp\":%.2f,"
    "\"state\":\"%s\",\"confidence\":%d}",
    ax, ay, az, gx, gy, gz, pitch, roll, yaw, tempC, motionState, motionConfidence);
  server.send(200, "application/json", json);
}

// ---------- SENSOR HELPERS ----------
void configureMpu() {
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

// Converts a raw sensor reading into engineering units and fuses it into the
// running pitch/roll/yaw estimate via a complementary filter.
void updateOrientation(const sensors_event_t& a, const sensors_event_t& g, const sensors_event_t& temp) {
  ax = a.acceleration.x / 9.81; // convert to g
  ay = a.acceleration.y / 9.81;
  az = a.acceleration.z / 9.81;
  gx = g.gyro.x * 57.2958; // rad/s -> deg/s
  gy = g.gyro.y * 57.2958;
  gz = g.gyro.z * 57.2958;
  tempC = temp.temperature;

  unsigned long now = millis();
  float dt = (now - lastUpdate) / 1000.0;
  if (dt <= 0) dt = 0.001;
  lastUpdate = now;

  // Angles from accelerometer alone (only valid when roughly static, but fused below)
  float accelPitch = atan2(-ax, sqrt(ay * ay + az * az)) * 57.2958;
  float accelRoll  = atan2(ay, az) * 57.2958;

  // Complementary filter: mostly trust gyro integration, slowly correct with accel
  pitch = ALPHA * (pitch + gy * dt) + (1 - ALPHA) * accelPitch;
  roll  = ALPHA * (roll + gx * dt) + (1 - ALPHA) * accelRoll;

  // Yaw: pure gyro integration. The MPU6050 has no magnetometer, so there's
  // no absolute heading reference — this WILL slowly drift over time, even
  // when the sensor is perfectly still. Treat it as "relative heading since
  // power-on", not a true compass reading.
  yaw += gz * dt;
  if (yaw >= 360) yaw -= 360;
  if (yaw < 0) yaw += 360;
}

// ---------- MOTION CLASSIFIER (Stage 6) ----------
// Feature math mirrors dashboard_html.h's client-side logger exactly
// (mean/std/min/max/FFT-peak per axis) — the deployed model was trained on
// features from that logger, so this has to match it, not just "be
// reasonable." See that file's extractFeatureRow()/axisStats()/fftPeakHz().

void axisStats(const float* v, int n, float* mean, float* stdOut, float* mn, float* mx) {
  float sum = 0;
  *mn = v[0];
  *mx = v[0];
  for (int i = 0; i < n; i++) {
    sum += v[i];
    if (v[i] < *mn) *mn = v[i];
    if (v[i] > *mx) *mx = v[i];
  }
  *mean = sum / n;

  float var = 0;
  for (int i = 0; i < n; i++) {
    float d = v[i] - *mean;
    var += d * d;
  }
  *stdOut = sqrt(var / n);
}

// Naive O(n^2) DFT peak — fine at this window size (12 samples), no FFT
// library needed. Skips the DC bin (k=0).
float fftPeakHz(const float* v, int n, float sampleRate) {
  float mean = 0;
  for (int i = 0; i < n; i++) mean += v[i];
  mean /= n;

  float bestMag = -1;
  int bestK = 1;
  for (int k = 1; k <= n / 2; k++) {
    float re = 0, im = 0;
    for (int t = 0; t < n; t++) {
      float angle = -2.0 * PI * k * t / n;
      re += (v[t] - mean) * cos(angle);
      im += (v[t] - mean) * sin(angle);
    }
    float mag = sqrt(re * re + im * im);
    if (mag > bestMag) {
      bestMag = mag;
      bestK = k;
    }
  }
  return (float)bestK * sampleRate / n;
}

void sendFeaturePacket(const float features[NUM_FEATURES]) {
  uint8_t pkt[FEATURE_PACKET_SIZE];
  pkt[0] = FEATURE_PACKET_START;
  memcpy(pkt + 1, features, NUM_FEATURES * sizeof(float));

  uint8_t chk = 0;
  for (int i = 1; i < 1 + NUM_FEATURES * 4; i++) chk ^= pkt[i];
  pkt[FEATURE_PACKET_SIZE - 1] = chk;

  Serial2.write(pkt, FEATURE_PACKET_SIZE);
}

// Computes features over the just-filled window and ships them to the
// Zybo. Raw values in natural units (g, deg/s, Hz) — normalization happens
// on the Zybo side (see zybo_ps/scaler_params.h) so only that firmware
// needs updating if the model is ever retrained.
void processMotionWindow() {
  unsigned long dtSum = 0;
  int dtCount = 0;
  for (int i = 1; i < motionWindowLen; i++) {
    long dt = motionWindowT[i] - motionWindowT[i - 1];
    if (dt > 0) {
      dtSum += dt;
      dtCount++;
    }
  }
  float avgDt = dtCount > 0 ? (float)dtSum / dtCount : (float)MOTION_SAMPLE_INTERVAL_MS;
  float sampleRate = 1000.0 / avgDt;

  float features[NUM_FEATURES];
  int idx = 0;
  for (int axis = 0; axis < 6; axis++) {
    float mean, stdDev, mn, mx;
    axisStats(motionWindow[axis], motionWindowLen, &mean, &stdDev, &mn, &mx);
    features[idx++] = mean;
    features[idx++] = stdDev;
    features[idx++] = mn;
    features[idx++] = mx;
    features[idx++] = fftPeakHz(motionWindow[axis], motionWindowLen, sampleRate);
  }

  sendFeaturePacket(features);
}

// Rate-limited sampling into the rolling window, separate from the main
// ~40Hz sensor loop — matches the ~20Hz cadence the training data's
// browser-side logger used (see dashboard_html.h's poll() at 50ms).
void sampleMotionWindow() {
  if (millis() - lastMotionSample < MOTION_SAMPLE_INTERVAL_MS) return;
  lastMotionSample = millis();

  motionWindow[0][motionWindowLen] = ax;
  motionWindow[1][motionWindowLen] = ay;
  motionWindow[2][motionWindowLen] = az;
  motionWindow[3][motionWindowLen] = gx;
  motionWindow[4][motionWindowLen] = gy;
  motionWindow[5][motionWindowLen] = gz;
  motionWindowT[motionWindowLen] = millis();
  motionWindowLen++;

  if (motionWindowLen >= MOTION_WINDOW_SIZE) {
    processMotionWindow();
    motionWindowLen = 0;
  }
}

// Non-blocking check for a classification result from the Zybo. Called
// every loop() iteration; only actually does work when bytes are waiting.
void pollMotionResult() {
  while (Serial2.available()) {
    int b = Serial2.read();
    if (b != RESULT_PACKET_START) continue;

    uint8_t pkt[RESULT_PACKET_SIZE];
    pkt[0] = (uint8_t)b;
    int got = 1;
    unsigned long waitStart = millis();
    while (got < RESULT_PACKET_SIZE && millis() - waitStart < 50) {
      if (Serial2.available()) pkt[got++] = Serial2.read();
    }
    if (got < RESULT_PACKET_SIZE) return; // timed out mid-packet — drop it, resync next time

    uint8_t chk = pkt[1] ^ pkt[2];
    if (chk != pkt[3]) continue; // corrupt packet, keep scanning for the next start byte

    int classIdx = pkt[1];
    if (classIdx >= 0 && classIdx < 5) {
      strncpy(motionState, MOTION_CLASSES[classIdx], sizeof(motionState) - 1);
      motionState[sizeof(motionState) - 1] = '\0';
      motionConfidence = pkt[2];
    }
  }
}

// ---------- I2C BUS RECOVERY ----------
// If a slave device locks up mid-transaction (common when a power dip from
// WiFi TX current spikes browns out the sensor briefly), the I2C bus can get
// stuck with SDA held low. The standard fix is to manually clock SCL a few
// times to force the stuck slave to release the bus, then reinitialize.
void recoverI2CBus() {
  Serial.println("I2C bus recovery: reinitializing...");
  Wire.end();

  pinMode(SCL_PIN, OUTPUT);
  pinMode(SDA_PIN, INPUT_PULLUP);
  for (int i = 0; i < 9; i++) {
    digitalWrite(SCL_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(SCL_PIN, LOW);
    delayMicroseconds(5);
  }
  pinMode(SCL_PIN, INPUT_PULLUP); // release the bus, implicit STOP

  delay(5);
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(100);

  if (mpu.begin()) {
    configureMpu();
    Serial.println("I2C bus recovery: sensor responding again.");
  } else {
    Serial.println("I2C bus recovery: sensor still not responding.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(SDA_PIN, SCL_PIN, 100000); // explicit 100kHz — same speed the scanner used successfully
  Wire.setTimeOut(100); // give the bus more headroom (ms) before declaring a timeout

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check wiring!");
    while (1) delay(1000);
  }
  Serial.println("MPU6050 found.");
  configureMpu();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("Access point \"");
  Serial.print(AP_SSID);
  Serial.println("\" started.");
  Serial.print("Connect to it, then open: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  Serial2.begin(MOTION_UART_BAUD, SERIAL_8N1, MOTION_RX_PIN, MOTION_TX_PIN);

  lastUpdate = millis();
}

void loop() {
  server.handleClient();
  pollMotionResult(); // cheap/non-blocking — keep draining even during an I2C hiccup below

  static int consecutiveFailures = 0;

  sensors_event_t a, g, temp;
  bool ok = mpu.getEvent(&a, &g, &temp);
  if (!ok) {
    static unsigned long lastWarn = 0;
    consecutiveFailures++;

    if (millis() - lastWarn > 2000) { // don't spam Serial
      Serial.println("MPU6050 read failed, skipping this cycle");
      lastWarn = millis();
    }

    if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
      recoverI2CBus();
      consecutiveFailures = 0;
    }

    delay(15);
    return; // keep last-known-good values instead of writing garbage
  }
  consecutiveFailures = 0;

  updateOrientation(a, g, temp);
  sampleMotionWindow(); // only feed the classifier genuinely fresh readings, not a stale/frozen value

  // Machine-readable line for tools/collect_training_data.py (Stage 3). Kept
  // to a fixed "DATA,..." prefix so it's trivially filterable from the rest
  // of this sketch's human-readable Serial.println() debug output.
  Serial.printf("DATA,%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", millis(), ax, ay, az, gx, gy, gz);

  delay(25); // ~40Hz sensor loop — a bit gentler on the bus than before; web clients still poll at ~20Hz
}
