#include "lob/mm/naive_maker.hpp"

namespace lob::mm {

Quote NaiveMaker::ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp /*now*/) {
  Quote quote;
  if (!snapshot.has_bid || !snapshot.has_ask) {
    return quote;  // not enough book info yet to compute a mid
  }

  double mid =
      (static_cast<double>(snapshot.best_bid) + static_cast<double>(snapshot.best_ask)) / 2.0;
  double half_spread = static_cast<double>(config_.half_spread);

  quote.has_bid = true;
  quote.bid_price = RoundToTick(mid - half_spread);
  quote.bid_quantity = config_.quote_size;

  quote.has_ask = true;
  quote.ask_price = RoundToTick(mid + half_spread);
  quote.ask_quantity = config_.quote_size;

  return quote;
}

}  // namespace lob::mm
