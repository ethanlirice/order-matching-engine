#include "lob/mm/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace lob::mm {

namespace {

constexpr std::uint64_t kMaxOrdinal = std::numeric_limits<std::uint64_t>::max();

double SideSign(Side side) {
  return side == Side::Buy ? 1.0 : -1.0;
}

FillMetrics ComputeFillMetrics(const Fill& fill, const sim::MarketDataLog& market_data_log,
                               Timestamp markout_horizon) {
  FillMetrics result;
  result.fill = fill;
  result.pre_mid = market_data_log.MidStrictlyBefore(fill.timestamp, fill.event_ordinal);
  result.post_mid = market_data_log.MidAsOf(fill.timestamp + markout_horizon, kMaxOrdinal);

  double side_sign = SideSign(fill.side);
  double price = static_cast<double>(fill.price);

  if (result.pre_mid.has_value()) {
    result.effective_spread = 2.0 * side_sign * (*result.pre_mid - price);
  }
  if (result.post_mid.has_value()) {
    result.markout = side_sign * (*result.post_mid - price);
  }
  if (result.pre_mid.has_value() && result.post_mid.has_value()) {
    result.pure_adverse_selection_cost = side_sign * (*result.pre_mid - *result.post_mid);
  }

  return result;
}

PnlDecomposition ComputePnl(const MarketMaker& maker, const std::vector<FillMetrics>& fill_metrics,
                            const sim::MarketDataLog& market_data_log, Timestamp session_end) {
  PnlDecomposition pnl;

  double spread_pnl = 0.0;
  for (const FillMetrics& fm : fill_metrics) {
    if (fm.effective_spread.has_value()) {
      spread_pnl += (*fm.effective_spread / 2.0) * static_cast<double>(fm.fill.quantity);
    }
  }
  pnl.spread_pnl = spread_pnl;

  double final_mid = market_data_log.MidAsOf(session_end, kMaxOrdinal).value_or(0.0);
  // inventory_initial == 0, cash_initial == 0 (a strategy always starts
  // flat with no cash committed) -- not derived, an assumed precondition.
  pnl.total_pnl = maker.cash() + static_cast<double>(maker.inventory()) * final_mid;
  pnl.inventory_pnl = pnl.total_pnl - pnl.spread_pnl;

  return pnl;
}

double ComputeSharpe(const std::vector<Fill>& fills, const sim::MarketDataLog& market_data_log,
                     Timestamp session_end, Timestamp bucket_duration) {
  if (bucket_duration == 0 || session_end == 0) {
    return 0.0;
  }
  std::uint64_t num_buckets = session_end / bucket_duration;
  if (num_buckets < 2) {
    return 0.0;
  }

  std::vector<double> mark_to_market;
  mark_to_market.reserve(num_buckets + 1);
  mark_to_market.push_back(0.0);  // t=0: flat, no cash committed.

  double cumulative_cash = 0.0;
  Inventory current_inventory = 0;
  std::size_t fill_index = 0;

  for (std::uint64_t k = 1; k <= num_buckets; ++k) {
    Timestamp bucket_end = k * bucket_duration;
    while (fill_index < fills.size() && fills[fill_index].timestamp <= bucket_end) {
      const Fill& fill = fills[fill_index];
      double notional = static_cast<double>(fill.quantity) * static_cast<double>(fill.price);
      cumulative_cash += (fill.side == Side::Buy) ? -notional : notional;
      current_inventory = fill.inventory_after;
      ++fill_index;
    }
    double mid = market_data_log.MidAsOf(bucket_end, kMaxOrdinal).value_or(0.0);
    mark_to_market.push_back(cumulative_cash + static_cast<double>(current_inventory) * mid);
  }

  std::vector<double> deltas;
  deltas.reserve(num_buckets);
  for (std::size_t i = 1; i < mark_to_market.size(); ++i) {
    deltas.push_back(mark_to_market[i] - mark_to_market[i - 1]);
  }

  double mean = 0.0;
  for (double delta : deltas) {
    mean += delta;
  }
  mean /= static_cast<double>(deltas.size());

  double variance = 0.0;
  for (double delta : deltas) {
    variance += (delta - mean) * (delta - mean);
  }
  variance /= static_cast<double>(deltas.size() - 1);
  double stddev = std::sqrt(variance);

  return stddev == 0.0 ? 0.0 : mean / stddev;
}

}  // namespace

MetricsSummary ComputeMetrics(const MarketMaker& maker, const sim::MarketDataLog& market_data_log,
                              const MetricsConfig& config) {
  MetricsSummary summary;

  summary.fill_metrics.reserve(maker.fills().size());
  for (const Fill& fill : maker.fills()) {
    summary.fill_metrics.push_back(
        ComputeFillMetrics(fill, market_data_log, config.markout_horizon));
  }

  summary.pnl = ComputePnl(maker, summary.fill_metrics, market_data_log, config.session_end);

  summary.sharpe = ComputeSharpe(maker.fills(), market_data_log, config.session_end,
                                 config.sharpe_bucket_duration);

  summary.fill_rate = config.session_end == 0 ? 0.0
                                              : static_cast<double>(maker.fills().size()) /
                                                    static_cast<double>(config.session_end);

  Inventory running_max = 0;
  Inventory running_min = 0;
  for (const Fill& fill : maker.fills()) {
    running_max = std::max(running_max, fill.inventory_after);
    running_min = std::min(running_min, fill.inventory_after);
  }
  summary.max_inventory = running_max;
  summary.min_inventory = running_min;
  summary.max_abs_inventory = std::max(running_max, -running_min);

  return summary;
}

}  // namespace lob::mm
