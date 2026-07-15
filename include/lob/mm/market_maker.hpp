#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "lob/order.hpp"
#include "lob/order_book.hpp"
#include "lob/order_command.hpp"
#include "lob/sim/strategy.hpp"

namespace lob::mm {

// Signed position -- can go short. L4-only concept; Order/Quantity stay
// L1-pure (unsigned).
using Inventory = std::int64_t;

// Explicit, consistent rounding rule for converting a continuous price
// (mid, reservation price, etc. -- always computed as double throughout
// this module to avoid systematic truncation bias) onto the integer tick
// grid, used at the one point each strategy actually constructs an order.
// Truncates toward zero; prices here are always positive in practice.
inline Price RoundToTick(double price) {
  return static_cast<Price>(price);
}

// A strategy's desired quotes for this tick. has_bid/has_ask == false
// means "don't quote this side right now" (e.g. inventory cap hit).
struct Quote {
  bool has_bid = false;
  Price bid_price = 0;
  Quantity bid_quantity = 0;
  bool has_ask = false;
  Price ask_price = 0;
  Quantity ask_quantity = 0;
};

// One fill of our own resting order (PROJECT_SPEC.md §8's metrics need
// this log).
struct Fill {
  Timestamp timestamp = 0;
  std::uint64_t event_ordinal = 0;
  Side side = Side::Buy;  // our side: Buy = we bought, Sell = we sold
  Price price = 0;
  Quantity quantity = 0;
  Inventory inventory_after = 0;
};

// Shared market-making mechanics for all of M5's strategies. Concrete
// strategies only implement ComputeQuotes; this handles:
//  - Reconciliation: Submit if nothing's resting on a side, Modify if the
//    desired price/quantity changed, Cancel if the side should stop
//    quoting.
//  - Fill attribution via OnTrade's maker_order_id -- safe because every
//    quote here is OrderType::PostOnly, guaranteeing every fill is maker-
//    side (never an ambiguous partial-fill-then-rest, never our own order
//    becoming a taker). PostOnly also structurally prevents this class
//    from ever wash-trading against its own resting order on the other
//    side: PostOnly's crossing check is against the book's current best
//    price regardless of whose order that is, so if our own ask is
//    currently the tightest, a bid priced at or through it is rejected
//    the same as it would be against anyone else's order.
//  - Resting-state ground truth via OnOrderAck -- never assumed static
//    between reconciliation passes, since a PostOnly modify-to-a-crossing-
//    price can silently cancel the original order and reject the
//    replacement (both gone).
//  - Inventory/cash/fill-log bookkeeping.
//
// Note on self-reference: since a strategy's own resting order can itself
// become the best bid/ask, ComputeQuotes's view of "mid" can be influenced
// by the strategy's own prior quotes while a Submit/Modify is still
// settling. Two distinct problems follow from this:
//  1. If the two sides' Submit/Modify actions were allowed to settle
//     asynchronously and independently, bid's desired price could be
//     computed from a snapshot where ask hadn't yet reflected an
//     already-issued (but not yet applied) ask Modify, and vice versa --
//     letting one side's Modify cross the OTHER side's own still-resting
//     order once it finally applied. PostOnly rejects a crossing Modify
//     by cancelling the original and rejecting the replacement (both
//     gone) -- and worse, chasing this repeatedly (as a strategy reacts
//     to its own churn) can cycle forever rather than converge. The fix:
//     at most ONE Submit/Modify is ever in flight across BOTH sides at a
//     time (see action_pending_ below) -- whenever a new decision is
//     made, both sides' resting_price/has_resting are guaranteed to
//     reflect the actual, fully-settled book state, so a self-cross is
//     never even computed, let alone attempted.
//  2. When a strategy's own order is the ENTIRE quantity resting at the
//     best price on a side, a mid computed from that price is circular:
//     with no inventory skew (inventory == 0), a constant half-spread
//     strategy's bid/ask satisfy bid = mid - hs, ask = mid + hs for ANY
//     mid, so the fixed-point equation has no unique solution -- there is
//     no restoring force pulling mid back to a "true" external value, and
//     RoundToTick's toward-zero truncation can then add a small
//     systematic bias each settling round. ReferenceMid (below) is the
//     fix: it reports the true book mid only when at least one side's
//     best price is verifiably not entirely our own resting order, and
//     otherwise holds the last such verified mid steady rather than
//     feeding a self-referential value back into ComputeQuotes.
//
// Two more failure modes surface once a strategy is driven by continuous
// real order flow (not just a couple of static seed orders) rather than
// self-reference alone:
//  3. A strategy's own reservation-price math can legitimately swing far
//     enough after just one or two fills (e.g. AS/OFI's inventory skew,
//     which scales with gamma*sigma^2*tau -- large for a long remaining
//     horizon) that the resulting quote would cross real external
//     liquidity. PostOnly rejects a crossing Submit/Modify -- but with
//     nothing else having changed, the very next ComputeQuotes call
//     reproduces the exact same crossing price, which without a guard is
//     a genuine reject-resubmit infinite loop, not just a missed quote.
//     OnBookUpdate clamps the desired quote against the raw external
//     market (never at or through the current opposing best price)
//     before reconciling.
//  4. A signal that depends on whether our own order shares a price level
//     with real external liquidity or rests alone (e.g. OFI's excluded
//     quantity, via OwnRestingQuantityAt) can be discontinuous exactly at
//     that boundary: moving to join external liquidity introduces an
//     imbalance signal that computes a price avoiding that level, but
//     that very avoidance removes the signal that caused the move,
//     producing a limit cycle that never converges -- observed as both a
//     2-tick cycle and a longer 3-tick one (10012 -> 10013 -> 10014 ->
//     10012 -> ...), so detecting the specific reversal pattern isn't
//     general enough. The fix is a per-tick circuit breaker instead:
//     SideState counts consecutive Modifies issued for that side at the
//     same virtual timestamp (`now`), and once that count reaches
//     kMaxConsecutiveModifiesPerTick, further Modifies for that side are
//     suppressed (holding its current resting price) until `now` actually
//     advances. A genuinely convergent settling cascade (geometric
//     decay, see #2) reaches a fixed point in a small number of
//     iterations, well under the cap; only a non-converging cycle -- of
//     any period -- ever hits it. The count resets as soon as `now`
//     changes, so this only ever suppresses same-tick self-churn, never
//     a strategy's response to genuinely new information.
class MarketMaker : public sim::Strategy {
 public:
  void OnBookUpdate(const sim::BookSnapshot& snapshot, Timestamp now, std::uint64_t event_ordinal,
                    sim::OrderIntentSink& intents) override;
  void OnTrade(const TradeEvent& trade, Timestamp now, std::uint64_t event_ordinal,
               sim::OrderIntentSink& intents) override;
  void OnOrderAck(OrderId id, const AddOrderResult& result) override;

