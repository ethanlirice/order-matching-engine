#pragma once

#include <cstdint>
#include <vector>

#include "lob/order.hpp"
#include "lob/sim/event.hpp"

namespace lob::sim {

// Poisson-arrival synthetic order-flow generator (PROJECT_SPEC.md §7
// "Synthetic mode"): deterministic given a fixed seed, for stress-testing
// and controlled experiments without needing external data. Produces a
// mostly-passive (resting) flow scattered around a slowly-drifting
// reference price, with a configurable fraction of aggressive (likely-
// crossing) orders so real matching activity actually occurs, plus
// occasional cancels of previously-generated ids.
struct SyntheticGeneratorConfig {
  std::uint64_t seed = 0;
  Timestamp duration = 1'000'000;  // total virtual ticks to generate over
  double arrival_rate = 0.01;      // Poisson intensity: expected events per tick
  Price base_price = 10000;
  Price price_offset_ticks = 20;  // max offset from the drifting mid price
  Quantity min_quantity = 1;
  Quantity max_quantity = 20;
  double aggressive_probability = 0.3;  // fraction of new orders priced to likely cross
  double cancel_probability = 0.2;  // fraction of events that cancel a live id instead of adding
};

// Generator-assigned OrderIds start at 1 and increment by one per Add --
// never reach id_space.hpp's kStrategyIdBase for any realistic
// (duration, arrival_rate) combination.
class SyntheticGenerator {
 public:
  explicit SyntheticGenerator(SyntheticGeneratorConfig config);

  // Generates the full event stream up front (not streamed lazily),
  // already sorted by timestamp ascending with sequence numbers reflecting
  // generation order -- ready to push directly onto an EventQueue.
  std::vector<Event> Generate();

 private:
  SyntheticGeneratorConfig config_;
};

}  // namespace lob::sim
