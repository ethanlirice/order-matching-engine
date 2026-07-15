#include "lob/mm/inventory_capped_maker.hpp"

namespace lob::mm {

Quote InventoryCappedMaker::ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp /*now*/) {
  Quote quote;
  std::optional<double> mid = ReferenceMid(snapshot);
  if (!mid.has_value()) {
    return quote;
  }

  double half_spread = static_cast<double>(config_.half_spread);

  if (inventory() < config_.max_inventory) {
    quote.has_bid = true;
    quote.bid_price = RoundToTick(*mid - half_spread);
    quote.bid_quantity = config_.quote_size;
  }

  if (inventory() > -config_.max_inventory) {
    quote.has_ask = true;
    quote.ask_price = RoundToTick(*mid + half_spread);
    quote.ask_quantity = config_.quote_size;
  }

  return quote;
}

}  // namespace lob::mm
