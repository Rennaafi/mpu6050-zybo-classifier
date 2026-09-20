#!/usr/bin/env python3
"""
Stage 3: UART data logger for the motion-state classifier.

Reads raw IMU samples the ESP32 streams over USB serial as
"DATA,<millis>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>" lines, groups them into
fixed-size windows, extracts per-axis features (mean/std/min/max/FFT peak),
and appends labeled rows to a CSV for training in Stage 4.

Usage:
    python collect_training_data.py --port COM5
    (omit --port to auto-pick the only available serial port)

While it's running, type a digit + Enter to set the active label, then move
the board. Samples are only collected while a label is active.
    1 stationary   2 tilt   3 freefall   4 impact   5 shake
    0 idle (pause collection)   q quit
"""
import argparse
import csv
import os
import sys
import threading

import numpy as np
import serial
import serial.tools.list_ports

WINDOW_SIZE = 25  # samples per feature window (~0.6s at the ESP32's ~40Hz loop)
AXES = ["ax", "ay", "az", "gx", "gy", "gz"]
LABELS = {
    "1": "stationary",
    "2": "tilt",
    "3": "freefall",
    "4": "impact",
    "5": "shake",
}
OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "training_data.csv")


def pick_port(explicit):
    if explicit:
        return explicit
    ports = list(serial.tools.list_ports.comports())
    if len(ports) == 1:
        return ports[0].device
    print("Multiple (or no) serial ports found — pass one explicitly with --port:")
    for p in ports:
        print(f"  {p.device}  ({p.description})")
    sys.exit(1)


class LabelState:
    """Shared between the input thread and the main serial-reading loop."""
    def __init__(self):
        self.label = None
        self.quit = False
        self.lock = threading.Lock()


def input_thread(state):
    print(__doc__)
    while True:
        cmd = input().strip()
        if cmd == "q":
            with state.lock:
                state.quit = True
            return
        elif cmd == "0":
            with state.lock:
                state.label = None
            print("-> paused")
        elif cmd in LABELS:
            with state.lock:
                state.label = LABELS[cmd]
            print(f"-> collecting: {LABELS[cmd]}")
        else:
            print("unrecognized; use 1-5, 0, or q")


def blank_window():
    w = {axis: [] for axis in AXES}
    w["_t"] = []
    return w


def extract_features(window):
    t = np.array(window["_t"], dtype=float)
    dt = np.diff(t)
    dt = dt[dt > 0]
    sample_rate = 1000.0 / dt.mean() if len(dt) else 40.0

    row = []
    for axis in AXES:
        v = np.array(window[axis], dtype=float)
        row += [v.mean(), v.std(), v.min(), v.max()]

        spectrum = np.abs(np.fft.rfft(v - v.mean()))
        freqs = np.fft.rfftfreq(len(v), d=1.0 / sample_rate)
        if len(spectrum) > 1:
            peak_idx = 1 + np.argmax(spectrum[1:])  # skip the DC bin
            row.append(freqs[peak_idx])
        else:
            row.append(0.0)
    return row


def csv_header():
    cols = ["label"]
    for axis in AXES:
        cols += [f"{axis}_mean", f"{axis}_std", f"{axis}_min", f"{axis}_max", f"{axis}_fft_peak_hz"]
    return cols


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="Serial port, e.g. COM5 (auto-picked if omitted and only one exists)")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    port = pick_port(args.port)
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    write_header = not os.path.exists(OUT_PATH)

    state = LabelState()
    threading.Thread(target=input_thread, args=(state,), daemon=True).start()

    window = blank_window()
    counts = {name: 0 for name in LABELS.values()}

    with serial.Serial(port, args.baud, timeout=1) as ser, \
         open(OUT_PATH, "a", newline="") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(csv_header())

        print(f"Logging to {os.path.abspath(OUT_PATH)}")
        while True:
            with state.lock:
                if state.quit:
                    break
                active_label = state.label

            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line.startswith("DATA,"):
                continue

            parts = line.split(",")
            if len(parts) != 8:
                continue
            try:
                t_ms, ax, ay, az, gx, gy, gz = (float(x) for x in parts[1:])
            except ValueError:
                continue

            if active_label is None:
                window = blank_window()
                continue

            window["_t"].append(t_ms)
            window["ax"].append(ax); window["ay"].append(ay); window["az"].append(az)
            window["gx"].append(gx); window["gy"].append(gy); window["gz"].append(gz)

            if len(window["_t"]) >= WINDOW_SIZE:
                row = extract_features(window)
                writer.writerow([active_label] + row)
                f.flush()
                counts[active_label] += 1
                print(f"  [{active_label}] window #{counts[active_label]} saved (total {sum(counts.values())})")
                window = blank_window()

    print("Stopped. Counts:", counts)


if __name__ == "__main__":
    main()
