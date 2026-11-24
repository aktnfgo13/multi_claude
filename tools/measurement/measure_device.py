#!/usr/bin/env python3
"""
Hardware measurement of a real pedal / amp (reamp workflow).  STATUS: NOT TESTED WITH HARDWARE.
This tool has only been exercised in "offline" mode with simulated recordings. Never commit fabricated
"measurement" data: every output carries data_kind = "hardware_measurement" only when produced by
`playrec` from a real audio interface.

Workflow
  1. loopback   : connect interface OUT -> IN directly (no pedal). Measures round-trip latency and gain.
       measure_device.py loopback --device "<name>" --out cal.json
  2. playrec    : interface OUT -> reamp box -> pedal -> interface IN, for each knob setting
       measure_device.py playrec --cal cal.json --signal stimulus.wav --label "drive=5,tone=12h" --outdir data/janray
  3. offline    : you recorded in a DAW instead; align/level-correct the recording with the calibration
       measure_device.py offline --cal cal.json --stimulus stimulus.wav --recorded take.wav --label "..." --outdir data/janray
The output directory gets the same index.json layout as research/spice/run_spice.py sweep, so it can be
used directly by research/ddsp/train_ddsp.py.

Recommended stimulus: tools/signal_generator (logsweep + ampsweep) followed by real guitar DI recordings.
Keep the reamp level documented (dBu at the pedal input) in --notes.
"""
import argparse
import json
import os
import sys
import time

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "audio_analysis"))
import mfx_analysis as A  # noqa: E402


def load(p):
    fs, x = wavfile.read(p)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x[:, 0]
    return fs, x


def playrec(x, fs, device):
    try:
        import sounddevice as sd  # optional dependency
    except ImportError:
        raise SystemExit("sounddevice is not installed (pip install sounddevice) — use 'offline' mode instead")
    y = sd.playrec(x.astype(np.float32), samplerate=fs, channels=1, device=device, blocking=True)
    return y[:, 0].astype(np.float64)


def calibrate(stim, rec, fs):
    lag = A.align(stim, rec, max_lag=int(fs))
    r = rec[lag:lag + len(stim)] if lag >= 0 else np.concatenate([np.zeros(-lag), rec])[: len(stim)]
    gain = float(np.dot(stim, r) / np.dot(r, r))
    return dict(latency_samples=int(lag), level_correction=gain, sample_rate=int(fs),
                residual_esr_after_correction=A.esr(stim, gain * r))


def apply_cal(rec, cal, n):
    lag = cal["latency_samples"]
    r = rec[lag:] if lag >= 0 else np.concatenate([np.zeros(-lag), rec])
    r = np.concatenate([r, np.zeros(max(0, n - len(r)))])[:n]
    return r * cal["level_correction"]


def add_item(outdir, fs, stim, out, label, kind, notes):
    os.makedirs(outdir, exist_ok=True)
    idx_path = os.path.join(outdir, "index.json")
    idx = json.load(open(idx_path)) if os.path.exists(idx_path) else dict(meta=dict(data_kind=kind, is_hardware_measurement=kind == "hardware_measurement",
                                                                                notes=notes, created=time.strftime("%Y-%m-%d %H:%M:%S")), items=[])
    tag = f"take{len(idx['items']):03d}"
    wavfile.write(os.path.join(outdir, tag + "_in.wav"), fs, stim.astype(np.float32))
    wavfile.write(os.path.join(outdir, tag + "_out.wav"), fs, out.astype(np.float32))
    params = dict(kv.split("=") for kv in label.split(",")) if label else {}
    params = {k: float(v) if v.replace(".", "", 1).lstrip("-").isdigit() else v for k, v in params.items()}
    idx["items"].append(dict(tag=tag, input=tag + "_in.wav", output=tag + "_out.wav", params=params, label=label))
    json.dump(idx, open(idx_path, "w"), indent=2)
    print("added", tag, "to", idx_path)


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    l = sp.add_parser("loopback"); l.add_argument("--device"); l.add_argument("--signal"); l.add_argument("--out", required=True)
    p = sp.add_parser("playrec"); p.add_argument("--device"); p.add_argument("--cal", required=True); p.add_argument("--signal", required=True)
    p.add_argument("--label", default=""); p.add_argument("--outdir", required=True); p.add_argument("--notes", default="")
    o = sp.add_parser("offline"); o.add_argument("--cal", required=True); o.add_argument("--stimulus", required=True)
    o.add_argument("--recorded", required=True); o.add_argument("--label", default=""); o.add_argument("--outdir", required=True)
    o.add_argument("--notes", default="")
    a = ap.parse_args()
    if a.cmd == "loopback":
        if a.signal:
            fs, x = load(a.signal)
        else:
            fs = 48000
            x, _ = A.farina_sweep(fs, 20, 20000, 3.0, 0.25)
        rec = playrec(np.concatenate([x, np.zeros(fs)]), fs, a.device)
        cal = calibrate(x, rec, fs)
        json.dump(cal, open(a.out, "w"), indent=2)
        print(cal)
    elif a.cmd == "playrec":
        cal = json.load(open(a.cal))
        fs, x = load(a.signal)
        rec = playrec(np.concatenate([x, np.zeros(fs)]), fs, a.device)
        add_item(a.outdir, fs, x, apply_cal(rec, cal, len(x)), a.label, "hardware_measurement", a.notes)
    else:
        cal = json.load(open(a.cal))
        fs, x = load(a.stimulus)
        _, rec = load(a.recorded)
        add_item(a.outdir, fs, x, apply_cal(rec, cal, len(x)), a.label, "offline_recording_user_supplied", a.notes)


if __name__ == "__main__":
    main()
