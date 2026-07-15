#include "lob/mm/ofi_maker.hpp"

namespace lob::mm {

OfiMaker::OfiMaker(OfiMakerConfig config, Timestamp horizon)
    : AvellanedaStoikovMaker(config.as_config, horizon), ofi_beta_(config.ofi_beta) {}

Quote OfiMaker::ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp now) {
  Quote quote;
  std::optional<ReservationAndSpread> rs = ComputeReservationAndHalfSpread(snapshot, now);
  if (!rs.has_value()) {
    return quote;
  }

  double reservation_price = rs->reservation_price;

  if (snapshot.has_bid && snapshot.has_ask) {
    Quantity own_bid = OwnRestingQuantityAt(Side::Buy, snapshot.best_bid);
    Quantity own_ask = OwnRestingQuantityAt(Side::Sell, snapshot.best_ask);
    Quantity external_bid_qty =
        snapshot.best_bid_quantity > own_bid ? snapshot.best_bid_quantity - own_bid : 0;
    Quantity external_ask_qty =
        snapshot.best_ask_quantity > own_ask ? snapshot.best_ask_quantity - own_ask : 0;

    double total = static_cast<double>(external_bid_qty) + static_cast<double>(external_ask_qty);
    if (total > 0.0) {
      double ofi =
          (static_cast<double>(external_bid_qty) - static_cast<double>(external_ask_qty)) / total;
      reservation_price += ofi_beta_ * ofi;
    }
  }

  quote.has_bid = true;
  quote.bid_price = RoundToTick(reservation_price - rs->half_spread);
  quote.bid_quantity = config().quote_size;

  quote.has_ask = true;
  quote.ask_price = RoundToTick(reservation_price + rs->half_spread);
  quote.ask_quantity = config().quote_size;

  return quote;
}

}  // namespace lob::mm
