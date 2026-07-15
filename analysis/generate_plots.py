"""Generates the four required plots (PROJECT_SPEC.md §8/§14) plus the
gamma sweep, and prints a findings summary (the numbers that get written
into README.md by hand -- plot images themselves aren't committed, per
CLAUDE.md's convention of not committing generated binaries).

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

from lob_sweep import fills_frame, inventory_frame, make_config, run

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output")
os.makedirs(OUTPUT_DIR, exist_ok=True)

STRATEGIES = ["naive", "inventory_capped", "avellaneda_stoikov", "ofi"]
STRATEGY_LABELS = {
    "naive": "Naive",
    "inventory_capped": "Inventory-capped",
    "avellaneda_stoikov": "Avellaneda-Stoikov",
    "ofi": "OFI",
}

COMMON = dict(seed=1, duration=50000, arrival_rate=0.05, latency=5, gamma=0.001)


def savefig(fig, name):
    path = os.path.join(OUTPUT_DIR, name)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {path}")


def plot_pnl_decomposition():
    print("\n=== PnL decomposition (spread vs inventory) ===")
    rows = []
    for name in STRATEGIES:
        result = run(make_config(name, **COMMON))
        rows.append(
            {
                "strategy": STRATEGY_LABELS[name],
                "spread_pnl": result.metrics.pnl.spread_pnl,
                "inventory_pnl": result.metrics.pnl.inventory_pnl,
                "total_pnl": result.metrics.pnl.total_pnl,
                "fills": len(result.fills),
            }
        )
    df = pd.DataFrame(rows)
    print(df.to_string(index=False))

    fig, ax = plt.subplots(figsize=(7, 4.5))
    x = np.arange(len(df))
    ax.bar(x, df["spread_pnl"], label="Spread PnL")
    ax.bar(x, df["inventory_pnl"], bottom=df["spread_pnl"], label="Inventory PnL")
    ax.set_xticks(x)
    ax.set_xticklabels(df["strategy"], rotation=15)
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_ylabel("PnL")
    ax.set_title(f"PnL decomposition (duration={COMMON['duration']}, seed={COMMON['seed']})")
    ax.legend()
    savefig(fig, "pnl_decomposition.png")
    return df


def plot_inventory_boundedness():
    print("\n=== Inventory boundedness over time ===")
    fig, ax = plt.subplots(figsize=(8, 4.5))
    summary_rows = []
    for name in STRATEGIES:
        result = run(make_config(name, **COMMON))
        inv = inventory_frame(result)
        if not inv.empty:
            ax.step(inv["timestamp"], inv["inventory"], where="post", label=STRATEGY_LABELS[name])
        summary_rows.append(
            {
                "strategy": STRATEGY_LABELS[name],
                "max_inventory": result.metrics.max_inventory,
                "min_inventory": result.metrics.min_inventory,
                "max_abs_inventory": result.metrics.max_abs_inventory,
            }
        )
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_xlabel("virtual time")
    ax.set_ylabel("inventory")
    ax.set_title(f"Inventory over time (duration={COMMON['duration']}, seed={COMMON['seed']})")
    ax.legend()
    savefig(fig, "inventory_boundedness.png")

    df = pd.DataFrame(summary_rows)
    print(df.to_string(index=False))
    return df


def plot_adverse_selection_markout():
    print("\n=== Adverse-selection markout: AS vs OFI ===")
    rows = []
    for name in ["avellaneda_stoikov", "ofi"]:
        result = run(make_config(name, **COMMON))
        fills = fills_frame(result)
        rows.append(
            {
                "strategy": STRATEGY_LABELS[name],
                "n_fills": len(fills),
                "mean_markout": fills["markout"].mean() if not fills.empty else float("nan"),
                "mean_pure_adverse_selection_cost": (
                    fills["pure_adverse_selection_cost"].mean() if not fills.empty else float("nan")
                ),
            }
        )
    df = pd.DataFrame(rows)
    print(df.to_string(index=False))

    fig, ax = plt.subplots(figsize=(6, 4.5))
    x = np.arange(len(df))
    width = 0.35
    ax.bar(x - width / 2, df["mean_markout"], width, label="Markout")
    ax.bar(x + width / 2, df["mean_pure_adverse_selection_cost"], width, label="Pure adverse-selection cost")
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_xticks(x)
    ax.set_xticklabels(df["strategy"])
    ax.set_ylabel("mean value per fill")
    ax.set_title("Adverse-selection markout: AS vs OFI")
    ax.legend()
    savefig(fig, "adverse_selection_markout.png")
    return df


def plot_pnl_vs_latency():
    print("\n=== PnL vs injected latency (OFI) ===")
    latencies = [0, 5, 10, 20, 50, 100, 200, 500]
    rows = []
    for latency in latencies:
        cfg = dict(COMMON)
        cfg["latency"] = latency
        result = run(make_config("ofi", **cfg))
        rows.append(
            {
                "latency": latency,
                "total_pnl": result.metrics.pnl.total_pnl,
                "fills": len(result.fills),
            }
        )
    df = pd.DataFrame(rows)
    print(df.to_string(index=False))

    fig, ax = plt.subplots(figsize=(6, 4.5))
    ax.plot(df["latency"], df["total_pnl"], marker="o")
    ax.axhline(0, color="black", linewidth=0.8)
    ax.set_xlabel("injected latency (virtual ticks)")
    ax.set_ylabel("total PnL")
    ax.set_title("PnL vs injected latency (OFI)")
    savefig(fig, "pnl_vs_latency.png")
    return df


def gamma_sweep():
    print("\n=== Gamma sweep (Avellaneda-Stoikov) ===")
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
        result = run(make_config("avellaneda_stoikov", **cfg))
        rows.append(
            {
                "gamma": gamma,
                "total_pnl": result.metrics.pnl.total_pnl,
                "max_abs_inventory": result.metrics.max_abs_inventory,
                "sharpe": result.metrics.sharpe,
                "fills": len(result.fills),
            }
        )
    df = pd.DataFrame(rows)
    print(df.to_string(index=False))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))
    ax1.plot(df["gamma"], df["total_pnl"], marker="o")
    ax1.set_xlabel("gamma (risk aversion)")
    ax1.set_ylabel("total PnL")
    ax1.set_xscale("log")
    ax2.plot(df["gamma"], df["max_abs_inventory"], marker="o", color="tab:orange")
    ax2.set_xlabel("gamma (risk aversion)")
    ax2.set_ylabel("max |inventory|")
    ax2.set_xscale("log")
    fig.suptitle("Gamma sweep (Avellaneda-Stoikov)")
    savefig(fig, "gamma_sweep.png")
    return df


if __name__ == "__main__":
    plot_pnl_decomposition()
    plot_inventory_boundedness()
    plot_adverse_selection_markout()
    plot_pnl_vs_latency()
    gamma_sweep()
    print("\nDone.")
