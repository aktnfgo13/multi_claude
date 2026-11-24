#!/usr/bin/env python3
"""
Baselines for the DDSP study on the same SPICE data (DRIVE = 0.5 by default):

  1. hand-designed C++ `overdrive` block (not fitted; only output gain & latency are matched)
  2. black-box GRU (hidden 16) — a NAM-*style* recurrent baseline trained here. It is NOT NAM
     (NAM uses WaveNet/LSTM architectures and its own trainer); it shows what a small black-box
     model reaches with the same data and a similar CPU-time budget.
  3. DDSP result read from research/ddsp/results (if present)

Writes research/graybox/results/baselines.json
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
import torch

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "research", "common"))
import mfx_dsp as M  # noqa: E402

FS = 48000


def load(dataset, pv):
    idx = json.load(open(os.path.join(dataset, "index.json")))
    items = [i for i in idx["items"] if abs(next(iter(i["params"].values())) - pv) < 1e-9]
    xs = [M.read_wav(os.path.join(dataset, i["input"]))[1] for i in items]
    ys = [M.read_wav(os.path.join(dataset, i["output"]))[1] for i in items]
    return xs, ys


def esr(t, y):
    return float(np.sum((t - y) ** 2) / np.sum(t ** 2))


def best_gain_esr(t, y):
    g = float(np.dot(t, y) / (np.dot(y, y) + 1e-20))
    return esr(t, g * y), g


def overdrive_baseline(xs, ys, build, drive_percent):
    out = []
    for x, y in zip(xs, ys):
        with tempfile.TemporaryDirectory() as d:
            i, o = os.path.join(d, "i.wav"), os.path.join(d, "o.wav")
            M.write_wav(i, FS, x)
            subprocess.run([os.path.join(build, "mfx_render"), "-i", i, "-o", o, "--effect", "overdrive",
                            "--set", f"drive={drive_percent}", "--set", "tone=100", "--set", "level=0",
                            "--compensate-latency"], check=True, capture_output=True)
            yc = M.read_wav(o)[1][:, 0][: len(y)]
        v = slice(2 * FS + 4096, None)  # validation (chirp) part
        out.append(best_gain_esr(y[v], yc[v])[0])
    return float(np.mean(out))


class GRUModel(torch.nn.Module):
    def __init__(self, h=16):
        super().__init__()
        self.rnn = torch.nn.GRU(1, h, batch_first=True)
        self.lin = torch.nn.Linear(h, 1)

    def forward(self, x):
        y, _ = self.rnn(x[..., None])
        return self.lin(y)[..., 0] + x  # residual (skip) connection, common in amp-modelling RNNs


def gru_baseline(xs, ys, steps, seed=0):
    torch.manual_seed(seed)
    rng = np.random.default_rng(seed)
    m = GRUModel().double()
    opt = torch.optim.Adam(m.parameters(), lr=3e-3)
    seg, t0 = 4096, time.time()
    for s in range(steps):
        bx, by = [], []
        for _ in range(8):
            k = rng.integers(len(xs))
            st = rng.integers(0, 2 * FS - seg)
            bx.append(xs[k][st: st + seg]); by.append(ys[k][st: st + seg])
        X, Y = torch.tensor(np.array(bx)), torch.tensor(np.array(by))
        opt.zero_grad()
        P = m(X)
        loss = torch.sum((P[:, 512:] - Y[:, 512:]) ** 2) / torch.sum(Y[:, 512:] ** 2)
        loss.backward(); opt.step()
        if s % max(1, steps // 5) == 0:
            print(f"  gru step {s} esr {loss.item():.4f}")
    train_s = time.time() - t0
    res = []
    with torch.no_grad():
        for x, y in zip(xs, ys):
            p = m(torch.tensor(x)[None]).numpy()[0]
            v = slice(2 * FS + 4096, None)
            res.append(esr(y[v], p[v]))
    h = 16
    macs = 3 * (h * 1 + h * h) + h  # per sample, approx.
    return dict(esr_validation=float(np.mean(res)), train_seconds=train_s, approx_macs_per_sample=macs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", default=os.path.join(ROOT, "audio", "reference", "spice_feedback_clipper"))
    ap.add_argument("--value", type=float, default=0.5)
    ap.add_argument("--gru-steps", type=int, default=400)
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    a = ap.parse_args()
    xs, ys = load(a.dataset, a.value)
    rep = dict(data="SPICE simulation (feedback clipper), synthetic guitar-like train / chirp validation",
               knob_value=a.value)
    rep["overdrive_handdesigned_esr_validation"] = overdrive_baseline(xs, ys, a.build, a.value * 100)
    print("overdrive (hand-designed, gain matched) ESR:", rep["overdrive_handdesigned_esr_validation"])
    rep["gru_blackbox"] = gru_baseline(xs, ys, a.gru_steps)
    print("GRU:", rep["gru_blackbox"])
    summ = os.path.join(ROOT, "research", "ddsp", "results", "ddsp_spice_feedback_clipper_summary.json")
    if os.path.exists(summ):
        for r in json.load(open(summ)):
            if abs(r["param_value"] - a.value) < 1e-9:
                rep["ddsp"] = dict(esr_validation_cpp=r["validation_chirp"]["esr_cpp_engine"], train_seconds=r["train_seconds"],
                                   note="7 physical parameters + mix/level; runs as plain DSP in C++ (no network)")
    os.makedirs(os.path.join(ROOT, "research", "graybox", "results"), exist_ok=True)
    json.dump(rep, open(os.path.join(ROOT, "research", "graybox", "results", "baselines.json"), "w"), indent=2)
    print(json.dumps(rep, indent=2))


if __name__ == "__main__":
    main()
