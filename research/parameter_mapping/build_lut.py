#!/usr/bin/env python3
"""
Knob mapping: per-knob-position DDSP fits -> look-up table of PHYSICAL parameters (spec §12 노브 매핑).

* Parameters are interpolated in a stable domain: cut-offs / thresholds / knee in log space, gains in dB,
  mix in percent — never raw filter coefficients. The C++ side (DdspDrive + KnobLut) recomputes the
  filter coefficients from the interpolated physical values (always stable first-order sections).
* Held-out validation: build the LUT from a subset of knob positions (default 0, 50, 100 %), evaluate the
  interpolated model at the held-out positions with the C++ engine and compare against SPICE and against
  the per-position fits.

Usage:
  build_lut.py --summary research/ddsp/results/ddsp_spice_feedback_clipper_summary.json \
               --dataset audio/reference/spice_feedback_clipper --train-knobs 0,50,100
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "research", "common"))
import mfx_dsp as M  # noqa: E402

PARAMS = [("hpf_hz", "log"), ("gain_db", "lin"), ("thr_pos", "log"), ("thr_neg", "log"), ("knee", "log"),
          ("lpf_hz", "log"), ("out_db", "lin"), ("mix", "lin"), ("level", "lin")]
FS = 48000
LAT = M.LATENCY[1]


def build(results, knobs):
    sel = sorted([r for r in results if round(r["param_value"] * 100) in knobs], key=lambda r: r["param_value"])
    lut = dict(format="mfx-knob-lut", version=1, target="ddsp_drive",
               source="DDSP fits to SPICE simulation (feedback clipper DRIVE sweep) — not a hardware measurement",
               knob=[round(r["param_value"] * 100, 6) for r in sel], params={})
    for p, dom in PARAMS:
        lut["params"][p] = dict(domain=dom, values=[float(r["params"][p]) for r in sel])
    return lut


def render_lut(build_dir, lut_path, knob, x):
    with tempfile.TemporaryDirectory() as d:
        i, o = os.path.join(d, "i.wav"), os.path.join(d, "o.wav")
        M.write_wav(i, FS, x)
        subprocess.run([os.path.join(build_dir, "mfx_render"), "-i", i, "-o", o, "--effect", "ddsp_drive",
                        "--state", lut_path, "--set", f"knob={knob}", "--set", "oversampling=1"], check=True, capture_output=True)
        return M.read_wav(o)[1][:, 0]


def esr(t, y):
    return float(np.sum((t - y) ** 2) / np.sum(t ** 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--summary", required=True)
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--train-knobs", default="0,50,100")
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--outdir", default=os.path.join(ROOT, "research", "parameter_mapping", "results"))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    results = json.load(open(a.summary))
    train = [int(v) for v in a.train_knobs.split(",")]
    lut = build(results, train)
    lut_path = os.path.join(a.outdir, "feedback_clipper_drive_lut.json")
    json.dump(lut, open(lut_path, "w"), indent=2)
    full = build(results, [round(r["param_value"] * 100) for r in results])
    json.dump(full, open(os.path.join(a.outdir, "feedback_clipper_drive_lut_all_points.json"), "w"), indent=2)

    idx = json.load(open(os.path.join(a.dataset, "index.json")))
    report = dict(lut=lut_path, train_knobs=train, rows=[])
    for r in sorted(results, key=lambda r: r["param_value"]):
        k = round(r["param_value"] * 100)
        items = [i for i in idx["items"] if abs(next(iter(i["params"].values())) - r["param_value"]) < 1e-9]
        e = []
        for it in items:
            x = M.read_wav(os.path.join(a.dataset, it["input"]))[1]
            y = M.read_wav(os.path.join(a.dataset, it["output"]))[1]
            yt = np.concatenate([np.zeros(LAT), y])[: len(y)]
            yc = render_lut(a.build, lut_path, k, x)[: len(y)]
            v = slice(2 * FS + 4096, None)  # validation part (chirp)
            e.append(esr(yt[v], yc[v]))
        row = dict(knob=k, held_out=k not in train, esr_lut_interpolated_cpp=float(np.mean(e)),
                   esr_direct_fit_cpp=r["validation_chirp"]["esr_cpp_engine"])
        report["rows"].append(row)
        print(row)
    # smoothness / stability of the mapping on a fine grid (first-order sections are stable for fc > 0)
    grid = np.linspace(0, 100, 101)
    kn = np.array(lut["knob"])
    for p, dom in PARAMS:
        v = np.array(lut["params"][p]["values"])
        g = np.exp(np.interp(grid, kn, np.log(v))) if dom == "log" else np.interp(grid, kn, v)
        assert np.all(np.isfinite(g)) and (dom != "log" or np.all(g > 0)), p
    report["mapping_checks"] = "all interpolated values finite; log-domain values > 0 (stable 1st-order filters)"
    json.dump(report, open(os.path.join(a.outdir, "knob_mapping_validation.json"), "w"), indent=2)


if __name__ == "__main__":
    main()
