#!/usr/bin/env python3
"""
DDSP training of the C++ `ddsp_drive` structure against reference data (SPICE or measured).

Model (identical to src/dsp/drive/Drives.cpp DdspDrive, oversampling=1 -> 2x halfband, same taps):
    x -> HPF1(hpf_hz) -> [2x up] -> asym algebraic sigmoid(gain, thr+, thr-, knee) -> [2x down]
      -> LPF1(lpf_hz) -> *out ; y = level * ((1-mix) * x_delayed + mix * wet)

Training uses a frequency-sampling approximation of the first-order IIR filters (FFT domain, zero
padded) for speed; the exported model is re-evaluated with the TRUE recursive IIR (numpy reference)
and with the C++ engine (mfx_render). Differences between the three are reported.

Loss: ESR + pre-emphasised ESR (1st-order HP pre-emphasis 0.95) + multi-resolution STFT.
Data split: guitar-like segment (first 2 s of each item) = train, chirp (last 1 s) = validation.

Usage:
  python3 research/ddsp/train_ddsp.py --dataset audio/reference/spice_feedback_clipper --param-value 0.5
  python3 research/ddsp/train_ddsp.py --dataset ... --all          # one model per knob value
"""
from __future__ import annotations

import argparse
import json
import math
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

torch.set_num_threads(max(1, os.cpu_count() or 1))
FS = 48000
LAT = M.LATENCY[1]
H = torch.tensor(M.halfband_kaiser(*M.STAGES[0]))


# ----------------------------------------------------------------------------- differentiable pieces
def fft_conv(x, k):
    n = x.shape[-1] + k.shape[-1] - 1
    N = 1 << (n - 1).bit_length()
    y = torch.fft.irfft(torch.fft.rfft(x, N) * torch.fft.rfft(k, N), N)
    return y[..., : x.shape[-1]]


