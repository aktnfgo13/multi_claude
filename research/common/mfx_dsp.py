"""
Python reference implementation of the C++ DSP building blocks used for Python/C++ parity
and for research (DDSP export targets).

Mirrors exactly (same formulas, same structure, same order of operations):
  src/dsp/filters/Biquad.cpp            -> design_biquad
  src/dsp/oversampling/Oversampler.cpp  -> halfband_kaiser, Oversampler
  src/dsp/drive/CustomDrive.cpp         -> custom_drive_mapping, CustomDriveRef
  src/dsp/drive/Drives.cpp (DdspDrive)  -> DdspDriveRef
  src/core/Smoothed.h                   -> LinearSmoothed (float32 accumulation)

Linear filters run in float64 with float32-rounded coefficients (the C++ path runs float32),
so parity is "numerically equivalent", not bit exact. Tolerances are defined in tests/parity.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from scipy.signal import lfilter

F32 = np.float32


# ----------------------------------------------------------------------------- biquad design
def design_biquad(kind: str, fs: float, f0: float, Q: float = 0.7071, gain_db: float = 0.0):
    """Returns (b, a) float32-rounded coefficients, a[0] = 1. Same formulas as Biquad.cpp."""
    f0 = min(max(f0, 1.0), 0.499 * fs)
    Q = max(Q, 0.025)
    w0 = 2.0 * math.pi * f0 / fs
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2.0 * Q)
    A = 10.0 ** (gain_db / 40.0)
    if kind == "lp":
        b0, b1, b2, a0, a1, a2 = (1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha
    elif kind == "hp":
        b0, b1, b2, a0, a1, a2 = (1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha
    elif kind == "peak":
        b0, b1, b2, a0, a1, a2 = 1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A
    elif kind == "lp1":
        K = math.tan(math.pi * f0 / fs)
        b0, b1, b2, a0, a1, a2 = K, K, 0.0, 1 + K, K - 1, 0.0
    elif kind == "hp1":
        K = math.tan(math.pi * f0 / fs)
        b0, b1, b2, a0, a1, a2 = 1.0, -1.0, 0.0, 1 + K, K - 1, 0.0
    elif kind == "ls1":
        G = 10.0 ** (gain_db / 20.0)
        K = math.tan(math.pi * f0 / fs)
        b0, b1, b2, a0, a1, a2 = 1 + G * K, G * K - 1, 0.0, 1 + K, K - 1, 0.0
    elif kind == "hs1":
        G = 10.0 ** (gain_db / 20.0)
        K = math.tan(math.pi * f0 / fs)
        b0, b1, b2, a0, a1, a2 = G + K, K - G, 0.0, 1 + K, K - 1, 0.0
    else:
        raise ValueError(kind)
    b = np.array([b0 / a0, b1 / a0, b2 / a0], dtype=F32).astype(np.float64)
    a = np.array([1.0, a1 / a0, a2 / a0], dtype=F32).astype(np.float64)
    a[0] = 1.0
    return b, a


class Filter:
    """Stateful biquad (TDF-II via scipy lfilter, state carried between calls)."""

    def __init__(self):
        self.b = np.array([1.0, 0, 0])
        self.a = np.array([1.0, 0, 0])
        self.zi = np.zeros(2)

    def set(self, ba):
        self.b, self.a = ba

    def __call__(self, x):
        y, self.zi = lfilter(self.b, self.a, x, zi=self.zi)
        return y


# ----------------------------------------------------------------------------- oversampler
def _bessel_i0(x: float) -> float:
    s, t, q = 1.0, 1.0, x * x / 4.0
    for k in range(1, 64):
        t *= q / (k * k)
        s += t
        if t < 1e-17 * s:
            break
    return s


def halfband_kaiser(N: int, beta: float) -> np.ndarray:
    M = (N - 1) / 2.0
    i0b = _bessel_i0(beta)
    h = np.zeros(N)
    for n in range(N):
        t = n - M
        sinc = 0.5 if t == 0 else math.sin(math.pi * 0.5 * t) / (math.pi * t)
        r = (n - M) / M
        w = _bessel_i0(beta * math.sqrt(max(0.0, 1 - r * r))) / i0b
        h[n] = sinc * w
    return (h / h.sum()).astype(F32).astype(np.float64)


STAGES = [(63, 7.8), (33, 7.0), (17, 6.0)]
LATENCY = [0, 31, 39, 41]


class Oversampler:
    """Whole-signal (or streaming) implementation equivalent to the C++ polyphase stages."""

    def __init__(self, factor_index: int):
        self.idx = factor_index
        self.h = [halfband_kaiser(n, b) for n, b in STAGES[:factor_index]]
        self.up_zi = [np.zeros(len(h) - 1) for h in self.h]
        self.dn_zi = [np.zeros(len(h) - 1) for h in self.h]

    def up(self, x):
        for s, h in enumerate(self.h):
            u = np.zeros(2 * len(x))
            u[0::2] = x
            x, self.up_zi[s] = lfilter(2.0 * h, [1.0], u, zi=self.up_zi[s])
        return x

    def down(self, x):
        for s in reversed(range(len(self.h))):
            y, self.dn_zi[s] = lfilter(self.h[s], [1.0], x, zi=self.dn_zi[s])
            x = y[0::2]
        return x


# ----------------------------------------------------------------------------- smoothing
class LinearSmoothed:
    """float32 replica of core/Smoothed.h LinearSmoothed."""

    def __init__(self, fs, seconds, value):
        self.ramp = max(1, int(round(fs * seconds)))
        self.cur = F32(value)
        self.tgt = F32(value)
        self.step = F32(0)
        self.rem = 0

    def set_target(self, v):
        v = F32(v)
        if v == self.tgt:
            return
        self.tgt = v
        self.rem = self.ramp
        self.step = F32((self.tgt - self.cur) / F32(self.rem))

    def next(self):
        if self.rem > 0:
            self.cur = F32(self.cur + self.step)
            self.rem -= 1
            if self.rem == 0:
                self.cur = self.tgt
        return self.cur

    def skip(self, n):
        if self.rem <= 0:
            return self.cur
        if n >= self.rem:
            self.cur, self.rem = self.tgt, 0
        else:
            self.cur = F32(self.cur + self.step * F32(n))
            self.rem -= n
        return self.cur

    @property
    def smoothing(self):
        return self.rem > 0


# ----------------------------------------------------------------------------- Custom Drive
KNOBS = ["drive", "headroom", "tone", "tight", "crunch", "grain", "mid"]
DEFAULTS = dict(drive=25, headroom=50, tone=50, tight=50, crunch=50, grain=50, mid=50, mix=100, level=-6, oversampling=1)


PRE_CLIP_LP_HZ = 7200.0


def custom_drive_mapping(k: dict) -> dict:
    """Mapping v2 (mirror of src/dsp/drive/CustomDrive.cpp customDriveMapping)."""
    n = lambda v: min(max(float(v) / 100.0, 0.0), 1.0)
    d, h, t, ti = n(k["drive"]), n(k["headroom"]), n(k["tone"]), n(k["tight"])
    cr, g, m = n(k["crunch"]), n(k["grain"]), (n(k["mid"]) - 0.5) * 2.0
    p = {}
    p["gain_db"] = -6.0 + 36.0 * d
    vth_db = -18.0 + 18.0 * h
    p["vth"] = 10.0 ** (vth_db / 20.0)
    p["p"] = 2.0 + 4.0 * cr * cr
    p["kappa"] = (0.12 + 0.5 * h) * (1.4 - 1.3 * cr)
    p["beta"] = 0.6 * g
    p["vp"] = p["vn"] = p["vth"]
    p["emph_db"], p["emph_hz"] = 5.0 * g, 1500.0
    p["tight_hz"], p["tight_q"] = 30.0 * (900.0 / 30.0) ** ti, 0.6
    p["mid_hz"] = 700.0 * 2.0 ** (0.5 * m)
    p["mid_db"] = 10.0 * m
    p["mid_q"] = 0.9 + 0.5 * m if m >= 0 else 0.7
    p["mid_post_db"] = 3.0 * m
    p["tilt_db"], p["tilt_hz"] = 12.0 * (t - 0.5), 800.0
    p["fizz_hz"] = 2500.0 * (14000.0 / 2500.0) ** t
    p["c2"] = 1.0 + 0.3 * cr
    p["bias_depth"] = 0.1 + 0.2 * g
    p["makeup_db"] = -0.5 * vth_db - 0.15 * p["gain_db"]
    return p


def knee_weights(pk):
    pk = float(F32(min(max(pk, 2.0), 6.0)))
    if pk <= 4.0:
        w = (pk - 2.0) * 0.5
        return (1.0 - w, w, 0.0)
    w = (pk - 4.0) * 0.5
    return (0.0, 1.0 - w, w)


def knee_curve(z, w):
    z2 = z * z
    y = 0.0
    if w[0] != 0.0:
        y += w[0] * z / math.sqrt(1.0 + z2)
    if w[1] != 0.0:
        y += w[1] * z / math.sqrt(math.sqrt(1.0 + z2 * z2))
    if w[2] != 0.0:
        y += w[2] * z / math.pow(math.sqrt(1.0 + z2 * z2 * z2), 1.0 / 3.0)
    return y


def _alg_sigmoid(u, p):
    a = abs(u)
    if a < 1e-9:
        return u
    return u / (1.0 + a ** p) ** (1.0 / p)


class CustomDriveRef:
    CONTROL = 32

    def __init__(self, fs: float, params: dict | None = None):
        self.fs = fs
        self.params = dict(DEFAULTS)
        self.params.update(params or {})
        self.os_idx = int(self.params["oversampling"])
        self.knob = {k: LinearSmoothed(fs, 0.03, self.params[k]) for k in KNOBS}
        self.mix = LinearSmoothed(fs, 0.02, F32(self.params["mix"]) / F32(100))
        self.level = LinearSmoothed(fs, 0.02, F32(10.0 ** (self.params["level"] * 0.05)))
        self.f = {k: Filter() for k in ["dc", "tight", "mid", "prelp", "emph", "deemph", "midpost", "tlo", "thi", "fizz"]}
        self.os = Oversampler(self.os_idx)
        self.bias = 0.0
        self.dc_x1 = 0.0
        self.dc_y1 = 0.0
        self.dc_R = math.exp(-2.0 * math.pi * 5.0 / fs)
        self.dry = np.zeros(64)
        self._update()

    def _update(self):
        fs, fso = self.fs, self.fs * (1 << self.os_idx)
        p = custom_drive_mapping({k: float(self.knob[k].cur) for k in KNOBS})
        self.p = p
        f = self.f
        f["dc"].set(design_biquad("hp1", fs, 20.0))
        f["tight"].set(design_biquad("hp", fs, p["tight_hz"], p["tight_q"]))
        f["mid"].set(design_biquad("peak", fs, p["mid_hz"], p["mid_q"], p["mid_db"]))
        f["emph"].set(design_biquad("hs1", fso, p["emph_hz"], 0.7071, p["emph_db"]))
        f["prelp"].set(design_biquad("lp", fso, min(PRE_CLIP_LP_HZ, 0.45 * fso), 0.7071))
        f["deemph"].set(design_biquad("hs1", fso, p["emph_hz"], 0.7071, -p["emph_db"]))
        f["midpost"].set(design_biquad("peak", fs, p["mid_hz"], p["mid_q"], p["mid_post_db"]))
        f["tlo"].set(design_biquad("ls1", fs, p["tilt_hz"], 0.7071, -0.5 * p["tilt_db"]))
        f["thi"].set(design_biquad("hs1", fs, p["tilt_hz"], 0.7071, 0.5 * p["tilt_db"]))
        f["fizz"].set(design_biquad("lp", fs, p["fizz_hz"], 0.7071))
        self.g = float(F32(10.0 ** (p["gain_db"] / 20.0)))
        self.vth, self.vp, self.vn = float(F32(p["vth"])), float(F32(p["vp"])), float(F32(p["vn"]))
        self.pk, self.c2 = float(F32(p["p"])), float(F32(p["c2"]))
        self.depth = float(F32(p["bias_depth"]))
        self.kappa = float(F32(p["kappa"]))
        self.beta = float(F32(p["beta"]))
        self.w = knee_weights(self.pk)
        bz, k, w = self.beta, self.kappa, self.w
        b2 = bz * bz; b4 = b2 * b2; b6 = b4 * b2
        f0 = w[0] * bz / math.sqrt(1 + b2) + w[1] * bz / (1 + b4) ** 0.25 + w[2] * bz / (1 + b6) ** (1 / 6) + k * math.asinh(bz)
        df = w[0] * (1 + b2) ** -1.5 + w[1] * (1 + b4) ** -1.25 + w[2] * (1 + b6) ** (-7 / 6) + k / math.sqrt(1 + b2)
        self.f0 = float(F32(f0))
        self.slope_inv = float(F32(1.0 / max(df, 1e-3)))
        self.makeup = float(F32(10.0 ** (p["makeup_db"] / 20.0)))
        self.kc = 1.0 - math.exp(-1.0 / (0.002 * fso))
        self.kr = 1.0 - math.exp(-1.0 / (0.150 * fso))

    def set_param(self, name, value):
        self.params[name] = value
        if name in KNOBS:
            self.knob[name].set_target(value)
        elif name == "mix":
            self.mix.set_target(F32(value) / F32(100))
        elif name == "level":
            self.level.set_target(F32(10.0 ** (value * 0.05)))
        else:
            raise NotImplementedError(name)

    def _nonlinear(self, u):
        vth, vp, c2, depth = self.vth, self.vp, self.c2, self.depth
        kappa, beta, w, f0, si = self.kappa, self.beta, self.w, self.f0, self.slope_inv
        e = self.f["emph"](self.f["prelp"](u * self.g))
        y = np.empty_like(e)
        b = self.bias
        s = 2.0 * vp
        w2 = (1.0, 0.0, 0.0)
        for i, v in enumerate(e):
            v = v - b * vth
            z = v / vth + beta
            yy = vth * (knee_curve(z, w) + kappa * math.asinh(z) - f0) * si
            over = min(max(0.0, v / vp - 1.0), 2.0)
            tgt = depth * over
            b += (self.kc if tgt > b else self.kr) * (tgt - b)
            y[i] = s * knee_curve(c2 * yy / s, w2) / c2
        self.bias = b if abs(b) >= 1e-15 else 0.0
        return self.f["deemph"](y)

    def process(self, x: np.ndarray, events: list | None = None) -> np.ndarray:
        """events: list of (sample_index, name, value); sample_index must be a multiple of 32."""
        events = sorted(events or [], key=lambda e: e[0])
        out = np.zeros(len(x))
        lat = LATENCY[self.os_idx]
        ev = 0
        for start in range(0, len(x), self.CONTROL):
            while ev < len(events) and events[ev][0] <= start:
                self.set_param(events[ev][1], events[ev][2])
                ev += 1
            L = min(self.CONTROL, len(x) - start)
            moving = False
            for k in KNOBS:
                if self.knob[k].smoothing:
                    self.knob[k].skip(L)
                    moving = True
            if moving:
                self._update()
            mixr = np.array([float(self.mix.next()) for _ in range(L)])
            lvlr = np.array([float(self.level.next()) for _ in range(L)])
            xs = x[start:start + L].astype(np.float64)
            t = self.f["mid"](self.f["tight"](self.f["dc"](xs)))
            u = self.os.up(t)
            v = self.os.down(self._nonlinear(u))
            # DC blocker (y = x - x1 + R*y1)
            dcout = np.empty(L)
            x1, y1, R = self.dc_x1, self.dc_y1, self.dc_R
            for i in range(L):
                yv = v[i] - x1 + R * y1
                x1, y1 = v[i], yv
                dcout[i] = yv
            self.dc_x1, self.dc_y1 = x1, y1
            w = self.f["fizz"](self.f["thi"](self.f["tlo"](self.f["midpost"](dcout)))) * self.makeup
            buf = np.concatenate([self.dry, xs])
            d = buf[len(self.dry) - lat: len(self.dry) - lat + L] if lat > 0 else xs
            self.dry = buf[-64:]
            out[start:start + L] = (d + mixr * (w - d)) * lvlr
        return out


# ----------------------------------------------------------------------------- DDSP Drive
@dataclass
class DdspDriveParams:
    hpf_hz: float = 20.0
    gain_db: float = 12.0
    thr_pos: float = 0.5
    thr_neg: float = 0.5
    knee: float = 2.0
    lpf_hz: float = 8000.0
    out_db: float = 0.0
    oversampling: int = 2

    def as_dict(self):
        return dict(self.__dict__)


class DdspDriveRef:
    """HPF1 -> gain -> [OS] asym algebraic sigmoid [OS] -> LPF1 -> out gain (C++ DdspDrive)."""

    def __init__(self, fs: float, prm: DdspDriveParams):
        self.fs, self.prm = fs, prm

    def process(self, x):
        p, fs = self.prm, self.fs
        hp = Filter(); hp.set(design_biquad("hp1", fs, p.hpf_hz))
        lp = Filter(); lp.set(design_biquad("lp1", fs, p.lpf_hz))
        os_ = Oversampler(int(p.oversampling))
        g = float(F32(10 ** (p.gain_db / 20)))
        vp, vn, k = float(F32(p.thr_pos)), float(F32(p.thr_neg)), float(F32(p.knee))
        u = os_.up(hp(np.asarray(x, dtype=np.float64)))
        gu = g * u
        a = np.abs(gu)
        sc = np.where(gu >= 0, vp, vn)
        y = sc * (gu / sc) / (1.0 + (a / sc) ** k) ** (1.0 / k)
        v = os_.down(y)
        lat = LATENCY[int(p.oversampling)]
        out = lp(v) * float(F32(10 ** (p.out_db / 20)))
        dry = np.concatenate([np.zeros(lat), np.asarray(x, dtype=np.float64)])[: len(x)]
        return out, dry  # mix = 100 % -> out


# ----------------------------------------------------------------------------- WAV helpers
def read_wav(path):
    from scipy.io import wavfile
    sr, d = wavfile.read(path)
    if d.dtype == np.int16:
        d = d / 32768.0
    elif d.dtype == np.int32:
        d = d / 2147483648.0
    return sr, d.astype(np.float64)


def write_wav(path, sr, x):
    from scipy.io import wavfile
    wavfile.write(path, int(sr), np.asarray(x, dtype=np.float32))
