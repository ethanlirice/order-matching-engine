#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "lob/order.hpp"

namespace lob::sim {

struct MidPriceSample {
  Timestamp timestamp = 0;
  std::uint64_t event_ordinal = 0;
  double mid = 0.0;    // only meaningful if valid == true
  bool valid = false;  // false if the book didn't have both a bid and ask
};

// Append-only, naturally-sorted-by-processing-order mid-price time series,
// recorded once per processed Simulator event. Supports the two as-of
// queries M5's metrics need:
//  - MidAsOf: latest sample at or before a key ("mid after a markout
//    horizon has elapsed").
//  - MidStrictlyBefore: latest sample strictly before a key (the pre-trade
//    reference mid). This must exclude the current event's own post-trade
//    snapshot -- OnTrade fires before the post-event OnBookUpdate that
//    reflects that trade's own book impact, so tying on timestamp alone
//    would let a fill contaminate its own "mid before this trade"
//    reference.
//
// Keyed by (timestamp, event_ordinal): event_ordinal must be Simulator's
// own monotonic per-processed-event counter, NOT Event::sequence (that's
// only unique within one event kind's bucket, so it can't provide a
// global strict order across Replay/StrategyOrderArrival at the same
// timestamp).
class MarketDataLog {
 public:
  void Record(Timestamp timestamp, std::uint64_t event_ordinal, bool has_bid, Price best_bid,
              bool has_ask, Price best_ask);

  std::optional<double> MidAsOf(Timestamp timestamp, std::uint64_t event_ordinal) const;
  std::optional<double> MidStrictlyBefore(Timestamp timestamp, std::uint64_t event_ordinal) const;

 private:
  std::vector<MidPriceSample> samples_;
};

}  // namespace lob::sim
