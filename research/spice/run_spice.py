#!/usr/bin/env python3
"""
ngspice automation (spec §13).

  run      : simulate one netlist with a WAV (or generated) input -> WAV + metadata JSON
  sweep    : parameter x amplitude sweep -> dataset directory (WAV pairs + index.json) for DDSP
  validate : linear circuits vs analytic responses, diode clipper vs C++ models

All outputs are labelled as SPICE SIMULATION (never as hardware measurements):
  metadata["data_kind"] = "spice_simulation"
Requires ngspice (tested with ngspice-42) on PATH.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "research", "common"))
import mfx_dsp as M  # noqa: E402

NETLISTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "netlists")


def ngspice_version():
    try:
        out = subprocess.run(["ngspice", "--version"], capture_output=True, text=True).stdout
        m = re.search(r"ngspice-(\d+)", out)
        return f"ngspice-{m.group(1)}" if m else out.strip().splitlines()[0]
    except FileNotFoundError:
        return None


def simulate(netlist: str, x: np.ndarray, fs: float, params: dict | None = None, oversample: int = 8, vscale: float = 1.0):
    """Run a transient simulation. x is in volts after multiplying by vscale. Returns output volts at fs."""
    if shutil.which("ngspice") is None:
        raise RuntimeError("ngspice not found on PATH")
    src = open(os.path.join(NETLISTS, netlist)).read()
    t = np.arange(len(x)) / fs
    v = np.asarray(x, dtype=np.float64) * vscale
    with tempfile.TemporaryDirectory() as d:
        pwl = os.path.join(d, "in.pwl")
        # XSPICE filesource code model: two columns "time value" (linear interpolation)
        np.savetxt(pwl, np.column_stack([t, v]), fmt="%.9e")
        source = (f"Ain %v([in]) insrc\n"
                  f".model insrc filesource (file=\"{pwl}\" amploffset=[0] amplscale=[1] "
                  f"timeoffset=0 timescale=1 timerelative=false amplstep=false)")
        body = src.replace("{INPUT_SOURCE}", source)
        for k, val in (params or {}).items():
            body = re.sub(rf"^\.param\s+{k}=\S+", f".param {k}={val}", body, flags=re.M)
        tstep = 1.0 / (fs * oversample)
        tstop = len(x) / fs
        out_txt = os.path.join(d, "out.txt")
        ctrl = f"""
