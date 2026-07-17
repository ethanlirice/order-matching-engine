"""Generates the four required plots (PROJECT_SPEC.md §8/§14) plus the
gamma sweep, and prints a findings summary (the numbers that get written
into README.md by hand -- plot images themselves aren't committed, per
CLAUDE.md's convention of not committing generated binaries).

Every summary statistic (PnL decomposition, adverse-selection markout,
PnL-vs-latency, gamma sweep) is aggregated over NUM_SEEDS independent
seeds and reported as mean +/- 95% CI, not a single run -- RESULTS.md's
prior "one seed" caveat is retired by this. The inventory-boundedness
*time series* plot still shows one representative seed (a multi-seed
overlay of raw paths is unreadable); its summary table (max |inventory|)
is multi-seed aggregated like everything else.

Run from the repo root after building the C++ project:
    cmake --build build -j
    python3 analysis/generate_plots.py
"""

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from lob_sweep import fills_frame, inventory_frame, make_config, run, run_multi_seed, summarize

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output")
os.makedirs(OUTPUT_DIR, exist_ok=True)

STRATEGIES = ["naive", "inventory_capped", "avellaneda_stoikov", "ofi"]
STRATEGY_LABELS = {
    "naive": "Naive",
    "inventory_capped": "Inventory-capped",
    "avellaneda_stoikov": "Avellaneda-Stoikov",
    "ofi": "OFI",
}

COMMON = dict(duration=50000, arrival_rate=0.05, latency=5, gamma=0.001)
NUM_SEEDS = 30
SEEDS = list(range(1, NUM_SEEDS + 1))


def savefig(fig, name):
    path = os.path.join(OUTPUT_DIR, name)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {path}")


def plot_pnl_decomposition():
    print(f"\n=== PnL decomposition (spread vs inventory), {NUM_SEEDS} seeds ===")
    rows = []
    for name in STRATEGIES:
        per_seed = run_multi_seed(name, SEEDS, **COMMON)
        summary = summarize(per_seed, ["spread_pnl", "inventory_pnl", "total_pnl", "n_fills"])
        summary["strategy"] = STRATEGY_LABELS[name]
        rows.append(summary)
    df = pd.DataFrame(rows)
    print(
        df[
            [
                "strategy",
                "spread_pnl_mean",
                "spread_pnl_ci95",
                "inventory_pnl_mean",
                "inventory_pnl_ci95",
                "total_pnl_mean",
                "total_pnl_ci95",
                "n_fills_mean",
            ]
        ].to_string(index=False)
    )

    fig, ax = plt.subplots(figsize=(7, 4.5))
    x = np.arange(len(df))
    ax.bar(x, df["spread_pnl_mean"], yerr=df["spread_pnl_ci95"], capsize=4, label="Spread PnL")
    ax.bar(
        x,
        df["inventory_pnl_mean"],
        bottom=df["spread_pnl_mean"],
        yerr=df["inventory_pnl_ci95"],
        capsize=4,
        label="Inventory PnL",
    )
    ax.set_xticks(x)
    ax.set_xticklabels(df["strategy"], rotation=15)
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_ylabel("PnL")
    ax.set_title(f"PnL decomposition (duration={COMMON['duration']}, {NUM_SEEDS} seeds, error bars = 95% CI)")
    ax.legend()
    savefig(fig, "pnl_decomposition.png")
    return df


def plot_inventory_boundedness():
    print(f"\n=== Inventory boundedness over time (path: seed=1; summary: {NUM_SEEDS} seeds) ===")
    fig, ax = plt.subplots(figsize=(8, 4.5))
    summary_rows = []
    for name in STRATEGIES:
        path_result = run(make_config(name, seed=1, **COMMON))
        inv = inventory_frame(path_result)
        if not inv.empty:
            ax.step(inv["timestamp"], inv["inventory"], where="post", label=STRATEGY_LABELS[name])

        per_seed_max_abs = []
        for seed in SEEDS:
            result = run(make_config(name, seed=seed, **COMMON))
            per_seed_max_abs.append(result.metrics.max_abs_inventory)
        per_seed_max_abs = pd.Series(per_seed_max_abs, dtype=float)
        summary_rows.append(
            {
                "strategy": STRATEGY_LABELS[name],
                "max_abs_inventory_mean": per_seed_max_abs.mean(),
                "max_abs_inventory_ci95": 1.96 * per_seed_max_abs.std(ddof=1) / (len(per_seed_max_abs) ** 0.5),
                "max_abs_inventory_worst": per_seed_max_abs.max(),
            }
        )
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_xlabel("virtual time")
    ax.set_ylabel("inventory")
    ax.set_title(f"Inventory over time, seed=1 (duration={COMMON['duration']})")
    ax.legend()
    savefig(fig, "inventory_boundedness.png")

    df = pd.DataFrame(summary_rows)
    print(df.to_string(index=False))
    return df


