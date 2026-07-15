#pragma once

#include "lob/mm/avellaneda_stoikov_maker.hpp"

namespace lob::mm {

// beta: sensitivity of the reservation-price skew to order-flow
// imbalance. Larger beta means the strategy leans harder into anticipated
// short-term price moves implied by resting-order imbalance.
struct OfiMakerConfig {
  AvellanedaStoikovConfig as_config;
  double ofi_beta = 1.0;
};

// Extends AvellanedaStoikovMaker with an order-flow-imbalance (OFI) skew
// on top of the same reservation price and half-spread:
//
//   ofi = (bid_qty' - ask_qty') / (bid_qty' + ask_qty')   -- in [-1, 1]
//   r_ofi = r_AS + beta * ofi
//   bid = r_ofi - half_spread ; ask = r_ofi + half_spread
//
// `bid_qty'`/`ask_qty'` are the top-of-book quantities with our OWN
// resting quantity at that price excluded (MarketMaker::
// OwnRestingQuantityAt) -- without this, a strategy quoting the best
// level would bias the very signal it's reacting to (its own resting
// size would always read as "buy pressure" or "sell pressure" regardless
// of what anyone else is doing). If neither side has any external
// quantity (bid_qty' + ask_qty' == 0), OFI falls back to plain AS (no
// skew term) rather than computing 0/0.
//
// Positive OFI (more external resting buy pressure than sell) predicts a
// near-term price rise: raising r raises the ask (charging more right
// before the anticipated move -- the adverse-selection defense) but also
// raises the bid, which is a real, separate directional-positioning
// effect, not adverse-selection defense alone.
class OfiMaker : public AvellanedaStoikovMaker {
 public:
  OfiMaker(OfiMakerConfig config, Timestamp horizon);

 protected:
  Quote ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp now) override;

 private:
  double ofi_beta_;
};

}  // namespace lob::mm
