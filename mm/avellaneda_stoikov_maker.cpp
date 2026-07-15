#include "lob/mm/avellaneda_stoikov_maker.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "lob/sim/time_utils.hpp"

namespace lob::mm {

AvellanedaStoikovMaker::AvellanedaStoikovMaker(AvellanedaStoikovConfig config, Timestamp horizon)
    : config_(config), horizon_(horizon) {
  assert(config_.gamma >= 0.0);
  assert(config_.kappa > 0.0);
  assert(config_.sigma >= 0.0);
}

std::optional<AvellanedaStoikovMaker::ReservationAndSpread>
AvellanedaStoikovMaker::ComputeReservationAndHalfSpread(const sim::BookSnapshot& snapshot,
                                                        Timestamp now) const {
  std::optional<double> mid = ReferenceMid(snapshot);
  if (!mid.has_value()) {
    return std::nullopt;
  }

  double tau = sim::TimeRemaining(horizon_, now);
  double variance_term = config_.gamma * config_.sigma * config_.sigma * tau;

  double reservation_price = *mid - static_cast<double>(inventory()) * variance_term;

  // (1/gamma) * ln(1 + gamma/kappa), computed via log1p to avoid
  // cancellation for small gamma/kappa, special-cased at the gamma -> 0
  // limit (1/kappa) to avoid a division by zero.
  double adverse_selection_term = config_.gamma == 0.0
                                      ? 1.0 / config_.kappa
                                      : std::log1p(config_.gamma / config_.kappa) / config_.gamma;
  double half_spread = variance_term / 2.0 + adverse_selection_term;
  half_spread = std::max(half_spread, 1.0);

  return ReservationAndSpread{reservation_price, half_spread};
}

Quote AvellanedaStoikovMaker::ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp now) {
  Quote quote;
  std::optional<ReservationAndSpread> rs = ComputeReservationAndHalfSpread(snapshot, now);
  if (!rs.has_value()) {
    return quote;
  }

  quote.has_bid = true;
  quote.bid_price = RoundToTick(rs->reservation_price - rs->half_spread);
  quote.bid_quantity = config_.quote_size;

  quote.has_ask = true;
  quote.ask_price = RoundToTick(rs->reservation_price + rs->half_spread);
  quote.ask_quantity = config_.quote_size;

  return quote;
}

}  // namespace lob::mm
