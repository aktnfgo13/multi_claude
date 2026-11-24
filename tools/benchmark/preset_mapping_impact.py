#!/usr/bin/env python3
"""Measure how a Custom Drive mapping change affects factory presets: render only the custom_drive
block of each preset with an old and a new mfx_render on a synthetic guitar-like DI (peak -7.2 dBFS)
and report output RMS/peak and level-matched residual. Synthetic signal, not a real DI recording."""
import json, os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "research", "common"))
import mfx_dsp as M

old_exe, new_exe, out = sys.argv[1], sys.argv[2], sys.argv[3]
old_dir = sys.argv[4] if len(sys.argv) > 4 else "presets/factory"   # presets as they were for old_exe
fs = 48000
rng = np.random.default_rng(11)
t = np.arange(fs * 3)
x = np.zeros(len(t)); pos = 0
for f0 in (82.4, 110, 196, 329.6):
    L = int(fs / f0); buf = rng.uniform(-1, 1, L); p = 0
    for i in range(len(t) // 4):
        q = (p + 1) % L; x[pos + i] = buf[p]; buf[p] = 0.497 * (buf[p] + buf[q]); p = q
    pos += len(t) // 4
x *= 10 ** (-7.2 / 20) / np.max(np.abs(x))
res = []
with tempfile.TemporaryDirectory() as d:
    inp = os.path.join(d, "in.wav"); M.write_wav(inp, fs, x)
    for f in sorted(os.listdir("presets/factory")):
        p = json.load(open(os.path.join("presets/factory", f)))
        if not os.path.exists(os.path.join(old_dir, f)):
            continue
        po = json.load(open(os.path.join(old_dir, f)))
        olds = [b for b in po["chain"] if b["type"] == "custom_drive"]
        for b in p["chain"]:
            if b["type"] != "custom_drive":
                continue
            ys = {}
            for tag, exe, prm in (("v1", old_exe, olds[0]["params"]), ("v2", new_exe, b["params"])):
                o = os.path.join(d, tag + ".wav")
                cmd = [exe, "-i", inp, "-o", o, "--effect", "custom_drive", "--compensate-latency"]
                for k, v in prm.items():
                    cmd += ["--set", f"{k}={v}"]
                subprocess.run(cmd, check=True, capture_output=True)
                ys[tag] = M.read_wav(o)[1][:, 0]
            r = {}
            for tag, y in ys.items():
                r[tag + "_rms_dbfs"] = float(20 * np.log10(np.sqrt(np.mean(y ** 2)) + 1e-12))
                r[tag + "_peak_dbfs"] = float(20 * np.log10(np.max(np.abs(y)) + 1e-12))
            a, c = ys["v1"], ys["v2"]
            c2 = c * np.sqrt(np.mean(a ** 2) / np.mean(c ** 2))
            r["level_matched_difference_pct"] = float(100 * np.sqrt(np.mean((a - c2) ** 2) / np.mean(a ** 2)))
            r["level_change_db"] = r["v2_rms_dbfs"] - r["v1_rms_dbfs"]
            res.append(dict(preset=f, params=b["params"], **r))
            print(f, {k: round(v, 2) for k, v in r.items()})
json.dump(dict(signal="synthetic Karplus-Strong DI, peak -7.2 dBFS (not a recording)", results=res), open(out, "w"), indent=2)
