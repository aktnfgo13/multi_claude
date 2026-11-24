#!/usr/bin/env python3
"""Generate the per-effect parameter tables of docs/PARAMETER_SPECIFICATION.md from the engine itself
(mfx_render --list / --describe), so the document cannot drift from the code."""
import json, subprocess, sys
build = sys.argv[1] if len(sys.argv) > 1 else "build"
exe = f"{build}/mfx_render"
types = [l.split()[0] for l in subprocess.run([exe, "--list"], capture_output=True, text=True).stdout.splitlines() if l.strip()]
out = []
for t in types:
    try:
        d = json.loads(subprocess.run([exe, "--describe", t], capture_output=True, text=True).stdout)
    except Exception:
        continue
    out.append(f"### `{t}` — {d.get('name', t)}\n")
    out.append("| id | 이름 | 범위 | 기본값 | 단위 | 스케일 |\n|---|---|---|---|---|---|")
    for p in d.get("params", []):
        rng = f"{p.get('min')}…{p.get('max')}"
        if p.get("choices"):
            rng = " / ".join(p["choices"])
        out.append(f"| `{p.get('id')}` | {p.get('name')} | {rng} | {p.get('default')} | {p.get('unit', '')} | {p.get('scale', '')} |")
    out.append("")
print("\n".join(out))