  Inventory inventory() const { return inventory_; }
  double cash() const { return cash_; }
  const std::vector<Fill>& fills() const { return fills_; }

 protected:
  virtual Quote ComputeQuotes(const sim::BookSnapshot& snapshot, Timestamp now) = 0;

  // The book mid, guarded against self-reference (see the class comment).
  // nullopt if there's no trustworthy mid available yet (either the book
  // isn't two-sided, or it is but only because of our own resting orders
  // and no external mid has ever been observed). Concrete strategies
  // should use this instead of computing (best_bid + best_ask) / 2.0
  // directly from the snapshot.
  std::optional<double> ReferenceMid(const sim::BookSnapshot& snapshot) const;

  // Our own currently-resting quantity if it's at exactly `price` on
  // `side`, else 0. For a strategy computing a signal from a raw book
  // quantity (e.g. OFI's imbalance ratio), the naive quantity at our own
  // best price includes our own order -- this is what lets a strategy
  // exclude it and react only to genuinely external volume.
  Quantity OwnRestingQuantityAt(Side side, Price price) const {
    const SideState& state = (side == Side::Buy) ? bid_ : ask_;
    return (state.has_resting && state.resting_price == price) ? state.resting_quantity : 0;
  }

 private:
  struct SideState {
    bool has_resting = false;
    OrderId order_id = 0;
    Price resting_price = 0;
    Quantity resting_quantity = 0;

    // Per-tick circuit breaker state -- see the class comment's #4.
    int consecutive_modifies = 0;
    Timestamp modify_streak_time = 0;
  };

  static constexpr int kMaxConsecutiveModifiesPerTick = 20;

  // Submits/modifies this side toward the desired price/quantity if it
  // doesn't already match. Returns true iff it issued a Submit or Modify
  // (setting action_pending_) -- the caller must not issue another action
  // this round if so, since only one may be in flight globally at a time.
  bool TryRequoteSide(SideState& state, Price desired_price, Quantity desired_quantity, Side side,
                      Timestamp now, sim::OrderIntentSink& intents);
  void ApplyAck(SideState& state, OrderId id, const AddOrderResult& result);
  void ApplyFill(SideState& state, Side side, const TradeEvent& trade, std::uint64_t event_ordinal);

  SideState bid_;
  SideState ask_;
  Inventory inventory_ = 0;
  double cash_ = 0.0;
  std::vector<Fill> fills_;

  // At most one Submit/Modify in flight across BOTH sides at a time (see
  // the class comment). Cancels aren't gated by this: Simulator never
  // fires OnOrderAck for a Cancel (nothing to wait on), and a cancelled
  // side's has_resting is cleared immediately/optimistically, same as
  // before this gate existed.
  bool action_pending_ = false;

  mutable bool has_reference_mid_ = false;
  mutable double reference_mid_ = 0.0;
};

}  // namespace lob::mm