def first_order(x, fc, kind, fs):
    """Bilinear 1st-order LP/HP applied by frequency sampling (approximation of the causal IIR)."""
    n = x.shape[-1]
    N = 1 << (2 * n - 1).bit_length()
    w = torch.linspace(0, math.pi, N // 2 + 1, dtype=x.dtype)
    z1 = torch.exp(-1j * w)
    K = torch.tan(math.pi * fc / fs)
    if kind == "lp":
        Hz = (K + K * z1) / ((1 + K) + (K - 1) * z1)
    else:
        Hz = (1 - z1) / ((1 + K) + (K - 1) * z1)
    return torch.fft.irfft(torch.fft.rfft(x, N) * Hz, N)[..., :n]


def up2(x):
    u = torch.zeros(*x.shape[:-1], 2 * x.shape[-1], dtype=x.dtype)
    u[..., 0::2] = x
    return fft_conv(u, 2.0 * H)


def down2(x):
    return fft_conv(x, H)[..., 0::2]


class DdspDrive(torch.nn.Module):
    # unconstrained parameters -> physical values (log-domain where positive)
    def __init__(self):
        super().__init__()
        self.log_hpf = torch.nn.Parameter(torch.tensor(math.log(40.0)))
        self.gain_db = torch.nn.Parameter(torch.tensor(20.0))
        self.log_tp = torch.nn.Parameter(torch.tensor(math.log(0.3)))
        self.log_tn = torch.nn.Parameter(torch.tensor(math.log(0.3)))
        self.log_knee = torch.nn.Parameter(torch.tensor(math.log(2.5)))
        self.log_lpf = torch.nn.Parameter(torch.tensor(math.log(6000.0)))
        self.out_db = torch.nn.Parameter(torch.tensor(0.0))
        self.mix_logit = torch.nn.Parameter(torch.tensor(0.0))
        self.level_db = torch.nn.Parameter(torch.tensor(0.0))

    def physical(self):
        return dict(
            hpf_hz=float(torch.exp(self.log_hpf.detach()).clamp(5, 2000)), gain_db=float(self.gain_db.detach().clamp(-12, 60)),
            thr_pos=float(torch.exp(self.log_tp.detach()).clamp(0.01, 4)), thr_neg=float(torch.exp(self.log_tn.detach()).clamp(0.01, 4)),
            knee=float(torch.exp(self.log_knee.detach()).clamp(1.0, 16)), lpf_hz=float(torch.exp(self.log_lpf.detach()).clamp(300, 22000)),
            out_db=float(self.out_db.detach().clamp(-40, 40)), mix=100.0 * float(torch.sigmoid(self.mix_logit.detach())),
            level=float(self.level_db.detach().clamp(-40, 40)), oversampling=1)

    def forward(self, x):
        hp = first_order(x, torch.exp(self.log_hpf).clamp(5, 2000), "hp", FS)
        u = up2(hp) * 10 ** (self.gain_db.clamp(-12, 60) / 20)
        tp, tn = torch.exp(self.log_tp).clamp(0.01, 4), torch.exp(self.log_tn).clamp(0.01, 4)
        k = torch.exp(self.log_knee).clamp(1.0, 16)  # k >= 1 keeps |u|^k differentiable at 0
        sc = torch.where(u >= 0, tp, tn)
        y = u / (1 + (u.abs() / sc) ** k) ** (1 / k)
        wet = first_order(down2(y), torch.exp(self.log_lpf).clamp(300, 22000), "lp", FS) * 10 ** (self.out_db / 20)
        dry = torch.nn.functional.pad(x, (LAT, 0))[..., : x.shape[-1]]
        m = torch.sigmoid(self.mix_logit)
        return 10 ** (self.level_db / 20) * ((1 - m) * dry + m * wet)


# ----------------------------------------------------------------------------- losses
def esr(t, y):
    return torch.sum((t - y) ** 2) / (torch.sum(t ** 2) + 1e-12)


def pre_emph(x):
    return x[..., 1:] - 0.95 * x[..., :-1]


def mrstft(t, y):
    loss = 0.0
    for n in (256, 1024, 4096):
        win = torch.hann_window(n, dtype=t.dtype)
        T = torch.stft(t, n, n // 4, window=win, return_complex=True).abs()
        Y = torch.stft(y, n, n // 4, window=win, return_complex=True).abs()
        loss = loss + torch.norm(T - Y) / (torch.norm(T) + 1e-9) + torch.mean(torch.abs(torch.log(T + 1e-7) - torch.log(Y + 1e-7)))
    return loss / 3


def total_loss(t, y):
    return esr(t, y) + esr(pre_emph(t), pre_emph(y)) + 0.1 * mrstft(t, y)


# ----------------------------------------------------------------------------- data
def load_items(dataset, param_value):
    idx = json.load(open(os.path.join(dataset, "index.json")))
    xs, ys = [], []
    for it in idx["items"]:
        pv = next(iter(it["params"].values()), None) if it["params"] else None
        if param_value is not None and (pv is None or abs(pv - param_value) > 1e-9):
            continue
        _, x = M.read_wav(os.path.join(dataset, it["input"]))
        _, y = M.read_wav(os.path.join(dataset, it["output"]))
        xs.append(x); ys.append(y)
    if not xs:
        raise SystemExit("no items for that parameter value")
    return xs, ys, idx["meta"]


def split(xs, ys):
    tr = [(x[: 2 * FS], y[: 2 * FS]) for x, y in zip(xs, ys)]
    va = [(x[2 * FS:], y[2 * FS:]) for x, y in zip(xs, ys)]
    return tr, va


def shift_target(y):
    """model output is delayed by LAT (oversampler); compare model[n+LAT] with target[n]."""
    return np.concatenate([np.zeros(LAT), y])[: len(y)]


# ----------------------------------------------------------------------------- evaluation
def eval_numpy(prm, x):
    p = M.DdspDriveParams(**{k: prm[k] for k in ["hpf_hz", "gain_db", "thr_pos", "thr_neg", "knee", "lpf_hz", "out_db", "oversampling"]})
    wet, dry = M.DdspDriveRef(FS, p).process(x)
    m = prm["mix"] / 100.0
    return 10 ** (prm["level"] / 20) * ((1 - m) * dry + m * wet)


def eval_cpp(prm, x, build):
    with tempfile.TemporaryDirectory() as d:
        i, o = os.path.join(d, "i.wav"), os.path.join(d, "o.wav")
        M.write_wav(i, FS, x)
        cmd = [os.path.join(build, "mfx_render"), "-i", i, "-o", o, "--effect", "ddsp_drive"]
        for k, v in prm.items():
            cmd += ["--set", f"{k}={v}"]
        subprocess.run(cmd, check=True, capture_output=True)
        _, y = M.read_wav(o)
        return y[:, 0]


def esr_np(t, y):
    return float(np.sum((t - y) ** 2) / np.sum(t ** 2))


def train_one(dataset, pv, steps, build, seed=0):
    torch.manual_seed(seed)
    xs, ys, meta = load_items(dataset, pv)
    tr, va = split(xs, ys)
    model = DdspDrive().double()
    opt = torch.optim.Adam(model.parameters(), lr=0.02)
    best, best_state = float("inf"), None
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, steps)
    seg = 16384
    rng = np.random.default_rng(seed)
    t0 = time.time()
    for step in range(steps):
        bx, by = [], []
        for _ in range(4):
            x, y = tr[rng.integers(len(tr))]
            s = rng.integers(0, len(x) - seg)
            bx.append(x[s: s + seg]); by.append(shift_target(y)[s: s + seg])
        X, Y = torch.tensor(np.array(bx)), torch.tensor(np.array(by))
        opt.zero_grad()
        out = model(X)
        loss = total_loss(Y[:, 2048:], out[:, 2048:])  # skip filter warm-up
        if not torch.isfinite(loss):
            if best_state is not None:
                model.load_state_dict(best_state)
            print(f"  step {step}: non-finite loss, restored best state")
            sched.step()
            continue
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step(); sched.step()
        if loss.item() < best:
            best, best_state = loss.item(), {k: v.detach().clone() for k, v in model.state_dict().items()}
        if step % max(1, steps // 8) == 0:
            print(f"  step {step:4d} loss {loss.item():.5f}")
    if best_state is not None:  # best training-batch state (noisy but avoids late divergence)
        model.load_state_dict(best_state)
    prm = model.physical()
    res = dict(param_value=pv, params=prm, train_seconds=time.time() - t0, data=meta)
    # evaluation on validation (chirp) and train segments with the TRUE IIR + C++
    for name, part in [("validation_chirp", va), ("train_guitar_like", tr)]:
        e_fs, e_np, e_cpp = [], [], []
        for x, y in part:
            yt = shift_target(y)
            with torch.no_grad():
                e_fs.append(esr_np(yt[4096:], model(torch.tensor(x)[None]).numpy()[0][4096:]))
            yn = eval_numpy(prm, x)
            e_np.append(esr_np(yt[4096:], yn[4096:]))
            if build:
                yc = eval_cpp(prm, x, build)
                e_cpp.append(esr_np(yt[4096:], yc[4096: len(yt)]))
        res[name] = dict(esr_freq_sampling_model=float(np.mean(e_fs)), esr_true_iir_python=float(np.mean(e_np)),
                         esr_cpp_engine=float(np.mean(e_cpp)) if e_cpp else None)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--param-value", type=float)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--steps", type=int, default=300)
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--out", default=os.path.join(ROOT, "research", "ddsp", "results"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    idx = json.load(open(os.path.join(a.dataset, "index.json")))
    values = sorted({next(iter(i["params"].values())) for i in idx["items"] if i["params"]}) if a.all else [a.param_value]
    results = []
    for pv in values:
        print(f"training value={pv}")
        r = train_one(a.dataset, pv, a.steps, a.build if os.path.exists(os.path.join(a.build, "mfx_render")) else None)
        print(json.dumps({k: r[k] for k in ["param_value", "validation_chirp", "train_guitar_like"]}, indent=1))
        results.append(r)
        # C++ compatible export: a preset with one ddsp_drive block
        preset = {"format": "mfx-preset", "version": 1, "name": f"DDSP {os.path.basename(a.dataset)} {pv}",
                  "chain": [{"type": "ddsp_drive", "lane": "pre", "params": r["params"]}], "midi": []}
        json.dump(preset, open(os.path.join(a.out, f"ddsp_{os.path.basename(a.dataset)}_{pv:g}.preset.json"), "w"), indent=2)
    json.dump(results, open(os.path.join(a.out, f"ddsp_{os.path.basename(a.dataset)}_summary.json"), "w"), indent=2)


if __name__ == "__main__":
    main()
