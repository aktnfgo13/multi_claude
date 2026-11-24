#!/usr/bin/env python3
"""
Reproducible benchmark protocol (docs/BENCHMARK_PROTOCOL.md).

  run_benchmarks.py run     [--build build] [--out tests/results/benchmark]
      fixed signals x fixed presets/effects x {44.1, 48, 96 kHz} x {32, 128, 512} block x OS 1/2/4/8
      -> benchmark.json, benchmark.csv, plots (PNG)
  run_benchmarks.py compare --a out_a.wav --b out_b.wav [--ref ref.wav]
      cross-engine comparison on identical input (e.g. another agent's engine), loudness matched.

The same input files, parameters and analysis are used for every engine; nothing is tuned per engine.
"""
import argparse
import csv
import json
import os
import subprocess
import sys

import numpy as np
from scipy.io import wavfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "audio_analysis"))
import mfx_analysis as A  # noqa: E402


def make_inputs(d, fs):
    os.makedirs(d, exist_ok=True)
    t = np.arange(int(fs * 3)) / fs
    rng = np.random.default_rng(11)
    sig = {
        "sine1k_-12": 10 ** (-12 / 20) * np.sin(2 * np.pi * 1000 * t),
        "sine3953_-6": 10 ** (-6 / 20) * np.sin(2 * np.pi * 3953 * t),  # non-divisor: aliasing measurable
        "noise_-20": 0.1 * rng.uniform(-1, 1, len(t)),
    }
    # synthetic guitar-like (Karplus-Strong), clearly labelled synthetic
    x = np.zeros(len(t)); pos = 0
    for f0 in (82.4, 110, 196, 329.6):
        L = int(fs / f0); buf = rng.uniform(-1, 1, L); p = 0
        for i in range(len(t) // 4):
            q = (p + 1) % L; x[pos + i] = 0.4 * buf[p]; buf[p] = 0.497 * (buf[p] + buf[q]); p = q
        pos += len(t) // 4
    sig["guitar_synthetic"] = x
    paths = {}
    for k, v in sig.items():
        p = os.path.join(d, f"{k}_{int(fs)}.wav")
        wavfile.write(p, int(fs), v.astype(np.float32))
        paths[k] = p
    return paths


def render(build, inp, out, bench, args):
    cmd = [os.path.join(build, "mfx_render"), "-i", inp, "-o", out, "--bench", bench] + args
    subprocess.run(cmd, check=True, capture_output=True)
    return json.load(open(bench))


def cmd_run(a):
    out = a.out
    os.makedirs(out, exist_ok=True)
    rows = []
    configs = [("custom_drive_os%d" % (1 << o), ["--effect", "custom_drive", "--set", f"oversampling={o}", "--set", "drive=70"]) for o in range(4)]
    configs += [(os.path.splitext(f)[0], ["--preset", os.path.join(ROOT, "presets", "factory", f)])
                for f in sorted(os.listdir(os.path.join(ROOT, "presets", "factory")))]
    for fs in (44100, 48000, 96000):
        ins = make_inputs(os.path.join(out, "inputs"), fs)
        for block in (32, 128, 512):
            for name, args in configs:
                if fs != 48000 and not name.startswith("custom_drive"):
                    continue
                for sk, p in ins.items():
                    if block != 128 and sk != "guitar_synthetic":
                        continue
                    o = os.path.join(out, "renders", f"{name}_{sk}_{fs}_{block}.wav")
                    os.makedirs(os.path.dirname(o), exist_ok=True)
                    extra = ["--block", str(block)]
                    if sk.startswith("sine"):
                        extra += ["--analyze-thd", sk[4:].split("_")[0]]
                    b = render(a.build, p, o, o.replace(".wav", ".json"), args + extra)
                    row = dict(config=name, signal=sk, sample_rate=fs, block=block, realtime_factor=b["realtime_factor"],
                               cpu_percent=b["cpu_percent_of_realtime"], worst_block_percent=b["worst_block_percent_of_budget"],
                               peak_rss_kb=b["peak_rss_kb"], latency_samples=b["latency_samples"], latency_ms=b["latency_ms"],
                               out_peak_dbfs=b["output_stats_left"]["peak_dbfs"], out_rms_dbfs=b["output_stats_left"]["rms_dbfs"],
                               all_finite=b["output_stats_left"]["all_finite"])
                    if "harmonic_analysis" in b:
                        h = b["harmonic_analysis"]
                        row.update(thd_percent=h["thd_percent"], non_harmonic_db=h["non_harmonic_db"])
                    rows.append(row)
                    print(row["config"], sk, fs, block, f"{row['realtime_factor']:.1f}x")
    json.dump(dict(engine="MultiEffects 0.1.0", rows=rows), open(os.path.join(out, "benchmark.json"), "w"), indent=2)
    keys = sorted({k for r in rows for k in r})
    with open(os.path.join(out, "benchmark.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys); w.writeheader(); w.writerows(rows)
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        sel = [r for r in rows if r["config"].startswith("custom_drive") and r["signal"] == "sine3953_-6" and r["sample_rate"] == 48000]
        fig, ax = plt.subplots(1, 2, figsize=(11, 4))
        xs = [r["config"].replace("custom_drive_", "") for r in sel]
        ax[0].bar(xs, [r["non_harmonic_db"] for r in sel]); ax[0].set_ylabel("non-harmonic energy [dB re fundamental]")
        ax[0].set_title("Custom Drive (drive 70) aliasing, 3953 Hz @ -6 dBFS, 48 kHz")
        ax[1].bar(xs, [r["cpu_percent"] for r in sel]); ax[1].set_ylabel("CPU [% of real time, 1 core]")
        ax[1].set_title("CPU cost vs oversampling (block 128)")
        fig.tight_layout(); fig.savefig(os.path.join(out, "custom_drive_oversampling.png"), dpi=110)
    except Exception as e:  # plotting optional
        print("plot skipped:", e)
    print("wrote", os.path.join(out, "benchmark.json"))


def cmd_compare(a):
    def load(p):
        fs, x = wavfile.read(p)
        x = x.astype(np.float64)
        return fs, (x[:, 0] if x.ndim > 1 else x)
    fs, xa = load(a.a)
    _, xb = load(a.b)
    lag = A.align(xa, xb)
    xb = xb[lag:] if lag > 0 else np.concatenate([np.zeros(-lag), xb])
    n = min(len(xa), len(xb))
    xa, xb = xa[:n], A.loudness_match(xa[:n], xb[:n])
    r = dict(a=a.a, b=a.b, lag_samples=lag, esr_a_vs_b=A.esr(xa, xb), mrstft_a_vs_b=A.mrstft(xa, xb), loudness_matched=True)
    if a.ref:
        _, ref = load(a.ref)
        ref = ref[:n]
        r["esr_a_vs_ref"] = A.esr(ref, A.loudness_match(ref, xa))
        r["esr_b_vs_ref"] = A.esr(ref, A.loudness_match(ref, xb))
    print(json.dumps(r, indent=2))


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    r = sp.add_parser("run"); r.add_argument("--build", default=os.path.join(ROOT, "build"))
    r.add_argument("--out", default=os.path.join(ROOT, "tests", "results", "benchmark"))
    c = sp.add_parser("compare"); c.add_argument("--a", required=True); c.add_argument("--b", required=True); c.add_argument("--ref")
    a = ap.parse_args()
    cmd_run(a) if a.cmd == "run" else cmd_compare(a)


if __name__ == "__main__":
    main()
