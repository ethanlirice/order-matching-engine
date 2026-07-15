#pragma once

#include <optional>

#include "lob/mm/market_maker.hpp"

namespace lob::mm {

// gamma: risk aversion: sigma: mid-price volatility (per unit virtual
// time); kappa: order-book liquidity/depth parameter (higher kappa ->
// tighter spreads, matching a more liquid book). See AvellanedaStoikovMaker
// for the exact formula and the numerical-safety notes on each term.
struct AvellanedaStoikovConfig {
  double gamma = 0.1;
  double sigma = 1.0;
  double kappa = 1.5;
  Quantity quote_size = 10;
};

// Avellaneda-Stoikov (2008) market maker: skews its reservation price away
// from mid by inventory (so it's biased to unwind, not just harvest
// spread) and widens/narrows its spread by remaining time-to-horizon and
// book liquidity.
//
//   r = mid - inventory * gamma * sigma^2 * tau
//   half_spread = (gamma * sigma^2 * tau) / 2 + (1/gamma) * ln(1 + gamma/kappa)
//   bid = r - half_spread ; ask = r + half_spread
//
// `tau` is time remaining to `horizon` (an explicit constructor parameter,
// not derived from any one data source's own config -- keeps this
// strategy decoupled from, e.g., SyntheticGeneratorConfig).
//
// Numerical hardening:
//  - (1/gamma) * ln(1 + gamma/kappa) is computed as std::log1p(gamma/kappa)
//    / gamma to avoid catastrophic cancellation for small gamma/kappa, and
//    is special-cased to the real gamma->0 limit (1/kappa) to avoid a
//    division by zero.
//  - half_spread is floor-clamped to >= 1 tick: kappa -> infinity (or
//    tau == 0 with gamma small) mathematically drives it to 0, which
//    without server-side self-trade prevention would let this strategy
//    wash-trade against its own resting order on the other side. (PostOnly
//    already prevents that structurally, per MarketMaker's own doc
//    comment, but a non-degenerate spread is also just the intended
//    behavior of a market maker.)
//  - gamma < 0 or kappa <= 0 are programmer errors (asserted on
//    construction, not data conditions) -- the model isn't meaningful for
//    non-positive liquidity or negative risk aversion.
class AvellanedaStoikovMaker : public MarketMaker {
 public:
  AvellanedaStoikovMaker(AvellanedaStoikovConfig config, Timestamp horizon);

 protected:
  Quote ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp now) override;

  struct ReservationAndSpread {
    double reservation_price;
    double half_spread;
  };

  // The r/half_spread computation above, exposed so OfiMaker (which
  // extends this model with an order-flow-imbalance skew term on top of
  // the same reservation price) doesn't have to re-derive or duplicate
  // it. nullopt under the same condition ComputeQuotes returns an empty
  // Quote (no trustworthy mid yet -- see MarketMaker::ReferenceMid).
  std::optional<ReservationAndSpread> ComputeReservationAndHalfSpread(
      const sim::BookSnapshot& snapshot, Timestamp now) const;

  const AvellanedaStoikovConfig& config() const { return config_; }

 private:
  AvellanedaStoikovConfig config_;
  Timestamp horizon_;
};

}  // namespace lob::mm
