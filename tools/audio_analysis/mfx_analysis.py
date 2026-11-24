"""
Audio analysis library (non real-time). All level values are dBFS unless noted; frequencies in Hz.

Functions return plain floats / numpy arrays so they can be used from tests, benchmarks and notebooks.
"""
from __future__ import annotations

import numpy as np
from scipy import signal as sps


def db(x, floor=1e-12):
    return 20 * np.log10(np.maximum(np.abs(x), floor))


def peak(x):
    return float(np.max(np.abs(x)))


def rms(x):
    return float(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2)))


def crest_factor_db(x):
    r = rms(x)
    return float(20 * np.log10(peak(x) / r)) if r > 0 else 0.0


def dc_offset(x):
    return float(np.mean(x))


def spectrum(x, fs, window="blackmanharris"):
    w = sps.get_window(window, len(x))
    X = np.fft.rfft(x * w)
    f = np.fft.rfftfreq(len(x), 1 / fs)
    mag = np.abs(X) / (np.sum(w) / 2)  # sine peak amplitude calibrated
    return f, mag


def harmonics(x, fs, f0, n_harm=15, half_bw_bins=6):
    """THD, THD+N, harmonic levels (dB re fundamental), even/odd ratio, non-harmonic (aliasing+noise)."""
    f, mag = spectrum(x, fs)
    bin_hz = f[1]
    p = mag ** 2

    def band(fr):
        c = int(round(fr / bin_hz))
        lo, hi = max(0, c - half_bw_bins), min(len(p), c + half_bw_bins + 1)
        return float(np.sum(p[lo:hi]))

    e1 = band(f0)
    edc = band(0)
    hs = {}
    eh = even = odd = 0.0
    k = 2
    harm_total = e1 + edc
    while k * f0 < fs / 2 - half_bw_bins * bin_hz:
        e = band(k * f0)
        harm_total += e
        if k <= n_harm:
            hs[k] = float(10 * np.log10(e / e1 + 1e-30))
            eh += e
            if k % 2 == 0:
                even += e
            else:
                odd += e
        k += 1
    total = float(np.sum(p))
    return dict(f0=f0, fundamental_dbfs=float(10 * np.log10(e1 + 1e-30)), thd=float(np.sqrt(eh / e1)),
                thd_n=float(np.sqrt(max(total - e1 - edc, 0) / e1)), harmonics_db=hs,
                even_odd_db=float(10 * np.log10((even + 1e-30) / (odd + 1e-30))),
                non_harmonic_db=float(10 * np.log10(max(total - harm_total, 1e-30) / e1)))


def imd_smpte(x, fs, f1=60.0, f2=7000.0):
    """SMPTE-style IMD: sidebands f2 +/- k f1 (k=1..4) relative to f2."""
    f, mag = spectrum(x, fs)
    bin_hz = f[1]
    p = mag ** 2

    def band(fr):
        c = int(round(fr / bin_hz))
        return float(np.sum(p[max(0, c - 5): c + 6]))

    ref = band(f2)
    side = sum(band(f2 + k * f1) + band(f2 - k * f1) for k in range(1, 5))
    return float(np.sqrt(side / ref))


def frequency_response(ir, fs, n_fft=None):
    n = n_fft or int(2 ** np.ceil(np.log2(max(len(ir), 4096))))
    H = np.fft.rfft(ir, n)
    f = np.fft.rfftfreq(n, 1 / fs)
    return f, db(H), np.unwrap(np.angle(H))


def farina_sweep(fs, f1, f2, secs, amp=0.5):
    """Exponential sine sweep + inverse filter (Farina 2000)."""
    t = np.arange(int(fs * secs)) / fs
    R = np.log(f2 / f1)
    x = amp * np.sin(2 * np.pi * f1 * secs / R * (np.exp(t * R / secs) - 1))
    inv = x[::-1] * np.exp(-t * R / secs)
    inv /= np.abs(np.fft.rfft(np.convolve(x, inv)))[len(x) // 4]  # rough normalisation, refined below
    return x, inv


def deconvolve_sweep(y, x_sweep, inv):
    """Impulse response from a recorded sweep (linear part at the main peak, harmonics before it)."""
    h = sps.fftconvolve(y, inv)
    ref = sps.fftconvolve(x_sweep, inv)
    k = int(np.argmax(np.abs(ref)))
    scale = 1.0 / ref[k]
    return h * scale, k


def transfer_curve(x, y, n_bins=101):
    """Static input->output curve (binned average). Use with a slow amplitude ramp or a low sine."""
    edges = np.linspace(-np.max(np.abs(x)), np.max(np.abs(x)), n_bins + 1)
    idx = np.digitize(x, edges) - 1
    xs, ys = [], []
    for b in range(n_bins):
        m = idx == b
        if np.any(m):
            xs.append(float(np.mean(x[m]))); ys.append(float(np.mean(y[m])))
    return np.array(xs), np.array(ys)


def envelope(x, fs, attack_ms=1.0, release_ms=50.0):
    a = np.exp(-1 / (attack_ms * 1e-3 * fs))
    r = np.exp(-1 / (release_ms * 1e-3 * fs))
    e = np.zeros(len(x))
    v = 0.0
    for i, s in enumerate(np.abs(x)):
        v = s + (a if s > v else r) * (v - s)
        e[i] = v
    return e


def envelope_error_db(ref, est, fs):
    er, ee = envelope(ref, fs), envelope(est, fs)
    m = er > np.max(er) * 1e-3
    return float(np.sqrt(np.mean((db(er[m]) - db(ee[m])) ** 2)))


def gain_compression(levels_in_db, levels_out_db):
    """Returns small-signal gain and the input level at 1 dB compression."""
    g = np.asarray(levels_out_db) - np.asarray(levels_in_db)
    g0 = g[0]
    over = np.where(g0 - g > 1.0)[0]
    return float(g0), (float(levels_in_db[over[0]]) if len(over) else None)


def noise_floor_db(x, fs):
    f, mag = spectrum(x, fs, "hann")
    return float(10 * np.log10(np.median(mag ** 2) + 1e-30))


def esr(ref, est, pre_emphasis=None):
    r, e = np.asarray(ref, float), np.asarray(est, float)
    if pre_emphasis:
        r = r[1:] - pre_emphasis * r[:-1]
        e = e[1:] - pre_emphasis * e[:-1]
    return float(np.sum((r - e) ** 2) / np.sum(r ** 2))


def mrstft(ref, est, ffts=(256, 1024, 4096)):
    tot = 0.0
    for n in ffts:
        _, _, R = sps.stft(ref, nperseg=n, noverlap=3 * n // 4)
        _, _, E = sps.stft(est, nperseg=n, noverlap=3 * n // 4)
        R, E = np.abs(R), np.abs(E)
        tot += np.linalg.norm(R - E) / (np.linalg.norm(R) + 1e-12) + np.mean(np.abs(np.log(R + 1e-7) - np.log(E + 1e-7)))
    return float(tot / len(ffts))


def align(ref, est, max_lag=48000):
    """Integer lag that best aligns est to ref (cross-correlation)."""
    n = min(len(ref), len(est))
    c = sps.correlate(est[:n], ref[:n], mode="full", method="fft")
    mid = n - 1
    lo, hi = max(0, mid - max_lag), min(len(c), mid + max_lag + 1)
    return int(np.argmax(np.abs(c[lo:hi])) + lo - mid)


def loudness_match(ref, est):
    """Scale est to the RMS of ref (for level-matched comparisons / ABX)."""
    return est * (rms(ref) / max(rms(est), 1e-20))
