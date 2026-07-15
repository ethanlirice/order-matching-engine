#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "lob/mm/market_maker.hpp"
#include "lob/sim/market_data_log.hpp"

namespace lob::mm {

// PROJECT_SPEC.md §8's metrics: what to measure and horizon lengths are
// caller-supplied since they depend on the specific run being analyzed,
// not on the strategy.
struct MetricsConfig {
  // How far after a fill to sample the "post" mid for markout.
  Timestamp markout_horizon = 0;
  // End of the session -- used for the final mark-to-market mid (total
  // PnL) and as the fill-rate/Sharpe denominator. Assumes the session
  // started at t=0.
  Timestamp session_end = 0;
  // Sharpe is computed over equal-duration mark-to-market buckets; this
  // is each bucket's width.
  Timestamp sharpe_bucket_duration = 1;
};

// Per-fill decomposition. pre_mid/post_mid (and everything derived from
// them) are nullopt if MarketDataLog has no sample to answer that as-of
// query yet (e.g. a fill before the book was ever two-sided) -- callers
// aggregating these should skip fills with no value rather than treat
// missing data as zero.
struct FillMetrics {
  Fill fill;
  std::optional<double> pre_mid;   // mid strictly before this fill
  std::optional<double> post_mid;  // mid at fill.timestamp + markout_horizon

  // 2 * side_sign * (pre_mid - fill_price) -- how much better/worse than
  // the pre-trade mid this fill executed at, doubled to express it as a
  // round-trip spread capture.
  std::optional<double> effective_spread;
  // side_sign * (post_mid - fill_price) -- this fill's PnL a markout
  // horizon later, per §8. Conflates execution-price quality with pure
  // post-trade drift; see pure_adverse_selection_cost for the isolated
  // drift-only version.
  std::optional<double> markout;
  // side_sign * (pre_mid - post_mid), reported as a cost (positive =
  // bad). Drift-only (no fill_price term) -- this is what actually
  // isolates adverse selection, distinct from execution-price quality.
  std::optional<double> pure_adverse_selection_cost;
};

// total_pnl = cash_pnl + inventory_final * mid_final - inventory_initial *
// mid_initial, assuming inventory_initial == 0 and cash_initial == 0 (a
// strategy always starts flat with no cash committed).
// spread_pnl = sum over fills with a valid effective_spread of
// (effective_spread / 2) * quantity.
// inventory_pnl = total_pnl - spread_pnl -- the residual, which is exactly
// the textbook inventory-PnL definition (mark pre-existing inventory
// start->end, mark each fill's acquired unit from its own fill-time mid to
// the final mid), not an arbitrary leftover.
struct PnlDecomposition {
  double total_pnl = 0.0;
  double spread_pnl = 0.0;
  double inventory_pnl = 0.0;
};

struct MetricsSummary {
  std::vector<FillMetrics> fill_metrics;
  PnlDecomposition pnl;
  // Unannualized: mean(delta mark-to-market) / sample_std(delta
  // mark-to-market, N-1) over equal-duration buckets across the full
  // session, computed over the FULL mark-to-market series (cash +
  // inventory * mid), not cash alone -- a cash-only series would hide an
  // inventory-heavy strategy's true risk until it unwinds. 0 if fewer
  // than 2 buckets or zero variance (e.g. a strategy that never filled),
  // not NaN.
  double sharpe = 0.0;
  // Fills per unit virtual time (not fills per submission -- the base
  // class favors Modify over Submit for in-place repricing, so a
  // submission-count denominator would badly undercount quoting
  // exposure), over [0, session_end].
  double fill_rate = 0.0;
  // Signed running extremes of inventory (including the implicit initial
  // 0, even if no fills occur) plus the larger-magnitude of the two.
  Inventory max_inventory = 0;
  Inventory min_inventory = 0;
  Inventory max_abs_inventory = 0;
};

MetricsSummary ComputeMetrics(const MarketMaker& maker, const sim::MarketDataLog& market_data_log,
                              const MetricsConfig& config);

}  // namespace lob::mm