.options reltol=1e-4 abstol=1e-12 vntol=1e-7 method=trap
.control
set filetype=ascii
tran {tstep:.6e} {tstop:.6e} 0 {tstep:.6e}
wrdata {out_txt} v(out)
quit
.endc
"""
        body = body.replace(".end\n", ctrl + ".end\n") if body.rstrip().endswith(".end") else body + ctrl + "\n.end\n"
        cir = os.path.join(d, "sim.cir")
        open(cir, "w").write(body)
        r = subprocess.run(["ngspice", "-b", cir], capture_output=True, text=True)
        if not os.path.exists(out_txt):
            raise RuntimeError("ngspice failed:\n" + r.stdout[-2000:] + r.stderr[-2000:])
        data = np.loadtxt(out_txt)
    ts, vs = data[:, 0], data[:, 1]
    return np.interp(t, ts, vs)


def metadata(netlist, params, fs, vscale, note=""):
    return dict(data_kind="spice_simulation", is_hardware_measurement=False, simulator=ngspice_version(),
                netlist=netlist, params=params or {}, sample_rate=fs, input_volts_per_fullscale=vscale, note=note)


def cmd_run(a):
    if a.input:
        fs, x = M.read_wav(a.input)
        x = x[:, 0] if x.ndim > 1 else x
    else:
        fs = a.fs
        t = np.arange(int(fs * a.seconds)) / fs
        x = a.amp * np.sin(2 * np.pi * a.freq * t)
    params = dict(p.split("=") for p in a.param or [])
    y = simulate(a.netlist, x, fs, params, a.oversample, a.vscale)
    M.write_wav(a.out, fs, y / a.vscale)
    json.dump(metadata(a.netlist, params, fs, a.vscale), open(os.path.splitext(a.out)[0] + ".json", "w"), indent=2)
    print("wrote", a.out)


def guitar_like(fs, secs, seed):
    """Synthetic plucked-string signal (Karplus-Strong), labelled synthetic."""
    rng = np.random.default_rng(seed)
    out = np.zeros(int(fs * secs))
    pos = 0
    for f0 in [82.4, 110, 146.8, 196, 246.9, 329.6]:
        n = int(fs * secs / 6)
        L = int(fs / f0)
        buf = rng.uniform(-1, 1, L)
        p = 0
        for i in range(n):
            q = (p + 1) % L
            if pos + i < len(out):
                out[pos + i] = buf[p]
            buf[p] = 0.497 * (buf[p] + buf[q])
            p = q
        pos += n
    return out / np.max(np.abs(out))


def cmd_sweep(a):
    os.makedirs(a.outdir, exist_ok=True)
    fs = a.fs
    index = dict(meta=metadata(a.netlist, {}, fs, a.vscale, "dataset for DDSP; input = synthetic guitar-like + sine sweep"), items=[])
    t = np.arange(int(fs * 1.0)) / fs
    sweep = np.sin(2 * np.pi * (50 * t + (8000 - 50) / 2 * t * t))  # linear chirp 50 Hz -> 8 kHz
    base = np.concatenate([guitar_like(fs, 2.0, 1), sweep])
    values = [None] if not a.param_name else [float(v) for v in a.values.split(",")]
    for pv in values:
        for amp in [float(v) for v in a.amps.split(",")]:
            params = {a.param_name: pv} if pv is not None else {}
            x = amp * base
            y = simulate(a.netlist, x, fs, params, a.oversample, a.vscale)
            tag = f"{os.path.splitext(a.netlist)[0]}_{a.param_name or 'fixed'}{'' if pv is None else f'{pv:g}'}_amp{amp:g}"
            M.write_wav(os.path.join(a.outdir, tag + "_in.wav"), fs, x)
            M.write_wav(os.path.join(a.outdir, tag + "_out.wav"), fs, y / a.vscale)
            index["items"].append(dict(tag=tag, input=tag + "_in.wav", output=tag + "_out.wav", amp=amp, params=params))
            print("simulated", tag)
    json.dump(index, open(os.path.join(a.outdir, "index.json"), "w"), indent=2)


TOLERANCES = {  # relative 1-sigma-like uniform half-widths (typical parts)
    "RGV": (4.7e3, 0.05), "CGV": (47e-9, 0.10), "CFV": (51e-12, 0.10), "ISV": (2.52e-9, 0.30),
    "NV": (1.752, 0.03), "RFB": (51e3, 0.05), "RPOT": (500e3, 0.20),
}


def cmd_tolerance(a):
    """Monte-Carlo component tolerance sweep -> per-variant metrics + seed-disjoint train/val/test split.

    Answers: how much do real units of the same circuit differ (gain, THD, onset)? A model error
    smaller than the unit-to-unit spread is not audible as 'wrong circuit'."""
    os.makedirs(a.outdir, exist_ok=True)
    fs = a.fs
    rng = np.random.default_rng(a.seed)
    t = np.arange(int(fs * 0.25)) / fs
    levels = [0.03, 0.1, 0.3, 1.0]
    variants = []
    for v in range(a.n):
        prm = {"DRIVE": a.drive}
        if v > 0:  # variant 0 = nominal
            for k, (nom, tol) in TOLERANCES.items():
                prm[k] = f"{nom * (1 + rng.uniform(-tol, tol)):.6g}"
        row = dict(variant=v, params=prm, gain_db=[], thd_pct=[])
        for amp in levels:
            x = amp * np.sin(2 * np.pi * 440 * t)
            y = simulate("feedback_clipper_tol.cir", x, fs, prm, a.oversample, a.vscale)[len(t) // 2:]
            sp = np.abs(np.fft.rfft(y * np.hanning(len(y))))
            b = int(round(440 * len(y) / fs))
            h1 = sp[b - 2:b + 3].max()
            hs = [sp[k * b - 2:k * b + 3].max() for k in range(2, 10)]
            row["gain_db"].append(float(20 * np.log10(h1 / (np.abs(np.fft.rfft(x[len(t) // 2:] * np.hanning(len(y))))[b - 2:b + 3].max()))))
            row["thd_pct"].append(float(100 * np.sqrt(np.sum(np.square(hs))) / h1))
        variants.append(row)
        print("variant", v, [round(g, 2) for g in row["gain_db"]], [round(h, 2) for h in row["thd_pct"]])
    idx = np.arange(1, a.n); rng.shuffle(idx)
    ntr = int(0.6 * len(idx)); nva = int(0.2 * len(idx))
    split = dict(train=[0] + sorted(idx[:ntr].tolist()), validation=sorted(idx[ntr:ntr + nva].tolist()),
                 test=sorted(idx[ntr + nva:].tolist()))
    G = np.array([r["gain_db"] for r in variants]); T = np.array([r["thd_pct"] for r in variants])
    summary = dict(data_kind="spice_simulation", netlist="feedback_clipper_tol.cir", drive=a.drive,
                   input_peak_v=levels, tolerances=TOLERANCES, split=split,
                   gain_db_spread_p2p=(G.max(0) - G.min(0)).tolist(), thd_pct_min=T.min(0).tolist(),
                   thd_pct_max=T.max(0).tolist(), variants=variants, ngspice=ngspice_version())
    json.dump(summary, open(os.path.join(a.outdir, "tolerance_summary.json"), "w"), indent=2)
    print("gain spread p2p [dB]:", np.round(G.max(0) - G.min(0), 2), " THD range [%]:", np.round(T.min(0), 2), np.round(T.max(0), 2))


def cmd_validate(a):
    """Checks the SPICE pipeline itself and compares with the C++ clipper models."""
    fs = 48000.0
    report = dict(simulator=ngspice_version(), checks=[])
    # 1) RC low-pass: |H| at fc must be -3.01 dB
    for net, fc, kind in [("rc_lowpass.cir", 1591.5, "lp"), ("rc_highpass.cir", 72.34, "hp")]:
        t = np.arange(int(fs * 0.5)) / fs
        x = 0.5 * np.sin(2 * np.pi * fc * t)
        y = simulate(net, x, fs)
        g = 20 * np.log10(np.sqrt(np.mean(y[len(y) // 2:] ** 2)) / np.sqrt(np.mean(x[len(x) // 2:] ** 2)))
        report["checks"].append(dict(name=f"{net} gain at fc", value_db=float(g), expected_db=-3.01, ok=bool(abs(g + 3.01) < 0.1)))
    # 2) diode clipper vs C++ models (modeling study export, 1 V = 1.0 in the WAV)
    study = os.path.join(ROOT, "audio", "output", "modeling_study")
    if os.path.exists(study + "_input.wav"):
        _, x = M.read_wav(study + "_input.wav")
        y = simulate("diode_clipper.cir", x, fs, oversample=16)
        M.write_wav(os.path.join(ROOT, "audio", "reference", "spice_diode_clipper_sweep.wav"), fs, y)
        for model in ["tanh", "diode_static_newton", "rc_diode_ode", "wdf_diode"]:
            p = f"{study}_{model}.wav"
            if not os.path.exists(p):
                continue
            _, ym = M.read_wav(p)
            n = min(len(y), len(ym)) - 200
            esr = float(np.sum((y[:n] - ym[:n]) ** 2) / np.sum(y[:n] ** 2))
            report["checks"].append(dict(name=f"C++ {model} (4x OS) vs SPICE diode clipper", esr=esr, esr_db=float(10 * np.log10(esr))))
    out = os.path.join(ROOT, "tests", "results", "spice_validation.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(report, open(out, "w"), indent=2)
    for c in report["checks"]:
        print(c)


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    r = sp.add_parser("run")
    r.add_argument("netlist"); r.add_argument("--input"); r.add_argument("--out", required=True)
    r.add_argument("--fs", type=float, default=48000); r.add_argument("--seconds", type=float, default=1.0)
    r.add_argument("--freq", type=float, default=440); r.add_argument("--amp", type=float, default=0.5)
    r.add_argument("--param", action="append"); r.add_argument("--oversample", type=int, default=8)
    r.add_argument("--vscale", type=float, default=1.0)
    s = sp.add_parser("sweep")
    s.add_argument("netlist"); s.add_argument("--outdir", required=True)
    s.add_argument("--fs", type=float, default=48000); s.add_argument("--amps", default="0.1,0.3,1.0")
    s.add_argument("--param-name"); s.add_argument("--values", default="")
    s.add_argument("--oversample", type=int, default=8); s.add_argument("--vscale", type=float, default=1.0)
    tl = sp.add_parser("tolerance")
    tl.add_argument("--outdir", required=True); tl.add_argument("--n", type=int, default=16)
    tl.add_argument("--drive", type=float, default=0.5); tl.add_argument("--seed", type=int, default=1)
    tl.add_argument("--fs", type=float, default=48000); tl.add_argument("--oversample", type=int, default=8)
    tl.add_argument("--vscale", type=float, default=1.0)
    sp.add_parser("validate")
    a = ap.parse_args()
    {"run": cmd_run, "sweep": cmd_sweep, "validate": cmd_validate, "tolerance": cmd_tolerance}[a.cmd](a)


if __name__ == "__main__":
    main()
