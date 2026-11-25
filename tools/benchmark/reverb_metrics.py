#!/usr/bin/env python3
"""Reverb impulse-response metrics (BENCHMARK_PROTOCOL §reverb): RT60 (Schroeder, T20 fit) broadband and
per octave band, normalised echo density (Abel & Huang 2006, 20 ms windows) over time, L/R correlation of
the late tail, spectral peakiness of the tail (max/median of the 1/24-octave smoothed spectrum, "metallic"
indicator), peak/finite checks. Renders with build/mfx_render (synthetic impulse, mix 100 %).

usage: reverb_metrics.py OUT.json "label:--effect TYPE --set k=v ..." [...]
"""
import json, math, os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "research", "common"))
import mfx_dsp as M

FS = 48000


def render(args, secs=12.0):
    d = tempfile.mkdtemp()
    x = np.zeros(int(FS * secs)); x[100] = 1.0
    inp, out = os.path.join(d, "i.wav"), os.path.join(d, "o.wav")
    M.write_wav(inp, FS, np.stack([x, x], 1))
    subprocess.run(["build/mfx_render", "-i", inp, "-o", out] + args.split(), check=True, capture_output=True)
    return M.read_wav(out)[1]


def rt60(y, f_lo=None, f_hi=None):
    if f_lo:
        n = 2 * len(y)  # zero-padded: no circular wrap of the band filter
        Y = np.fft.rfft(y, n); f = np.fft.rfftfreq(n, 1 / FS)
        Y[(f < f_lo) | (f > f_hi)] = 0; y = np.fft.irfft(Y, n)[: len(y)]
    e = np.cumsum((y ** 2)[::-1])[::-1]
    edc = 10 * np.log10(e / e[0] + 1e-30)
    i5, i25 = np.argmax(edc < -5), np.argmax(edc < -25)
    if i25 <= i5: return float("nan")
    t = np.arange(i5, i25) / FS
    slope = np.polyfit(t, edc[i5:i25], 1)[0]
    return float(-60.0 / slope)


def ned(y, t):
    n = int(0.02 * FS); c = int(t * FS)
    w = y[c - n // 2: c + n // 2]; s = np.std(w)
    return float(np.mean(np.abs(w) > s) / 0.3173)


def peakiness(y):
    seg = y[int(0.3 * FS): int(1.3 * FS)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2; f = np.fft.rfftfreq(len(seg), 1 / FS)
    m = (f > 200) & (f < 8000); S, f = S[m], f[m]
    # 1/24-octave smoothing in power
    sm = np.array([S[(f > fc * 2 ** (-1 / 48)) & (f < fc * 2 ** (1 / 48))].mean() for fc in f[::20]])
    raw = S[::20]
    return float(10 * np.log10(np.max(raw / sm)))  # strongest narrow peak above its local average


def main():
    out = {}
    for spec in sys.argv[2:]:
        label, args = spec.split(":", 1)
        y = render(args)
        l, r = y[:, 0], y[:, 1]
        tail = slice(int(0.3 * FS), int(1.3 * FS))
        out[label] = dict(
            finite=bool(np.all(np.isfinite(y))), peak=float(np.max(np.abs(y))),
            rt60=rt60(l), rt60_125=rt60(l, 88, 177), rt60_500=rt60(l, 354, 707), rt60_2k=rt60(l, 1414, 2828),
            rt60_8k=rt60(l, 5657, 11314), ned_50ms=ned(l, 0.05), ned_150ms=ned(l, 0.15), ned_400ms=ned(l, 0.4),
            lr_corr_tail=float(np.corrcoef(l[tail], r[tail])[0, 1]), tail_peakiness_db=peakiness(l))
        print(label, {k: round(v, 3) if isinstance(v, float) else v for k, v in out[label].items()})
    json.dump(dict(data_kind="synthetic impulse render", fs=FS, results=out), open(sys.argv[1], "w"), indent=2)


if __name__ == "__main__":
    main()
