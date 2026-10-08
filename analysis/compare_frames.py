#!/usr/bin/env python3
"""
compare_frames.py - 64 baytlik ASCII satir ile kisa ikili cercevenin karsilastirmasi

Girdi:
  measurements/ascii64/<surum>T_S<n>.csv   64 bayt (PROTOCOL_COMPACT 0)
  measurements/compact/<surum>TK_S<n>.csv  kisa ikili cerceve (PROTOCOL_COMPACT 1)

Cikti:
  analysis/plots/before_after.png   iki panel: R ortalama ve R en buyuk, kosu basina

Iki olcu ayni birimde (ms) olsa da tek eksende birlestirilmez: iki ayri panel.
Kullanim:  python compare_frames.py
"""
from __future__ import annotations

import csv
from pathlib import Path

import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
MEAS = HERE.parent / "measurements"
OUT = HERE / "plots" / "before_after.png"
MOD = 2**32
DEADLINE_MS = 20.0

RUNS = ["A_S5", "B_S5", "C_S5", "A_S6", "B_S6", "C_S6"]
SERIES = [  # (etiket, dosya adi uretici, renk) - dogrulanmis kategorik palet, slot 1 ve 2
    ("64-byte ASCII frame (before)", lambda r: MEAS / "ascii64" / f"{r[0]}T_{r[2:]}.csv", "#2a78d6"),
    ("19/20-byte binary frame (after)", lambda r: MEAS / "compact" / f"{r[0]}TK_{r[2:]}.csv", "#eb6834"),
]
TEXT, MUTED, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"


def r_values(path: Path) -> list[float]:
    with path.open(encoding="utf-8") as f:
        return [((int(r["t4_us"]) - int(r["t0_us"])) % MOD) / 1000.0
                for r in csv.DictReader(f) if r["status"] == "ok"]


def main() -> None:
    data = {label: [r_values(path(run)) for run in RUNS] for label, path, _ in SERIES}
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.8), facecolor=SURFACE)
    width, gap = 0.38, 0.02
    for ax, (title, agg) in zip(axes, [("Mean R (ms)", lambda v: sum(v) / len(v)),
                                       ("Max R (ms)", max)]):
        ax.set_facecolor(SURFACE)
        for i, (label, _, color) in enumerate(SERIES):
            xs = [k + (i - 0.5) * (width + gap) for k in range(len(RUNS))]
            ax.bar(xs, [agg(v) for v in data[label]], width=width, color=color, label=label, zorder=2)
        ax.axhline(DEADLINE_MS, color=MUTED, linestyle=(0, (4, 3)), linewidth=1.2, zorder=1)
        ax.text(len(RUNS) - 0.5, DEADLINE_MS + 0.3, "20 ms deadline", ha="right", va="bottom", fontsize=9, color=MUTED)
        ax.set_xticks(range(len(RUNS)), [r.replace("_", " / ") for r in RUNS], color=TEXT)
        ax.set_ylim(0, 22)
        ax.set_title(title, loc="left", fontsize=12, color=TEXT)
        ax.grid(axis="y", color=GRID, linewidth=0.8, zorder=0)
        ax.tick_params(colors=MUTED, length=0)
        for s in ("top", "right", "left"):
            ax.spines[s].set_visible(False)
        ax.spines["bottom"].set_color(GRID)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, frameon=False, fontsize=10, labelcolor=TEXT, ncol=2,
               loc="upper left", bbox_to_anchor=(0.005, 0.925))
    fig.suptitle("Effect of the compact binary frame · 230400 baud · 30 presses per run",
                 x=0.01, ha="left", fontsize=13, color=TEXT)
    fig.tight_layout(rect=(0, 0, 1, 0.86))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT, dpi=150, facecolor=SURFACE)
    print("yazildi:", OUT)


if __name__ == "__main__":
    main()
