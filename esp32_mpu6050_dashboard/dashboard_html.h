#pragma once

// Static web dashboard served from flash at "/". Polls GET /data (JSON) at
// ~20 Hz and renders it with a hand-rolled canvas line chart — no external
// libraries or internet access required, so it works fully offline on the
// ESP32's local WiFi network.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MPU6050 Live Dashboard</title>
<style>
  :root {
    --bg: #0b0d12;
    --panel: #161a23;
    --panel-border: #262c3a;
    --text: #eef1f6;
    --muted: #8b93a1;
    --accent: #4da6ff;
    --accent-2: #6cb4ff;
    --sky: #3a7bd5;
    --ground: #7a5230;
    --good: #3ddc84;
    --bad: #ff4d6d;
    --warn: #ffb648;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background:
      radial-gradient(circle at 15% 0%, rgba(77,166,255,0.08) 0%, transparent 45%),
      radial-gradient(circle at 85% 100%, rgba(77,166,255,0.05) 0%, transparent 45%),
      var(--bg);
    color: var(--text);
    padding: 28px 24px 60px;
  }
  .page { max-width: 1320px; margin: 0 auto; }
  .topbar {
    display: flex;
    justify-content: space-between;
    align-items: flex-end;
    flex-wrap: wrap;
    gap: 12px;
    padding-bottom: 18px;
    margin-bottom: 22px;
    border-bottom: 1px solid var(--panel-border);
  }
  h1 {
    font-size: 1.5rem;
    margin: 0 0 4px 0;
    letter-spacing: -0.01em;
  }
  .sub { color: var(--muted); font-size: 0.85rem; }
  .status-pill {
    display: flex;
    align-items: center;
    gap: 8px;
    background: var(--panel);
    border: 1px solid var(--panel-border);
    padding: 7px 14px 7px 10px;
    border-radius: 999px;
    font-size: 0.8rem;
    color: var(--muted);
  }
  .status-dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: var(--good);
    box-shadow: 0 0 8px var(--good);
    animation: pulse 1.6s ease-in-out infinite;
  }
  .status-dot.offline {
    background: var(--bad);
    box-shadow: 0 0 8px var(--bad);
    animation: none;
  }
  .status-dot.stale {
    background: var(--warn);
    box-shadow: 0 0 8px var(--warn);
    animation: none;
  }
  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.35; }
  }
  .kpi-row {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 14px;
    margin-bottom: 22px;
  }
  .kpi-card {
    background: var(--panel);
    border: 1px solid var(--panel-border);
    border-left: 3px solid var(--kpi-color, var(--accent));
    border-radius: 12px;
    padding: 14px 16px;
    box-shadow: 0 4px 14px rgba(0,0,0,0.25);
  }
  .kpi-label {
    font-size: 0.72rem;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.06em;
    margin-bottom: 6px;
  }
  .kpi-value {
    font-size: 1.7rem;
    font-weight: 650;
    letter-spacing: -0.01em;
  }
  .kpi-value .unit { font-size: 1rem; color: var(--muted); font-weight: 500; margin-left: 2px; }
  .grid {
    display: grid;
    grid-template-columns: 300px 1fr;
    gap: 20px;
  }
  @media (max-width: 800px) {
    .grid { grid-template-columns: 1fr; }
  }
  .panel {
    background: var(--panel);
    border: 1px solid var(--panel-border);
    border-radius: 16px;
    padding: 20px;
    box-shadow: 0 6px 20px rgba(0,0,0,0.28);
  }
  .panel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 12px;
  }
  .panel-title {
    font-size: 0.9rem;
    font-weight: 650;
  }
  .attitude-stage {
    width: 240px;
    height: 240px;
    margin: 0 auto;
    perspective: 650px;
    position: relative;
    background: radial-gradient(circle at 50% 38%, #232834 0%, #12151b 72%);
    border-radius: 16px;
    border: 1px solid #2a2f3a;
  }
  .plane-3d {
    width: 100%;
    height: 100%;
    position: relative;
    transform-style: preserve-3d;
    transition: transform 0.05s linear;
  }
  .p3d-part {
    position: absolute;
    top: 0; left: 0;
    transform-style: preserve-3d;
  }
  .p3d-fuselage-a, .p3d-fuselage-b {
    width: 16px; height: 140px;
    transform: translate3d(112px, 50px, 0);
    background: linear-gradient(to bottom, #eef1f5, #9aa3b0);
    clip-path: polygon(50% 0%, 78% 16%, 78% 90%, 60% 100%, 40% 100%, 22% 90%, 22% 16%);
    border-radius: 2px;
  }
  .p3d-fuselage-b {
    transform: translate3d(112px, 50px, 0) rotateY(90deg);
    background: linear-gradient(to bottom, #c7ccd4, #6e7684);
  }
  .p3d-wing {
    width: 210px; height: 30px;
    transform: translate3d(15px, 77px, 0);
    background: linear-gradient(90deg, #2f7fd6, #6cb4ff, #2f7fd6);
    clip-path: polygon(50% 0%, 100% 65%, 85% 100%, 15% 100%, 0% 65%);
  }
  .p3d-tailwing {
    width: 110px; height: 18px;
    transform: translate3d(65px, 164px, 0);
    background: linear-gradient(90deg, #2f7fd6, #6cb4ff, #2f7fd6);
    clip-path: polygon(50% 0%, 100% 60%, 85% 100%, 15% 100%, 0% 60%);
  }
  .p3d-fin {
    width: 48px; height: 16px;
    transform: translate3d(112px, 168px, 24px) rotateY(90deg);
    background: linear-gradient(to bottom, #ff7a7a, #d64545);
    clip-path: polygon(0% 100%, 100% 100%, 72% 0%);
  }
  .instrument-label {
    text-align: center;
    font-size: 0.78rem;
    color: var(--muted);
    letter-spacing: 0.04em;
    text-transform: uppercase;
    margin-bottom: 8px;
  }
  .compass-wrap {
    width: 220px;
    height: 220px;
    margin: 0 auto;
    border-radius: 50%;
    position: relative;
    background: #1c2029;
    border: 4px solid #2a2f3a;
  }
  .compass-tick {
    position: absolute;
    font-size: 0.75rem;
    font-weight: 700;
    color: var(--muted);
  }
  .compass-tick.n { top: 8px; left: 50%; transform: translateX(-50%); color: #ff6b6b; }
  .compass-tick.e { top: 50%; right: 10px; transform: translateY(-50%); }
  .compass-tick.s { bottom: 8px; left: 50%; transform: translateX(-50%); }
  .compass-tick.w { top: 50%; left: 10px; transform: translateY(-50%); }
  .heading-plane {
    position: absolute;
    top: 50%; left: 50%;
    width: 60px; height: 60px;
    transform: translate(-50%, -50%) rotate(0deg);
    color: var(--accent);
    transition: transform 0.05s linear;
  }
  .instrument-divider { height: 22px; }
  canvas.chart { width: 100%; max-height: 220px; background: #0d0f14; border-radius: 10px; border: 1px solid #20242e; }
  .charts { display: flex; flex-direction: column; gap: 20px; }
  .legend { display: flex; gap: 14px; font-size: 0.78rem; color: var(--muted); }
  .legend span { display: flex; align-items: center; gap: 5px; }
  .legend i { width: 9px; height: 9px; border-radius: 2px; display: inline-block; }
  .footer-note { margin-top: 26px; text-align: center; font-size: 0.75rem; color: var(--muted); }
  .logger-panel { margin-top: 20px; }
  .logger-labels { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 12px; }
  .label-btn {
    background: var(--panel);
    border: 1px solid var(--panel-border);
    border-left: 3px solid var(--label-color, var(--accent));
    color: var(--text);
    padding: 8px 14px;
    border-radius: 8px;
    font-size: 0.85rem;
    cursor: pointer;
  }
  .label-btn.active { background: var(--label-color, var(--accent)); color: #0b0d12; font-weight: 650; }
  .logger-status { color: var(--muted); font-size: 0.85rem; margin-bottom: 10px; }
  .logger-counts { font-size: 0.8rem; color: var(--muted); margin-bottom: 14px; }
  .logger-actions { display: flex; gap: 10px; }
  .logger-actions button {
    background: var(--panel);
    border: 1px solid var(--panel-border);
    color: var(--text);
    padding: 8px 16px;
    border-radius: 8px;
    cursor: pointer;
    font-size: 0.85rem;
  }
  .logger-actions button:hover { border-color: var(--accent); }
</style>
</head>
<body>
<div class="page">
  <div class="topbar">
    <div>
      <h1>MPU6050 Flight Dashboard</h1>
      <div class="sub">GY-521 / MPU6050 over I&sup2;C &middot; streamed from ESP32 over WiFi</div>
    </div>
    <div class="status-pill">
      <span class="status-dot" id="statusDot"></span>
      <span id="statusText">Live &middot; ~20 Hz</span>
    </div>
  </div>

  <div class="kpi-row">
    <div class="kpi-card" style="--kpi-color:#4da6ff">
      <div class="kpi-label">Pitch</div>
      <div class="kpi-value"><span id="pitchVal">0.0</span><span class="unit">&deg;</span></div>
    </div>
    <div class="kpi-card" style="--kpi-color:#3ddc84">
      <div class="kpi-label">Roll</div>
      <div class="kpi-value"><span id="rollVal">0.0</span><span class="unit">&deg;</span></div>
    </div>
    <div class="kpi-card" style="--kpi-color:#ff6b6b">
      <div class="kpi-label">Yaw (relative)</div>
      <div class="kpi-value"><span id="yawVal">0.0</span><span class="unit">&deg;</span></div>
    </div>
    <div class="kpi-card" style="--kpi-color:#ffb648">
      <div class="kpi-label">Temperature</div>
      <div class="kpi-value"><span id="tempVal">0.0</span><span class="unit">&deg;C</span></div>
    </div>
    <div class="kpi-card" style="--kpi-color:#c792ea">
      <div class="kpi-label">Motion State <span style="color:var(--muted); font-weight:500;">(Stage 6, Zybo)</span></div>
      <div class="kpi-value"><span id="motionStateVal">warming up&hellip;</span><span class="unit" id="motionConfidenceVal"></span></div>
    </div>
  </div>

  <svg style="display:none">
    <defs>
      <symbol id="plane-icon" viewBox="0 0 100 100">
        <path d="M50 6 C54 6 56 10 56 16 L56 82 C56 88 54 92 50 92 C46 92 44 88 44 82 L44 16 C44 10 46 6 50 6 Z" fill="currentColor"/>
        <path d="M50 38 L92 58 L92 66 L50 52 L8 66 L8 58 Z" fill="currentColor"/>
        <path d="M50 74 L72 90 L72 96 L50 86 L28 96 L28 90 Z" fill="currentColor"/>
      </symbol>
    </defs>
  </svg>

  <div class="grid">
    <div class="panel">
      <div class="instrument-label">3D Attitude (Pitch, Roll &amp; Yaw)</div>
      <div class="attitude-stage">
        <div class="plane-3d" id="attitudePlane3d">
          <div class="p3d-part p3d-fuselage-a"></div>
          <div class="p3d-part p3d-fuselage-b"></div>
          <div class="p3d-part p3d-wing"></div>
          <div class="p3d-part p3d-tailwing"></div>
          <div class="p3d-part p3d-fin"></div>
        </div>
      </div>

      <div class="instrument-divider"></div>

      <div class="instrument-label">Heading (Yaw, relative)</div>
      <div class="compass-wrap">
        <span class="compass-tick n">N</span>
        <span class="compass-tick e">E</span>
        <span class="compass-tick s">S</span>
        <span class="compass-tick w">W</span>
        <svg class="heading-plane" id="headingPlane"><use href="#plane-icon"></use></svg>
      </div>
    </div>

    <div class="panel charts">
      <div>
        <div class="panel-header">
          <span class="panel-title">Accelerometer <span style="color:var(--muted); font-weight:500;">(g)</span></span>
          <div class="legend">
            <span><i style="background:#4da6ff"></i>X</span>
            <span><i style="background:#4dff88"></i>Y</span>
            <span><i style="background:#ff4d6d"></i>Z</span>
          </div>
        </div>
        <canvas class="chart" id="accelChart" width="480" height="180"></canvas>
      </div>
      <div>
        <div class="panel-header">
          <span class="panel-title">Gyroscope <span style="color:var(--muted); font-weight:500;">(&deg;/s)</span></span>
          <div class="legend">
            <span><i style="background:#4da6ff"></i>X</span>
            <span><i style="background:#4dff88"></i>Y</span>
            <span><i style="background:#ff4d6d"></i>Z</span>
          </div>
        </div>
        <canvas class="chart" id="gyroChart" width="480" height="180"></canvas>
      </div>
    </div>
  </div>

  <div class="panel logger-panel">
    <div class="panel-header">
      <span class="panel-title">Training Data Logger <span style="color:var(--muted); font-weight:500;">(Stage 3 — over WiFi, no USB needed)</span></span>
    </div>
    <div class="logger-labels">
      <button class="label-btn" data-label="stationary" style="--label-color:#4da6ff">Stationary</button>
      <button class="label-btn" data-label="tilt" style="--label-color:#3ddc84">Tilt</button>
      <button class="label-btn" data-label="freefall" style="--label-color:#ffb648">Freefall</button>
      <button class="label-btn" data-label="impact" style="--label-color:#ff4d6d">Impact</button>
      <button class="label-btn" data-label="shake" style="--label-color:#c792ea">Shake</button>
      <button class="label-btn" data-label="">Pause</button>
    </div>
    <div class="logger-status" id="loggerStatus">Paused — pick a label above, then move the board</div>
    <div class="logger-counts" id="loggerCounts"></div>
    <div class="logger-actions">
      <button id="downloadCsvBtn">Download CSV</button>
      <button id="clearLogBtn">Clear Log</button>
    </div>
  </div>

  <div class="footer-note">MPU6050 has no magnetometer — yaw is relative heading since power-on and will drift over time</div>
</div>

<script>
// Lightweight rolling-buffer line chart drawn directly on <canvas>.
// No external libraries or internet access required.
const MAX_POINTS = 60;
const SERIES_COLORS = ['#4da6ff', '#4dff88', '#ff4d6d'];

function makeBuffer() {
  return [Array(MAX_POINTS).fill(0), Array(MAX_POINTS).fill(0), Array(MAX_POINTS).fill(0)];
}

function pushPoint(buffer, values) {
  buffer.forEach((series, i) => {
    series.push(values[i]);
    if (series.length > MAX_POINTS) series.shift();
  });
}

function drawChart(canvas, buffer) {
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  // Auto-scale Y based on visible data, with a little padding
  const all = buffer.flat();
  let min = Math.min(...all), max = Math.max(...all);
  if (min === max) { min -= 1; max += 1; }
  const pad = (max - min) * 0.15;
  min -= pad; max += pad;

  // Gridlines with value labels
  ctx.strokeStyle = '#232833';
  ctx.lineWidth = 1;
  ctx.font = '10px -apple-system, sans-serif';
  ctx.fillStyle = '#5b6472';
  ctx.textBaseline = 'middle';
  for (let i = 0; i <= 4; i++) {
    const y = (h / 4) * i;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
    const value = max - (i / 4) * (max - min);
    const labelY = i === 0 ? y + 8 : (i === 4 ? y - 8 : y);
    ctx.fillText(value.toFixed(1), 6, labelY);
  }

  buffer.forEach((series, s) => {
    ctx.strokeStyle = SERIES_COLORS[s];
    ctx.lineWidth = 2;
    ctx.beginPath();
    series.forEach((v, i) => {
      const x = (i / (MAX_POINTS - 1)) * w;
      const y = h - ((v - min) / (max - min)) * h;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
  });
}

const accelCanvas = document.getElementById('accelChart');
const gyroCanvas = document.getElementById('gyroChart');
const accelBuf = makeBuffer();
const gyroBuf = makeBuffer();

// ---------- Stage 3: WiFi-based training data logger ----------
// Runs entirely client-side against the same /data poll — no USB serial
// needed, so this works with the ESP32 on external power only. Feature
// columns (mean/std/min/max/FFT-peak-Hz per axis) match
// tools/collect_training_data.py's CSV exactly, so the downloaded file is a
// drop-in for tools/train_classifier.py.
const LOG_AXES = ['ax', 'ay', 'az', 'gx', 'gy', 'gz'];
// ~0.6s per window at the ~20Hz browser poll rate (vs. 25 samples at ~40Hz
// for the USB-serial collector) — sized in *time*, not sample count, to
// match the same real-world window the serial-based tool targets.
const LOG_WINDOW_SIZE = 12;
const LOG_LABELS = ['stationary', 'tilt', 'freefall', 'impact', 'shake'];
const LOG_STORAGE_KEY = 'mpu6050_training_log_v1';

function blankLogWindow() {
  const w = { _t: [] };
  LOG_AXES.forEach(a => w[a] = []);
  return w;
}

let activeLabel = null;
let logWindow = blankLogWindow();
let loggedRows = [];
try {
  loggedRows = JSON.parse(localStorage.getItem(LOG_STORAGE_KEY) || '[]');
} catch (e) { loggedRows = []; }

function saveLoggedRows() {
  try { localStorage.setItem(LOG_STORAGE_KEY, JSON.stringify(loggedRows)); } catch (e) { /* storage full/unavailable — keep collecting in memory */ }
}

function axisStats(values) {
  const n = values.length;
  const mean = values.reduce((a, b) => a + b, 0) / n;
  const variance = values.reduce((a, b) => a + (b - mean) * (b - mean), 0) / n;
  return { mean, std: Math.sqrt(variance), min: Math.min(...values), max: Math.max(...values) };
}

// Naive O(n^2) DFT — fine at this window size (12 samples), no library needed.
function fftPeakHz(values, sampleRate) {
  const n = values.length;
  const mean = values.reduce((a, b) => a + b, 0) / n;
  const centered = values.map(v => v - mean);
  let bestMag = -1, bestK = 1;
  for (let k = 1; k <= Math.floor(n / 2); k++) {
    let re = 0, im = 0;
    for (let t = 0; t < n; t++) {
      const angle = -2 * Math.PI * k * t / n;
      re += centered[t] * Math.cos(angle);
      im += centered[t] * Math.sin(angle);
    }
    const mag = Math.hypot(re, im);
    if (mag > bestMag) { bestMag = mag; bestK = k; }
  }
  return bestK * sampleRate / n;
}

function extractFeatureRow(w) {
  const dts = [];
  for (let i = 1; i < w._t.length; i++) {
    const dt = w._t[i] - w._t[i - 1];
    if (dt > 0) dts.push(dt);
  }
  const avgDt = dts.length ? dts.reduce((a, b) => a + b, 0) / dts.length : 50;
  const sampleRate = 1000 / avgDt;

  const row = [];
  LOG_AXES.forEach(axis => {
    const s = axisStats(w[axis]);
    row.push(s.mean, s.std, s.min, s.max, fftPeakHz(w[axis], sampleRate));
  });
  return row;
}

function updateLoggerUI() {
  const counts = {};
  LOG_LABELS.forEach(l => counts[l] = 0);
  loggedRows.forEach(r => { if (counts[r.label] !== undefined) counts[r.label]++; });
  document.getElementById('loggerCounts').textContent =
    LOG_LABELS.map(l => `${l}: ${counts[l]}`).join('   ·   ') + `   ·   total: ${loggedRows.length}`;

  document.getElementById('loggerStatus').textContent = activeLabel
    ? `Collecting "${activeLabel}" — move the board (window ${logWindow._t.length}/${LOG_WINDOW_SIZE})`
    : 'Paused — pick a label above, then move the board';

  document.querySelectorAll('.label-btn').forEach(btn => {
    btn.classList.toggle('active', btn.dataset.label === (activeLabel || ''));
  });
}

document.querySelectorAll('.label-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    activeLabel = btn.dataset.label || null;
    logWindow = blankLogWindow();
    updateLoggerUI();
  });
});

document.getElementById('downloadCsvBtn').addEventListener('click', () => {
  const header = ['label'];
  LOG_AXES.forEach(a => header.push(`${a}_mean`, `${a}_std`, `${a}_min`, `${a}_max`, `${a}_fft_peak_hz`));
  const lines = [header.join(',')];
  loggedRows.forEach(r => lines.push([r.label, ...r.row].join(',')));
  const blob = new Blob([lines.join('\n')], { type: 'text/csv' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `training_data_${Date.now()}.csv`;
  a.click();
  URL.revokeObjectURL(url);
});

document.getElementById('clearLogBtn').addEventListener('click', () => {
  if (!confirm(`Clear all ${loggedRows.length} logged windows? This can't be undone.`)) return;
  loggedRows = [];
  saveLoggedRows();
  updateLoggerUI();
});

function logSample(d) {
  if (!activeLabel) return;
  logWindow._t.push(Date.now());
  LOG_AXES.forEach(axis => logWindow[axis].push(d[axis]));

  if (logWindow._t.length >= LOG_WINDOW_SIZE) {
    loggedRows.push({ label: activeLabel, row: extractFeatureRow(logWindow) });
    saveLoggedRows();
    logWindow = blankLogWindow();
  }
  updateLoggerUI();
}

updateLoggerUI();

const attitudePlane3d = document.getElementById('attitudePlane3d');
const headingPlane = document.getElementById('headingPlane');
const statusDot = document.getElementById('statusDot');
const statusText = document.getElementById('statusText');
let lastGoodTime = Date.now();
let lastValues = null;
let lastChangeTime = Date.now();

async function poll() {
  try {
    const res = await fetch('/data');
    const d = await res.json();

    document.getElementById('pitchVal').textContent = d.pitch.toFixed(1);
    document.getElementById('rollVal').textContent = d.roll.toFixed(1);
    document.getElementById('yawVal').textContent = d.yaw.toFixed(1);
    document.getElementById('tempVal').textContent = d.temp.toFixed(1);

    if (d.state) {
      document.getElementById('motionStateVal').textContent = d.state.charAt(0).toUpperCase() + d.state.slice(1);
      document.getElementById('motionConfidenceVal').textContent = ` ${d.confidence}%`;
    } else {
      document.getElementById('motionStateVal').textContent = 'warming up…';
      document.getElementById('motionConfidenceVal').textContent = '';
    }

    // 3D attitude: the whole airplane model rotates in real 3D space.
    // rotateX = pitch (nose up/down), rotateY = roll (bank), rotateZ = yaw (turn).
    // If any axis looks inverted once you see it move, flip its sign here.
    attitudePlane3d.style.transform =
      `rotateX(${d.pitch}deg) rotateY(${d.roll}deg) rotateZ(${d.yaw}deg)`;

    // Heading indicator: airplane pointer rotates to the current relative heading
    headingPlane.style.transform = `translate(-50%, -50%) rotate(${d.yaw}deg)`;

    pushPoint(accelBuf, [d.ax, d.ay, d.az]);
    pushPoint(gyroBuf, [d.gx, d.gy, d.gz]);
    drawChart(accelCanvas, accelBuf);
    drawChart(gyroCanvas, gyroBuf);
    logSample(d);

    lastGoodTime = Date.now();

    // The ESP32's I2C bus can occasionally wedge while the HTTP server
    // keeps responding successfully with the last-known-good (now frozen)
    // values, which would otherwise hide the problem entirely. Detect that
    // by watching whether the raw accel/gyro readings actually change.
    const signature = `${d.ax},${d.ay},${d.az},${d.gx},${d.gy},${d.gz}`;
    if (signature !== lastValues) {
      lastValues = signature;
      lastChangeTime = Date.now();
    }
    const staleMs = Date.now() - lastChangeTime;

    if (staleMs > 3000) {
      statusDot.classList.remove('offline');
      statusDot.classList.add('stale');
      statusText.textContent = 'Data frozen — sensor may need recovery';
    } else {
      statusDot.classList.remove('offline', 'stale');
      statusText.textContent = 'Live · ~20 Hz';
    }
  } catch (e) {
    // ESP32 web server can occasionally miss a beat under WiFi load; only
    // flag it as offline if several consecutive polls have failed
    if (Date.now() - lastGoodTime > 1000) {
      statusDot.classList.remove('stale');
      statusDot.classList.add('offline');
      statusText.textContent = 'Reconnecting…';
    }
  }
}

setInterval(poll, 50);
poll();
</script>
</body>
</html>
)rawliteral";
