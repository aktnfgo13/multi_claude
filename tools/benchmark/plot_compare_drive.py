#!/usr/bin/env python3
"""Plot Custom Drive comparison results (mfx_compare_drive grid.csv / report.json).

usage: plot_compare_drive.py OUT_DIR SERIES_LABEL=PATH_TO_RESULT_DIR [...]
       (engine 'claude' of every dir is drawn under its label, engine 'codex' once if present)

Figures (PNG):
  thd_vs_input.png     THD [%] vs input peak, small multiples per Drive (Headroom 50)
  gain_vs_input.png    gain [dB] vs input peak (static compression curve), same grid
  summary_bars.png     aliasing / phrase residual / attack compression at Drive 0/50/100, H50
"""
import csv, json, os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# validated reference categorical palette (slots 1..3), fixed order
COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
INK, MUTED, GRID = "#1f1f1e", "#6b6a64", "#e4e3de"


def load(dirpath):
    rows = list(csv.DictReader(open(os.path.join(dirpath, "grid.csv"))))
    rep = json.load(open(os.path.join(dirpath, "report.json")))
    return rows, rep


def style(ax):
    ax.grid(True, color=GRID, linewidth=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(MUTED)
    ax.tick_params(colors=MUTED, labelsize=8)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    series = []  # (label, rows, summary)
    codex = None
    for arg in sys.argv[2:]:
        label, path = arg.split("=", 1)
        rows, rep = load(path)
        series.append((label, [r for r in rows if r["engine"] == "claude"],
                       [s for s in rep["same_parameter_summary"] if s["engine"] == "claude"]))
        if codex is None and any(r["engine"] == "codex" for r in rows):
            codex = ("Codex (reference)", [r for r in rows if r["engine"] == "codex"],
                     [s for s in rep["same_parameter_summary"] if s["engine"] == "codex"])
    if codex:
        series.append(codex)

    drives = [0, 50, 100]
    for metric, ylab, fname, logy in [("thd_pct", "THD [%]", "thd_vs_input.png", True),
                                      ("gain_db", "Gain [dB]", "gain_vs_input.png", False)]:
        fig, axs = plt.subplots(1, 3, figsize=(11, 3.4), sharey=True)
        for ax, d in zip(axs, drives):
            for i, (lab, rows, _) in enumerate(series):
                pts = sorted((float(r["in_peak_dbfs"]), float(r[metric])) for r in rows
                             if float(r["drive"]) == d and float(r["headroom"]) == 50)
                if pts:
                    ax.plot([p[0] for p in pts], [max(p[1], 1e-3) for p in pts], color=COLORS[i],
                            linewidth=2, marker="o", markersize=4, label=lab)
            ax.set_title(f"Drive {d} / Headroom 50", fontsize=9, color=INK)
            ax.set_xlabel("Input peak [dBFS]", fontsize=8, color=MUTED)
            if logy:
                ax.set_yscale("log")
            ax.axvline(-7.2, color=MUTED, linewidth=0.8, linestyle=":")
            style(ax)
        axs[0].set_ylabel(ylab, fontsize=8, color=MUTED)
        axs[0].legend(fontsize=8, frameon=False)
        fig.suptitle(f"Custom Drive — {ylab} vs input (dotted: -7.2 dBFS guitar peak)", fontsize=10, color=INK)
        fig.tight_layout()
        fig.savefig(os.path.join(out, fname), dpi=130)
        plt.close(fig)

    metrics = [("alias_db_at_-7.2dBFS", "Aliasing @3953 Hz, -7.2 dBFS [dB]"),
               ("phrase_residual_pct", "Phrase distortion residual [%]"),
               ("attack_gain_change_db", "Attack gain change [dB]")]
    fig, axs = plt.subplots(1, 3, figsize=(11, 3.4))
    w = 0.8 / len(series)
    for ax, (key, title) in zip(axs, metrics):
        for i, (lab, _, summ) in enumerate(series):
            vals = []
            for d in drives:
                m = [s[key] for s in summ if s["drive"] == d and s["headroom"] == 50]
                vals.append(m[0] if m else 0.0)
            xs = [j + (i - (len(series) - 1) / 2) * w for j in range(len(drives))]
            ax.bar(xs, vals, width=w * 0.92, color=COLORS[i], label=lab)
        ax.set_xticks(range(len(drives)), [f"D{d}" for d in drives])
        ax.set_title(title, fontsize=9, color=INK)
        ax.axhline(0, color=MUTED, linewidth=0.8)
        style(ax)
    axs[1].legend(fontsize=8, frameon=False, loc="upper left")
    fig.suptitle("Custom Drive — same knob values, Headroom 50, OS 2x",
                 fontsize=10, color=INK)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "summary_bars.png"), dpi=130)
    plt.close(fig)
    print("wrote", out)


if __name__ == "__main__":
    main()
