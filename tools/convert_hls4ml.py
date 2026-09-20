#!/usr/bin/env python3
"""
Stage 5: quantize the Stage 4 Keras model to fixed-point and convert it to
an HLS IP core with hls4ml, targeting the Zybo Z7-10's Zynq-7010
(xc7z010clg400-1) PL fabric.

Uses hls4ml's 'Vitis' backend (not the older 'Vivado' backend) — this repo
targets a Vitis HLS install (Xilinx folded standalone Vivado HLS into Vitis
starting ~2020.2; there's no separate vivado_hls anymore). --no-synth only
needs a C++ compiler on PATH (for hls4ml's csim step); the full synth step
additionally needs Vitis HLS's vitis_hls itself on PATH. Neither is covered
by tools/requirements.txt; see tools/requirements-hls4ml.txt.

IP interface: io_parallel (plain C array I/O) + an AXI4-Lite control/data
interface, patched in after hls4ml writes the project (see PATCH_INTERFACE
below) — not io_stream. io_stream's AXI4-Stream ports came out as 480-bit
(in) / 80-bit (out), which don't match any standard AXI DMA width; AXI-Lite
lets the Zynq PS just read/write the whole in[30]/out[5] arrays as
memory-mapped registers directly, no DMA or width converter needed.

Usage:
    python convert_hls4ml.py            # csim + synth
    python convert_hls4ml.py --no-synth # csim only, skip the slow HLS build
"""
import argparse
import os
import re
import shutil
import subprocess

import numpy as np
import tensorflow as tf
from sklearn.model_selection import train_test_split

if os.name == 'nt':
    # The compiled .so is a MinGW-built PE DLL that depends on
    # libgcc_s_seh-1.dll / libstdc++-6.dll from the compiler's own bin dir.
    # Python 3.8+ no longer implicitly searches PATH for DLL dependencies —
    # os.add_dll_directory() is the required replacement — so without this,
    # ctypes.cdll.LoadLibrary() fails with a misleading "module not found"
    # even though the .so file itself exists right where it's expected.
    _gxx = shutil.which('g++')
    if _gxx:
        os.add_dll_directory(os.path.dirname(_gxx))

    # hls4ml's FPGABackend.compile() always runs ['./build_lib.sh'] with
    # shell=True, which on Windows dispatches through cmd.exe — and cmd.exe
    # can't run a bash script ("'.' is not recognized..."). Redirect just
    # that one call through bash (Git Bash's bash.exe), which handles it fine.
    _original_run = subprocess.run

    def _find_real_bash():
        # shutil.which('bash') is PATH-order-dependent, and PATH gets messy
        # once Xilinx's settings64.bat scripts are sourced (needed for
        # vitis-run) — it can resolve to Windows' WSL launcher stub
        # (...\WindowsApps\bash.exe) instead of Git Bash, which mangles this
        # script's line endings/quoting instead of running it. Prefer Git
        # Bash's known install locations explicitly.
        for candidate in (
            r'C:\Program Files\Git\bin\bash.exe',
            r'C:\Program Files\Git\usr\bin\bash.exe',
            r'D:\Program Files\Git\bin\bash.exe',
            r'D:\Program Files\Git\usr\bin\bash.exe',
        ):
            if os.path.exists(candidate):
                return candidate
        return shutil.which('bash') or 'bash'

    _bash_path = _find_real_bash()

    def _run_build_lib_via_bash(cmd, *args, **kwargs):
        if cmd == ['./build_lib.sh']:
            bash = _bash_path
            kwargs['shell'] = False
            return _original_run([bash, 'build_lib.sh'], *args, **kwargs)
        return _original_run(cmd, *args, **kwargs)

    subprocess.run = _run_build_lib_via_bash


def patch_interface_to_axi_lite(output_dir, project_name="myproject"):
    """hls4ml's io_parallel writer always emits a plain ap_vld interface
    pragma for the top-level function's array ports. There's no hls4ml
    config knob to make it emit s_axilite instead (that's specific to the
    separate VivadoAccelerator backend, which needs the old vivado_hls
    binary we don't have on this machine) — so patch the generated source
    directly. Must run after hls_model.write()/.compile() (which generates
    the file) and before hls_model.build() (which synthesizes it)."""
    cpp_path = os.path.join(output_dir, "firmware", f"{project_name}.cpp")
    with open(cpp_path, "r") as f:
        src = f.read()

    match = re.search(r"#pragma HLS INTERFACE ap_vld port=([^\s]+)\s*\n", src)
    if not match:
        raise SystemExit(f"Expected ap_vld interface pragma not found in {cpp_path} — "
                          f"hls4ml's generated code may have changed; update patch_interface_to_axi_lite().")
    ports = match.group(1).split(",")
    axi_lite_pragmas = "".join(f"    #pragma HLS INTERFACE s_axilite port={p} bundle=CTRL\n" for p in ports)
    axi_lite_pragmas += "    #pragma HLS INTERFACE s_axilite port=return bundle=CTRL\n"
    src = src[:match.start()] + axi_lite_pragmas + src[match.end():]

    with open(cpp_path, "w") as f:
        f.write(src)