def plot_adverse_selection_markout():
    print(f"\n=== Adverse-selection markout: AS vs OFI, {NUM_SEEDS} seeds ===")
    rows = []
    for name in ["avellaneda_stoikov", "ofi"]:
        per_seed = run_multi_seed(name, SEEDS, **COMMON)
        summary = summarize(per_seed, ["mean_markout", "mean_pure_adverse_selection_cost", "n_fills"])
        summary["strategy"] = STRATEGY_LABELS[name]
        rows.append(summary)
    df = pd.DataFrame(rows)
    print(
        df[
            [
                "strategy",
                "n_fills_mean",
                "mean_markout_mean",
                "mean_markout_ci95",
                "mean_pure_adverse_selection_cost_mean",
                "mean_pure_adverse_selection_cost_ci95",
            ]
        ].to_string(index=False)
    )

    # Whether OFI's isolated adverse-selection improvement over AS holds up
    # across seeds: overlapping CIs on the difference means "not
    # distinguishable from noise at this sample size", reported honestly
    # either way rather than picking whichever run looked good.
    as_row = df[df["strategy"] == "Avellaneda-Stoikov"].iloc[0]
    ofi_row = df[df["strategy"] == "OFI"].iloc[0]
    diff = ofi_row["mean_pure_adverse_selection_cost_mean"] - as_row["mean_pure_adverse_selection_cost_mean"]
    diff_ci95 = (as_row["mean_pure_adverse_selection_cost_ci95"] ** 2 + ofi_row["mean_pure_adverse_selection_cost_ci95"] ** 2) ** 0.5
    print(
        f"  OFI - AS pure adverse-selection cost: {diff:+.4f} (95% CI half-width {diff_ci95:.4f}) "
        f"-> {'distinguishable from 0' if abs(diff) > diff_ci95 else 'NOT distinguishable from 0 at this seed count'}"
    )

    fig, ax = plt.subplots(figsize=(6, 4.5))
    x = np.arange(len(df))
    width = 0.35
    ax.bar(
        x - width / 2,
        df["mean_markout_mean"],
        width,
        yerr=df["mean_markout_ci95"],
        capsize=4,
        label="Markout",
    )
    ax.bar(
        x + width / 2,
        df["mean_pure_adverse_selection_cost_mean"],
        width,
        yerr=df["mean_pure_adverse_selection_cost_ci95"],
        capsize=4,
        label="Pure adverse-selection cost",
    )
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_xticks(x)
    ax.set_xticklabels(df["strategy"])
    ax.set_ylabel("mean value per fill")
    ax.set_title(f"Adverse-selection markout: AS vs OFI ({NUM_SEEDS} seeds, error bars = 95% CI)")
    ax.legend()
    savefig(fig, "adverse_selection_markout.png")
    return df


def plot_pnl_vs_latency():
    print(f"\n=== PnL vs injected latency (OFI), {NUM_SEEDS} seeds per point ===")
    latencies = [0, 5, 10, 20, 50, 100, 200, 500]
    rows = []
    for latency in latencies:
        cfg = dict(COMMON)
        cfg["latency"] = latency
        per_seed = run_multi_seed("ofi", SEEDS, **cfg)
        summary = summarize(per_seed, ["total_pnl", "n_fills"])
        summary["latency"] = latency
        rows.append(summary)
    df = pd.DataFrame(rows)
    print(df[["latency", "total_pnl_mean", "total_pnl_ci95", "n_fills_mean"]].to_string(index=False))

    fig, ax = plt.subplots(figsize=(6, 4.5))
    ax.errorbar(df["latency"], df["total_pnl_mean"], yerr=df["total_pnl_ci95"], marker="o", capsize=4)
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_xlabel("injected latency (virtual ticks)")
    ax.set_ylabel("total PnL")
    ax.set_title(f"PnL vs injected latency (OFI, {NUM_SEEDS} seeds, error bars = 95% CI)")
    savefig(fig, "pnl_vs_latency.png")
    return df


def gamma_sweep():
    print(f"\n=== Gamma sweep (Avellaneda-Stoikov), {NUM_SEEDS} seeds per point ===")
    # AS/OFI's horizon is the full session duration (COMMON["duration"]),
    # so variance_term = gamma*sigma^2*tau is huge near the start of a long
    # session unless gamma is scaled down accordingly -- see the README's
    # "Calibrating gamma to session length" note. This range is centered on
    # the gamma found (empirically) to produce a comparable fill count to
    # the other strategies at COMMON["duration"].
    gammas = [0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05]
    rows = []
    for gamma in gammas:
        cfg = dict(COMMON)
        cfg["gamma"] = gamma
        per_seed = run_multi_seed("avellaneda_stoikov", SEEDS, **cfg)
        summary = summarize(per_seed, ["total_pnl", "max_abs_inventory", "sharpe", "n_fills"])
        summary["gamma"] = gamma
        rows.append(summary)
    df = pd.DataFrame(rows)
    print(
        df[
            [
                "gamma",
                "total_pnl_mean",
                "total_pnl_ci95",
                "max_abs_inventory_mean",
                "max_abs_inventory_ci95",
                "n_fills_mean",
            ]
        ].to_string(index=False)
    )

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))
    ax1.errorbar(df["gamma"], df["total_pnl_mean"], yerr=df["total_pnl_ci95"], marker="o", capsize=4)
    ax1.set_xlabel("gamma (risk aversion)")
    ax1.set_ylabel("total PnL")
    ax1.set_xscale("log")
    ax2.errorbar(
        df["gamma"],
        df["max_abs_inventory_mean"],
        yerr=df["max_abs_inventory_ci95"],
        marker="o",
        capsize=4,
        color="tab:orange",
    )
    ax2.set_xlabel("gamma (risk aversion)")
    ax2.set_ylabel("max |inventory|")
    ax2.set_xscale("log")
    fig.suptitle(f"Gamma sweep (Avellaneda-Stoikov, {NUM_SEEDS} seeds, error bars = 95% CI)")
    savefig(fig, "gamma_sweep.png")
    return df


if __name__ == "__main__":
    plot_pnl_decomposition()
    plot_inventory_boundedness()
    plot_adverse_selection_markout()
    plot_pnl_vs_latency()
    gamma_sweep()
    print("\nDone.")
