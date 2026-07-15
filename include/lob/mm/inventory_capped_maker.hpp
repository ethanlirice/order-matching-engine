#pragma once

#include "lob/mm/market_maker.hpp"

namespace lob::mm {

struct InventoryCappedMakerConfig {
  Price half_spread = 5;
  Quantity quote_size = 10;
  Inventory max_inventory = 50;
};

// Identical to NaiveMaker, except it stops quoting a side once inventory
// hits the configured cap in that direction (PROJECT_SPEC.md §8).
class InventoryCappedMaker : public MarketMaker {
 public:
  explicit InventoryCappedMaker(InventoryCappedMakerConfig config) : config_(config) {}

 protected:
  Quote ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp now) override;

 private:
  InventoryCappedMakerConfig config_;
};

}  // namespace lob::mm