HERE = os.path.dirname(__file__)
DATA_PATH = os.path.join(HERE, "..", "data", "training_data.csv")
MODEL_DIR = os.path.join(HERE, "..", "models")
# Vitis HLS refuses any space anywhere in the project path ("Project/solution
# path ... contains illegal character ' '") — and both this repo's folder
# ("ngulik fpga\mpu6050 enhancement") and the Windows user profile
# ("C:\Users\Muhammad Refansa") have spaces in them, so the HLS project can't
# live under either. Default to a space-free path outside both; override
# with --output-dir if you'd rather put it somewhere else space-free.
DEFAULT_OUTPUT_DIR = r"D:\hls_builds\mpu6050_motion_classifier"
FPGA_PART = "xc7z010clg400-1"  # Zybo Z7-10
CLASSES = ["stationary", "tilt", "freefall", "impact", "shake"]  # must match train_classifier.py


def reload_test_split():
    """Reproduces train_classifier.py's split (same seed) so we validate
    quantized accuracy on data the float model never trained on."""
    import pandas as pd

    df = pd.read_csv(DATA_PATH)
    X = df.drop(columns=["label"]).to_numpy(dtype=np.float32)
    y = np.array([CLASSES.index(label) for label in df["label"]])
    _, X_test, _, y_test = train_test_split(X, y, test_size=0.2, stratify=y, random_state=0)

    scaler = np.load(os.path.join(MODEL_DIR, "scaler.npz"))
    X_test = (X_test - scaler["mean"]) / scaler["scale"]
    return X_test.astype(np.float32), y_test


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-synth", action="store_true", help="Skip the slow Vivado HLS synthesis step")
    ap.add_argument("--precision", default="ap_fixed<16,6>",
                     help="Fixed-point type for weights/activations. Default validated at zero accuracy loss "
                          "vs. float — ap_fixed<8,3> loses ~21 points (3 integer bits can't cover the "
                          "dynamic range of the shake/impact gyro features).")
    ap.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR,
                     help=f"Where to write the HLS project. Must be a space-free path — Vitis HLS rejects any "
                          f"space in the project path. Default: {DEFAULT_OUTPUT_DIR}")
    ap.add_argument("--reuse-factor", type=int, default=16,
                     help="Higher = more time-multiplexed (less parallel) hardware: fewer DSPs/LUTs, more "
                          "latency. Default 16 is validated to comfortably fit the Zynq-7010 (DSP 47%%, "
                          "LUT 22%%) with Strategy=Resource; ReuseFactor=1 overflows it (DSP 577%%).")
    args = ap.parse_args()

    if " " in os.path.abspath(args.output_dir):
        raise SystemExit(f"--output-dir '{args.output_dir}' contains a space — Vitis HLS will reject it. "
                          f"Pick a space-free path (e.g. {DEFAULT_OUTPUT_DIR}).")

    import hls4ml  # imported late so --help works without it installed

    model_path = os.path.join(MODEL_DIR, "motion_classifier.h5")
    if not os.path.exists(model_path):
        raise SystemExit(f"No model at {os.path.abspath(model_path)} — run train_classifier.py first (Stage 4).")
    model = tf.keras.models.load_model(model_path)

    hls_config = hls4ml.utils.config_from_keras_model(model, granularity="name", backend="Vitis")
    hls_config["Model"]["Precision"] = args.precision
    hls_config["Model"]["ReuseFactor"] = args.reuse_factor
    # 'Latency' strategy (the default) fully unrolls the dot-product logic
    # regardless of ReuseFactor, so raising ReuseFactor alone just adds
    # muxing overhead on top of that — LUTs go up, not down. 'Resource'
    # strategy is the one actually designed to share MAC hardware across a
    # higher ReuseFactor (and can move weights into BRAM instead of LUTs).
    hls_config["Model"]["Strategy"] = "Resource"
    for layer_cfg in hls_config["LayerName"].values():
        layer_cfg["Precision"] = args.precision
        # config_from_keras_model(granularity="name") gives every layer its
        # own ReuseFactor/Strategy (defaults: 1 / Latency), which overrides
        # the Model-level defaults above — has to be set here too or it's
        # silently ignored.
        layer_cfg["ReuseFactor"] = args.reuse_factor
        layer_cfg["Strategy"] = "Resource"

    hls_model = hls4ml.converters.convert_from_keras_model(
        model,
        hls_config=hls_config,
        output_dir=args.output_dir,
        backend="Vitis",
        part=FPGA_PART,
        io_type="io_parallel",
    )
    hls_model.compile()  # writes the project, then csim-compiles it
    patch_interface_to_axi_lite(args.output_dir)

    X_test, y_test = reload_test_split()
    float_pred = model.predict(X_test).argmax(axis=1)
    hls_pred = hls_model.predict(X_test).argmax(axis=1)
    float_acc = (float_pred == y_test).mean()
    hls_acc = (hls_pred == y_test).mean()
    agree = (float_pred == hls_pred).mean()

    print(f"Float Keras accuracy:        {float_acc:.3f}")
    print(f"Quantized (csim) accuracy:   {hls_acc:.3f}")
    print(f"Float vs quantized agreement: {agree:.3f}")
    if hls_acc < float_acc - 0.05:
        print(f"Quantization cost >5 points of accuracy — try a wider "
              f"Precision than {args.precision} (e.g. ap_fixed<16,6>) before synthesizing.")

    if not args.no_synth:
        print("\nRunning HLS synthesis (this can take several minutes)...")
        hls_model.build(csim=False, synth=True, export=True)
        print(f"HLS project written to {os.path.abspath(args.output_dir)}")
    else:
        print("\n--no-synth passed, skipping HLS build.")


if __name__ == "__main__":
    main()
