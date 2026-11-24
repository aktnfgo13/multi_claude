#!/usr/bin/env python3
"""
Analyse one file, or compare a processed/estimated file against a reference.

  analyze.py report out.wav [--f0 1000] [--json r.json] [--png r.png] [--title "..."]
  analyze.py compare ref.wav est.wav [--align] [--loudness-match] [--json c.json] [--png c.png]

PNG figures always carry the measurement conditions (file, sample rate, f0, units).
"""
import argparse
import json
import os
import sys

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mfx_analysis as A  # noqa: E402


def load(p):
    fs, x = wavfile.read(p)
    x = x.astype(np.float64)
    if x.dtype.kind == "i":
        x /= np.iinfo(np.int16).max
    if x.ndim > 1:
        x = x[:, 0]
    return fs, x


def report(a):
    fs, x = load(a.file)
    r = dict(file=a.file, sample_rate=fs, seconds=len(x) / fs, peak_dbfs=float(A.db(A.peak(x))), rms_dbfs=float(A.db(A.rms(x))),
             crest_factor_db=A.crest_factor_db(x), dc_offset=A.dc_offset(x), noise_floor_db_per_bin=A.noise_floor_db(x, fs))
    if a.f0:
        seg = x[len(x) // 2:]
        r["harmonics"] = A.harmonics(seg, fs, a.f0)
    if a.json:
        json.dump(r, open(a.json, "w"), indent=2)
    print(json.dumps(r, indent=2))
    if a.png:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(3, 1, figsize=(10, 10))
        t = np.arange(len(x)) / fs
        ax[0].plot(t, x, lw=0.5); ax[0].set_xlabel("time [s]"); ax[0].set_ylabel("amplitude [FS]")
        ax[0].set_title(f"{a.title or os.path.basename(a.file)} — waveform ({fs} Hz)")
        f, m = A.spectrum(x[len(x) // 2:], fs)
        ax[1].semilogx(f[1:], A.db(m[1:]), lw=0.6); ax[1].set_xlabel("frequency [Hz]"); ax[1].set_ylabel("level [dBFS, sine peak]")
        ax[1].set_ylim(-160, 6); ax[1].grid(True, which="both", alpha=0.3)
        ax[1].set_title("spectrum (Blackman-Harris, 2nd half of file)" + (f", f0 = {a.f0} Hz, THD = {100 * r['harmonics']['thd']:.2f} %" if a.f0 else ""))
        ax[2].specgram(x, NFFT=2048, Fs=fs, noverlap=1536, cmap="magma"); ax[2].set_yscale("symlog", linthresh=500)
        ax[2].set_xlabel("time [s]"); ax[2].set_ylabel("frequency [Hz]"); ax[2].set_title("spectrogram (2048 / 75 % overlap)")
        fig.tight_layout(); fig.savefig(a.png, dpi=110)
        print("wrote", a.png)


def compare(a):
    fs, ref = load(a.ref)
    fs2, est = load(a.est)
    assert fs == fs2, "sample rates differ"
    lag = 0
    if a.align:
        lag = A.align(ref, est)
        est = est[lag:] if lag > 0 else np.concatenate([np.zeros(-lag), est])
    n = min(len(ref), len(est))
    ref, est = ref[:n], est[:n]
    if a.loudness_match:
        est = A.loudness_match(ref, est)
    r = dict(reference=a.ref, estimate=a.est, sample_rate=fs, lag_samples=lag, loudness_matched=bool(a.loudness_match),
             esr=A.esr(ref, est), esr_pre_emphasis=A.esr(ref, est, 0.95), mrstft=A.mrstft(ref, est),
             envelope_error_db=A.envelope_error_db(ref, est, fs), level_difference_db=float(A.db(A.rms(est)) - A.db(A.rms(ref))))
    r["esr_db"] = float(10 * np.log10(r["esr"] + 1e-30))
    if a.json:
        json.dump(r, open(a.json, "w"), indent=2)
    print(json.dumps(r, indent=2))
    if a.png:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(2, 1, figsize=(10, 7))
        k = slice(n // 2, n // 2 + int(0.02 * fs))
        t = np.arange(n)[k] / fs * 1000
        ax[0].plot(t, ref[k], label="reference"); ax[0].plot(t, est[k], "--", label="estimate")
        ax[0].set_xlabel("time [ms]"); ax[0].set_ylabel("amplitude [FS]"); ax[0].legend()
        ax[0].set_title(f"20 ms excerpt — ESR {r['esr_db']:.1f} dB, lag {lag} samples, fs {fs} Hz")
        f, mr = A.spectrum(ref, fs); _, me = A.spectrum(est, fs)
        ax[1].semilogx(f[1:], A.db(mr[1:]), lw=0.6, label="reference"); ax[1].semilogx(f[1:], A.db(me[1:]), lw=0.6, label="estimate")
        ax[1].set_xlabel("frequency [Hz]"); ax[1].set_ylabel("level [dBFS]"); ax[1].legend(); ax[1].grid(True, which="both", alpha=0.3)
        fig.tight_layout(); fig.savefig(a.png, dpi=110)
        print("wrote", a.png)


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    r = sp.add_parser("report"); r.add_argument("file"); r.add_argument("--f0", type=float)
    r.add_argument("--json"); r.add_argument("--png"); r.add_argument("--title")
    c = sp.add_parser("compare"); c.add_argument("ref"); c.add_argument("est"); c.add_argument("--align", action="store_true")
    c.add_argument("--loudness-match", action="store_true"); c.add_argument("--json"); c.add_argument("--png")
    a = ap.parse_args()
    report(a) if a.cmd == "report" else compare(a)


if __name__ == "__main__":
    main()
