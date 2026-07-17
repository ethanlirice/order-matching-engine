"""Shared helpers for M5's Python analytics layer (PROJECT_SPEC.md §11):
path setup to import the pybind11 module, config construction for each
strategy kind, and DataFrame conversion of a SimulationResult. No plotting
or sweep logic lives here -- see generate_plots.py for that.
"""

import os
import sys

import pandas as pd


def _find_bindings_dir():
    """Locates the built lob_bindings module without hardcoding a build
    directory name -- CI and local dev may use different ones (build,
    build-asan, etc.), so this searches a short list of common locations
    relative to this file, and honors LOB_BUILD_DIR if set.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(here)

    override = os.environ.get("LOB_BUILD_DIR")
    candidates = []
    if override:
        candidates.append(os.path.join(override, "bindings"))
    candidates.append(os.path.join(repo_root, "build", "bindings"))

    for candidate in candidates:
        if os.path.isdir(candidate):
            for name in os.listdir(candidate):
                if name.startswith("lob_bindings.") and name.endswith((".so", ".pyd")):
                    return candidate
    raise RuntimeError(
        "Could not find the built lob_bindings module. Build the project first "
        "(cmake --build build -j) or set LOB_BUILD_DIR to point at your build "
        "directory. Searched: " + ", ".join(candidates)
    )


sys.path.insert(0, _find_bindings_dir())
import lob_bindings as lob  # noqa: E402  (path must be set up first)


STRATEGY_KINDS = {
    "naive": lob.StrategyKind.Naive,
    "inventory_capped": lob.StrategyKind.InventoryCapped,
    "avellaneda_stoikov": lob.StrategyKind.AvellanedaStoikov,
    "ofi": lob.StrategyKind.Ofi,
}


def make_config(
    strategy_name,
    seed=1,
    duration=20000,
    arrival_rate=0.05,
    base_price=10000,
    price_offset_ticks=20,
    aggressive_probability=0.3,
    cancel_probability=0.2,
    half_spread=5,
    quote_size=10,
    max_inventory=50,
    gamma=0.1,
    sigma=1.0,
    kappa=1.5,
    ofi_beta=1.0,
    latency=5,
    markout_horizon=100,
    sharpe_bucket_duration=1000,
):
    """Builds a lob.SimulationConfig with sane defaults; every field is a
    keyword override so a sweep only needs to touch the one parameter it
    varies.
    """
    config = lob.SimulationConfig()
    config.generator.seed = seed
    config.generator.duration = duration
    config.generator.arrival_rate = arrival_rate
    config.generator.base_price = base_price
    config.generator.price_offset_ticks = price_offset_ticks
    config.generator.aggressive_probability = aggressive_probability
    config.generator.cancel_probability = cancel_probability
    config.strategy_kind = STRATEGY_KINDS[strategy_name]
    config.half_spread = half_spread
    config.quote_size = quote_size
    config.max_inventory = max_inventory
    config.gamma = gamma
    config.sigma = sigma
    config.kappa = kappa
    config.ofi_beta = ofi_beta
    config.latency = latency
    config.markout_horizon = markout_horizon
    config.sharpe_bucket_duration = sharpe_bucket_duration
    return config


def run(config):
    return lob.run_simulation(config)


def run_multi_seed(strategy_name, seeds, **config_kwargs):
    """Runs the same config across multiple seeds (only `seed` varies) and
    returns one row per seed with the summary metrics the M5 plots/tables
    aggregate over. Kept here (not in generate_plots.py) so any future
    script needing seed-aggregated data doesn't duplicate this loop.
    """
    config_kwargs.pop("seed", None)
    rows = []
    for seed in seeds:
        result = run(make_config(strategy_name, seed=seed, **config_kwargs))
        fills = fills_frame(result)
        rows.append(
            {
                "seed": seed,
                "total_pnl": result.metrics.pnl.total_pnl,
                "spread_pnl": result.metrics.pnl.spread_pnl,
                "inventory_pnl": result.metrics.pnl.inventory_pnl,
                "max_abs_inventory": result.metrics.max_abs_inventory,
                "sharpe": result.metrics.sharpe,
                "n_fills": len(fills),
                "mean_markout": fills["markout"].mean() if not fills.empty else float("nan"),
                "mean_pure_adverse_selection_cost": (
                    fills["pure_adverse_selection_cost"].mean() if not fills.empty else float("nan")
                ),
            }
        )
    return pd.DataFrame(rows)


def summarize(df, value_columns):
    """Collapses a per-seed DataFrame (as returned by run_multi_seed) into
    mean/std/95%-CI-half-width per column, using a normal approximation
    (adequate here -- N=30 seeds, not claiming small-sample exactness).
    """
    n = len(df)
    summary = {"n_seeds": n}
    for col in value_columns:
        values = df[col].dropna()
        mean = values.mean() if not values.empty else float("nan")
        std = values.std(ddof=1) if len(values) > 1 else float("nan")
        ci95 = 1.96 * std / (len(values) ** 0.5) if len(values) > 1 else float("nan")
        summary[f"{col}_mean"] = mean
        summary[f"{col}_std"] = std
        summary[f"{col}_ci95"] = ci95
    return summary


def fills_frame(result):
    """One row per fill, joined with its markout/adverse-selection metrics."""
    rows = []
    for fm in result.metrics.fill_metrics:
        rows.append(
            {
                "timestamp": fm.fill.timestamp,
                "side": "Buy" if fm.fill.side == lob.Side.Buy else "Sell",
                "price": fm.fill.price,
                "quantity": fm.fill.quantity,
                "inventory_after": fm.fill.inventory_after,
                "pre_mid": fm.pre_mid,
                "post_mid": fm.post_mid,
                "effective_spread": fm.effective_spread,
                "markout": fm.markout,
                "pure_adverse_selection_cost": fm.pure_adverse_selection_cost,
            }
        )
    return pd.DataFrame(rows)


def inventory_frame(result):
    return pd.DataFrame(
        {
            "timestamp": [p.timestamp for p in result.inventory_series],
            "inventory": [p.inventory for p in result.inventory_series],
        }
    )


def mid_price_frame(result):
    return pd.DataFrame(
        {
            "timestamp": [p.timestamp for p in result.mid_price_series],
            "mid": [p.mid for p in result.mid_price_series],
        }
    )
