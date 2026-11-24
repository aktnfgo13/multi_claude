#!/usr/bin/env python3
"""
Test-signal generator. Writes 32-bit float WAV + a JSON sidecar describing the signal.

  generate.py sine --freq 1000 --level -12 --seconds 2 -o sine.wav
  generate.py multitone --freqs 100,1000,5000 --level -18
  generate.py logsweep --f1 20 --f2 20000 --seconds 5   (Farina; inverse filter saved as *_inverse.wav)
  generate.py linsweep --f1 20 --f2 20000
  generate.py white | pink --level -20
  generate.py impulse | step | burst --freq 1000 --cycles 10
  generate.py ampsweep --freq 1000 --from -60 --to 0 --step 3 --dwell 0.25
  generate.py imd --f1 60 --f2 7000 --ratio 4
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "audio_analysis"))
import mfx_analysis as A  # noqa: E402
from scipy.io import wavfile  # noqa: E402


def write(path, fs, x, meta):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    wavfile.write(path, int(fs), np.asarray(x, dtype=np.float32))
    meta.update(sample_rate=fs, samples=len(x), peak_dbfs=float(A.db(A.peak(x))), rms_dbfs=float(A.db(A.rms(x))),
                data_kind="synthetic_test_signal")
    json.dump(meta, open(os.path.splitext(path)[0] + ".json", "w"), indent=2)
    print("wrote", path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=["sine", "multitone", "logsweep", "linsweep", "white", "pink", "impulse", "step", "burst", "ampsweep", "imd"])
    ap.add_argument("-o", "--out")
    ap.add_argument("--fs", type=float, default=48000)
    ap.add_argument("--seconds", type=float, default=2.0)
    ap.add_argument("--level", type=float, default=-12.0, help="peak level dBFS")
    ap.add_argument("--freq", type=float, default=1000)
    ap.add_argument("--freqs", default="100,1000,5000")
    ap.add_argument("--f1", type=float, default=20)
    ap.add_argument("--f2", type=float, default=20000)
    ap.add_argument("--cycles", type=int, default=10)
    ap.add_argument("--from", dest="lfrom", type=float, default=-60)
    ap.add_argument("--to", type=float, default=0)
    ap.add_argument("--step", type=float, default=3)
    ap.add_argument("--dwell", type=float, default=0.25)
    ap.add_argument("--ratio", type=float, default=4.0)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    fs, n = a.fs, int(a.fs * a.seconds)
    amp = 10 ** (a.level / 20)
    t = np.arange(n) / fs
    rng = np.random.default_rng(a.seed)
    meta = dict(kind=a.kind)
    if a.kind == "sine":
        x = amp * np.sin(2 * np.pi * a.freq * t); meta.update(freq=a.freq)
    elif a.kind == "multitone":
        fr = [float(v) for v in a.freqs.split(",")]
        x = sum(np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi)) for f in fr)
        x = amp * x / np.max(np.abs(x)); meta.update(freqs=fr)
    elif a.kind == "logsweep":
        x, inv = A.farina_sweep(fs, a.f1, a.f2, a.seconds, amp)
        meta.update(f1=a.f1, f2=a.f2, method="Farina exponential sweep")
        out = a.out or f"logsweep_{a.f1:g}_{a.f2:g}.wav"
        write(os.path.splitext(out)[0] + "_inverse.wav", fs, inv / np.max(np.abs(inv)), dict(kind="inverse_filter"))
    elif a.kind == "linsweep":
        x = amp * np.sin(2 * np.pi * (a.f1 * t + (a.f2 - a.f1) / (2 * a.seconds) * t * t)); meta.update(f1=a.f1, f2=a.f2)
    elif a.kind == "white":
        x = rng.uniform(-1, 1, n); x = amp * x / np.max(np.abs(x))
    elif a.kind == "pink":
        X = np.fft.rfft(rng.standard_normal(n))
        f = np.fft.rfftfreq(n, 1 / fs); f[0] = f[1]
        x = np.fft.irfft(X / np.sqrt(f), n); x = amp * x / np.max(np.abs(x))
    elif a.kind == "impulse":
        x = np.zeros(n); x[0] = amp
    elif a.kind == "step":
        x = np.zeros(n); x[n // 10:] = amp
    elif a.kind == "burst":
        m = int(fs / a.freq * a.cycles)
        x = np.zeros(n); x[n // 10: n // 10 + m] = amp * np.sin(2 * np.pi * a.freq * np.arange(m) / fs)
        meta.update(freq=a.freq, cycles=a.cycles)
    elif a.kind == "ampsweep":
        levels = np.arange(a.lfrom, a.to + 1e-9, a.step)
        d = int(fs * a.dwell)
        tt = np.arange(d) / fs
        x = np.concatenate([10 ** (l / 20) * np.sin(2 * np.pi * a.freq * tt) for l in levels])
        meta.update(freq=a.freq, levels_dbfs=levels.tolist(), dwell_s=a.dwell)
    elif a.kind == "imd":
        x = np.sin(2 * np.pi * a.f1 * t) + np.sin(2 * np.pi * a.f2 * t) / a.ratio
        x = amp * x / np.max(np.abs(x)); meta.update(f1=a.f1, f2=a.f2, ratio=a.ratio, standard="SMPTE-like 4:1")
    write(a.out or f"{a.kind}.wav", fs, x, meta)


if __name__ == "__main__":
    main()
