#!/usr/bin/env python3
"""
Knob-mapping method comparison (integration review §12): how should per-position model parameters be
interpolated between measured/simulated knob positions?

Methods (all in the stable parameter domain: log for cut-offs/thresholds/knee, dB/percent otherwise):
  linear     : straight line through the two end points only (2 measurements)
  piecewise  : piecewise-linear through the training positions (current KnobLut in C++)
  pchip      : monotone cubic (shape preserving) spline through the training positions
  lowdim     : 2-parameter curve per parameter, p(k) = a + b * log(1 + c0 * k), c0 fixed = 20
  mlp        : tiny MLP (1-8-8-P, tanh), trained on the training positions (deliberately included to
               show over/under-fitting with very few positions)

Validation: knob positions that are NOT in the training set (held out). Error is reported in the
parameter domain and as audio ESR of the resulting model against the SPICE target (numpy reference
of ddsp_drive, i.e. the same structure as the C++ effect; parity with C++ is checked elsewhere).

Data: DDSP fits to a SPICE feedback-clipper DRIVE sweep (simulation, not a hardware measurement).
With only 5 positions the conclusions are limited to "which method is safe with sparse data".
"""
import argparse
import json
import os
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "research", "common"))
sys.path.insert(0, os.path.join(ROOT, "research", "ddsp"))
import mfx_dsp as M  # noqa: E402

PARAMS = [("hpf_hz", "log"), ("gain_db", "lin"), ("thr_pos", "log"), ("thr_neg", "log"), ("knee", "log"),
          ("lpf_hz", "log"), ("out_db", "lin"), ("mix", "lin"), ("level", "lin")]
FS = 48000


def to_dom(v, d):
    return np.log(v) if d == "log" else v


def from_dom(v, d):
    return np.exp(v) if d == "log" else v


def pchip(xk, yk, x):
    xk, yk = np.asarray(xk, float), np.asarray(yk, float)
    h = np.diff(xk); dlt = np.diff(yk) / h
    m = np.zeros_like(yk)
    for i in range(1, len(yk) - 1):
        if dlt[i - 1] * dlt[i] > 0:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / dlt[i - 1] + w2 / dlt[i])
    m[0], m[-1] = dlt[0], dlt[-1]
    i = np.clip(np.searchsorted(xk, x) - 1, 0, len(h) - 1)
    t = (x - xk[i]) / h[i]
    h00, h10, h01, h11 = 2 * t**3 - 3 * t**2 + 1, t**3 - 2 * t**2 + t, -2 * t**3 + 3 * t**2, t**3 - t**2
    return h00 * yk[i] + h10 * h[i] * m[i] + h01 * yk[i + 1] + h11 * h[i] * m[i + 1]


def lowdim(xk, yk, x, c0=20.0):
    A = np.stack([np.ones(len(xk)), np.log1p(c0 * np.asarray(xk))], 1)
    coef, *_ = np.linalg.lstsq(A, np.asarray(yk), rcond=None)
    return coef[0] + coef[1] * np.log1p(c0 * x)


def mlp(xk, Y, x, seed=0, steps=4000):
    import torch
    torch.manual_seed(seed)
    X = torch.tensor(np.asarray(xk)[:, None], dtype=torch.float64)
    mu, sd = Y.mean(0), Y.std(0) + 1e-9
    T = torch.tensor((Y - mu) / sd, dtype=torch.float64)
    net = torch.nn.Sequential(torch.nn.Linear(1, 8), torch.nn.Tanh(), torch.nn.Linear(8, 8), torch.nn.Tanh(),
                              torch.nn.Linear(8, Y.shape[1])).double()
    opt = torch.optim.Adam(net.parameters(), lr=0.01, weight_decay=1e-4)
    for _ in range(steps):
        opt.zero_grad(); loss = torch.mean((net(X) - T) ** 2); loss.backward(); opt.step()
    with torch.no_grad():
        return net(torch.tensor([[x]], dtype=torch.float64)).numpy()[0] * sd + mu


def predict(method, train, k):
    xk = [r["param_value"] for r in train]
    Y = np.array([[to_dom(r["params"][p], d) for p, d in PARAMS] for r in train])
    if method == "mlp":
        out = mlp(xk, Y, k)
    else:
        out = []
        for j in range(len(PARAMS)):
            yk = Y[:, j]
            if method == "linear":
                out.append(np.interp(k, [xk[0], xk[-1]], [yk[0], yk[-1]]))
            elif method == "piecewise":
                out.append(np.interp(k, xk, yk))
            elif method == "pchip":
                out.append(pchip(xk, yk, k))
            elif method == "lowdim":
                out.append(lowdim(xk, yk, k))
        out = np.array(out)
    prm = {p: float(from_dom(v, d)) for (p, d), v in zip(PARAMS, out)}
    prm["oversampling"] = 1
    return prm, out


def audio_esr(prm, dataset, pv):
    from train_ddsp import load_items, shift_target, eval_numpy, esr_np, align
    xs, ys, _ = load_items(dataset, pv)
    ys, _ = align(xs, ys)
    e = []
    for x, y in zip(xs, ys):
        yt = shift_target(y)
        e.append(esr_np(yt[4096:], eval_numpy(prm, x)[4096:]))
    return float(np.mean(e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--summary", default=os.path.join(ROOT, "research/ddsp/results/ddsp_spice_feedback_clipper_summary.json"))
    ap.add_argument("--dataset", default=os.path.join(ROOT, "audio/reference/spice_feedback_clipper"))
    ap.add_argument("--train-knobs", default="0,50,100")
    ap.add_argument("--out", default=os.path.join(ROOT, "research/parameter_mapping/results/mapping_method_comparison.json"))
    a = ap.parse_args()
    res = sorted(json.load(open(a.summary)), key=lambda r: r["param_value"])
    tk = {int(v) for v in a.train_knobs.split(",")}
    train = [r for r in res if round(r["param_value"] * 100) in tk]
    held = [r for r in res if round(r["param_value"] * 100) not in tk]
    report = dict(data="DDSP fits to SPICE feedback-clipper DRIVE sweep (simulation)", train_knobs=sorted(tk),
                  held_out=[round(r["param_value"] * 100) for r in held], methods={})
    for method in ["linear", "piecewise", "pchip", "lowdim", "mlp"]:
        rows = []
        for r in held:
            prm, dom = predict(method, train, r["param_value"])
            ref = np.array([to_dom(r["params"][p], d) for p, d in PARAMS])
            rows.append(dict(knob=round(r["param_value"] * 100),
                             param_domain_rmse=float(np.sqrt(np.mean((dom - ref) ** 2))),
                             esr_vs_spice=audio_esr(prm, a.dataset, r["param_value"]),
                             esr_per_position_fit=audio_esr({**r["params"], "oversampling": 1}, a.dataset, r["param_value"])))
        report["methods"][method] = rows
        print(method, [(x["knob"], round(x["esr_vs_spice"], 4), round(x["esr_per_position_fit"], 4)) for x in rows])
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    json.dump(report, open(a.out, "w"), indent=2)


if __name__ == "__main__":
    main()
