#pragma once

#include "lob/mm/market_maker.hpp"

namespace lob::mm {

struct NaiveMakerConfig {
  Price half_spread = 5;
  Quantity quote_size = 10;
};

// Fixed spread around mid, ignores inventory (PROJECT_SPEC.md §8 -- the
// null strategy; expected to accumulate inventory and lose).
class NaiveMaker : public MarketMaker {
 public:
  explicit NaiveMaker(NaiveMakerConfig config) : config_(config) {}

 protected:
  Quote ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp now) override;

 private:
  NaiveMakerConfig config_;
};

}  // namespace lob::mm
