#!/usr/bin/env python3
"""
analyze.py - PressTrace sched: buton yanit suresi analizi (A/B/C)

Girdi (arayuzun "CSV Kaydet" ciktilari, measurements/<klasor>/ altinda;
klasorler: ascii64 = 64 bayt ASCII, compact = kisa ikili cerceve):
  <surum>_S<n>.csv           scenario,event_id,t0_us,...,t4_us,status,note
  <surum>_S<n>_counters.csv  name,value   (pencere sayaclari; yoksa sayac kolonlari bos kalir)
  <surum>: A, B ya da C; hizli yol aciksa sonuna F, SystemView izlemesi aciksa
  T, kisa ikili TEL/BTN cercevesi aciksa K (orn. A_S5, BT_S6, AK_S6). Eski
  adlandirma S<n>.csv de okunur.

Cikti (her klasor icin):
  measurements/<klasor>/summary.csv
  analysis/plots/<klasor>/r_vs_event.png        senaryo basina: olay -> R, 20 ms cizgisi
  analysis/plots/<klasor>/stages_by_scenario.png kosu (surum/senaryo) -> asamalarin ortalama sureleri

Tum sureler ayni kart sayacindan (TIM2, 1 us) gelir; farklar mod 2^32
hesaplanir (sayac ~71,6 dakikada doner). R ve asama istatistikleri yalnizca
status == "ok" satirlarindan hesaplanir; digerleri (drop, tx_error, timeout)
ayrica sayilir ve "deadline karsilandi" sayilmaz. drop'un nerede oldugu bos
zamanlardan okunur: buton kuyrugu -> yalnizca t0, TX kuyrugu -> t0..t2.

Kullanim:
    python analyze.py                      # measurements/ altindaki her klasor
    python analyze.py --measurements ../measurements/compact --plots ./plots/compact

Bagimliliklar: pandas, matplotlib (pip install pandas matplotlib)
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

DEADLINE_US = 20_000  # R <= 20 ms deney butcesi
MOD = 2**32

# ID -> (nominal telemetri Hz, nominal ek CPU isi ms, periyot basina TEL) -
# firmware/Core/Src/main.c g_scenarios ile birebir.
SCENARIOS = {
    "S0": (0, 0, 0),
    "S1": (10, 0, 1),
    "S2": (50, 0, 1),
    "S3": (100, 0, 1),
    "S4": (100, 2, 1),
    "S5": (100, 5, 1),
    "S6": (10, 0, 4),
}
# Dosya adi: <surum>_S<n> (surum A/B/C + istege bagli F, T, K) ya da eski S<n>.
RUN_NAME = re.compile(r"^(?:(?P<variant>[ABC]F?T?K?)_)?(?P<scenario>S\d)$")
STATUSES = ["ok", "drop", "tx_error", "timeout"]  # ortak durum degerleri
LEGACY_STATUS = {"tx_drop": "drop", "btn_drop": "drop"}  # eski arayuz ciktilari
STAGES = [  # (kolon, etiket, bas, son)
    ("wait", "t1−t0 task wait", "t0_us", "t1_us"),
    ("prep", "t2−t1 prepare", "t1_us", "t2_us"),
    ("txq", "t3−t2 before TX start", "t2_us", "t3_us"),
    ("uart", "t4−t3 UART line + TC", "t3_us", "t4_us"),
]
STAGE_COLORS = ["#8f7fe0", "#e0a030", "#4c9aff", "#2fbf9a"]
COLUMNS = ["scenario", "event_id", "t0_us", "t1_us", "t2_us", "t3_us", "t4_us", "status"]
COUNTERS = ["accepted", "repeat", "btn_queue_drop", "tx_queue_drop", "tx_queue_max",
            "pool_overflow", "droplog_overflow", "tx_timeout", "tx_start_fail",
            "spurious_tc", "tel_sent", "tel_drop", "tel_period_avg_us",
            "tel_wait_avg_us", "tel_wait_max_us",
            "btn_direct", "btn_latched", "btn_fallback", "btn_result_lost"]


def find_runs(folder: Path) -> list[tuple[str, str, str, Path]]:
    """measurements/ altindaki olay CSV'leri: (kosu adi, surum, senaryo, yol).
    Sayac dosyalari (_counters) ve summary.csv adlari kaliba uymadigi icin elenir."""
    runs = []
    for p in folder.glob("*.csv"):
        m = RUN_NAME.match(p.stem)
        if m and m["scenario"] in SCENARIOS:
            runs.append((p.stem, m["variant"] or "", m["scenario"], p))
    return sorted(runs, key=lambda r: (r[2], r[1]))  # senaryo, sonra surum


def load_events(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path, dtype={"scenario": str, "status": str})
    missing = set(COLUMNS) - set(df.columns)
    if missing:
        raise ValueError(f"{path.name}: eksik kolon(lar): {sorted(missing)}")
    for c in ["t0_us", "t1_us", "t2_us", "t3_us", "t4_us"]:
        df[c] = pd.to_numeric(df[c], errors="coerce")  # bos -> NaN (0 degil)
    df["status"] = df["status"].replace(LEGACY_STATUS)
    df = df.sort_values("event_id").reset_index(drop=True)
    ok = df["status"] == "ok"
    df["r_us"] = ((df["t4_us"] - df["t0_us"]) % MOD).where(ok)
    for key, _, a, b in STAGES:
        df[key] = ((df[b] - df[a]) % MOD).where(ok)
    return df


def load_counters(path: Path) -> dict[str, int]:
    if not path.exists():
        return {}
    c = pd.read_csv(path, dtype=str)  # host_variant gibi metin satirlari da var
    out: dict = {}
    for k, v in zip(c["name"], c["value"]):
        if pd.isna(v):
            continue
        num = pd.to_numeric(v, errors="coerce")
        out[str(k)] = v if pd.isna(num) else (int(num) if float(num).is_integer() else float(num))
    return out


def summarize(run: str, variant: str, sid: str, df: pd.DataFrame, counters: dict[str, int]) -> dict:
    hz, load_ms, tel_per_period = SCENARIOS[sid]
    row: dict = {"run": run, "variant": variant[:1], "trace": "T" in variant,
                 "fast_path": "F" in variant, "compact_frame": "K" in variant, "scenario": sid,
                 "telemetry_hz_nominal": hz, "extra_load_ms_nominal": load_ms,
                 "tel_per_period_nominal": tel_per_period, "n_events": len(df)}
    for s in STATUSES:
        row[f"n_{s}"] = int((df["status"] == s).sum())
    drop = df["status"] == "drop"
    row["drop_btn_queue"] = int((drop & df["t1_us"].isna()).sum())
    row["drop_tx_queue"] = int((drop & df["t1_us"].notna()).sum())
    row["n_other"] = len(df) - sum(row[f"n_{s}"] for s in STATUSES)

    r = df["r_us"].dropna() / 1000.0
    if len(r):
        row.update(r_min_ms=round(r.min(), 3), r_mean_ms=round(r.mean(), 3),
                   r_p95_ms=round(r.quantile(0.95), 3), r_max_ms=round(r.max(), 3),
                   deadline_miss=int((r > DEADLINE_US / 1000).sum()))
        for key, *_ in STAGES:
            row[f"{key}_mean_ms"] = round(df[key].dropna().mean() / 1000.0, 3)
    else:
        row.update(r_min_ms="", r_mean_ms="", r_p95_ms="", r_max_ms="", deadline_miss="")
        for key, *_ in STAGES:
            row[f"{key}_mean_ms"] = ""

    for k in COUNTERS:
        row[k] = counters.get(k, "")
    avg = counters.get("tel_period_avg_us")
    row["tel_rate_hz_measured"] = round(1e6 / avg, 2) if avg else ""

    notes = []
    if len(df) < 30:
        notes.append(f"{len(df)} olay (<30)")
    if counters and counters.get("accepted") not in (None, len(df)):
        notes.append(f"kabul {counters['accepted']} != kayit {len(df)}")
    row["notes"] = "; ".join(notes)
    return row


def plot_r_vs_event(data: dict[str, tuple[str, pd.DataFrame]], out: Path) -> bool:
    """Senaryo basina bir alt grafik: surumler yalnizca ayni yuk altinda karsilastirilir."""
    by_scenario: dict[str, list[tuple[str, pd.DataFrame]]] = {}
    for run, (sid, df) in data.items():
        ok = df.dropna(subset=["r_us"])
        if not ok.empty:
            by_scenario.setdefault(sid, []).append((run, df))
    if not by_scenario:
        return False
    fig, axes = plt.subplots(len(by_scenario), 1, figsize=(9, 4.2 * len(by_scenario)), squeeze=False)
    for ax, (sid, runs) in zip(axes[:, 0], by_scenario.items()):
        for run, df in runs:
            ok = df.dropna(subset=["r_us"])
            excluded = len(df) - len(ok)
            ax.plot(range(1, len(ok) + 1), ok["r_us"] / 1000.0, marker="o", markersize=3,
                    linewidth=0.8, label=f"{run} (n={len(ok)} ok, {excluded} excluded)")
        ax.axhline(DEADLINE_US / 1000, color="red", linestyle="--", linewidth=1, label="deadline 20 ms")
        ax.set_xlabel("Event # within the run (status=ok only)")
        ax.set_ylabel("R = t4 − t0 (ms)")
        ax.set_title(f"{sid}: response time per event")
        ax.legend(fontsize=8)
        ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(out / "r_vs_event.png", dpi=150)
    plt.close(fig)
    return True


def plot_stages(summary: pd.DataFrame, out: Path) -> bool:
    s = summary[summary["r_mean_ms"] != ""].copy()
    if s.empty:
        return False
    fig, ax = plt.subplots(figsize=(max(8, 1.3 * len(s)), 5))
    bottom = [0.0] * len(s)
    for (key, label, *_), color in zip(STAGES, STAGE_COLORS):
        vals = s[f"{key}_mean_ms"].astype(float).tolist()
        ax.bar(s["run"], vals, bottom=bottom, color=color, label=label)
        bottom = [b + v for b, v in zip(bottom, vals)]
    for x, total, n in zip(s["run"], bottom, s["n_ok"]):
        ax.text(x, total, f"{total:.2f} ms\nn={n}", ha="center", va="bottom", fontsize=8)
    ax.axhline(DEADLINE_US / 1000, color="red", linestyle="--", linewidth=1, label="deadline 20 ms")
    ax.set_ylim(0, max(max(bottom), DEADLINE_US / 1000) * 1.18)  # sutun etiketlerine yer
    ax.set_ylabel("Mean duration (ms)")
    ax.set_title("Mean stage durations per run (variant/scenario, status=ok)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out / "stages_by_scenario.png", dpi=150)
    plt.close(fig)
    return True


def analyze_folder(meas: Path, plots: Path) -> int:
    plots.mkdir(parents=True, exist_ok=True)
    data: dict[str, tuple[str, pd.DataFrame]] = {}
    rows = []
    for run, variant, sid, path in find_runs(meas):
        df = load_events(path)
        data[run] = (sid, df)
        rows.append(summarize(run, variant, sid, df, load_counters(meas / f"{run}_counters.csv")))

    if not rows:
        print(f"hic olcum dosyasi yok: {meas} (beklenen ad: A_S5.csv, BT_S6.csv, ...)")
        return 1

    print(f"== {meas.name} ==")
    summary = pd.DataFrame(rows)
    summary.to_csv(meas / "summary.csv", index=False)
    print(summary.to_string(index=False))
    traced = summary.loc[summary["trace"], "run"].tolist()
    if traced:
        print(f"not: {', '.join(traced)} SystemView izlemesi acikken olculdu (adinda T); "
              "izlemenin ek yuku sonuclara dahildir.")

    made = []
    if plot_r_vs_event(data, plots):
        made.append("r_vs_event.png")
    if plot_stages(summary, plots):
        made.append("stages_by_scenario.png")
    print("grafikler:", ", ".join(made) if made else "cizilecek 'ok' olay yok")
    return 0


def main() -> int:
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--measurements", type=Path, default=None,
                    help="tek bir olcum klasoru (varsayilan: measurements/ altindaki her klasor)")
    ap.add_argument("--plots", type=Path, default=None, help="grafik klasoru (--measurements ile)")
    args = ap.parse_args()

    if args.measurements is not None:
        return analyze_folder(args.measurements, args.plots or here / "plots" / args.measurements.name)
    folders = sorted(p for p in (here.parent / "measurements").iterdir() if p.is_dir())
    return max((analyze_folder(f, here / "plots" / f.name) for f in folders), default=1)


if __name__ == "__main__":
    raise SystemExit(main())
