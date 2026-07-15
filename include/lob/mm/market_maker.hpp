#pragma once

#include <cstdint>
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
//    quoting. At most one unacknowledged action per side at a time --
//    avoids a real race where a second action could be issued against an
//    id whose fate (rested vs. rejected) the base class doesn't know yet.
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
// by the strategy's own prior quotes while a Submit/Modify is settling.
// Empirically (and by construction, since each side's move only shifts
// mid by half its delta) this is a damped, self-correcting transient of a
// handful of extra Modify calls while a side is first established, not an
// unbounded cascade -- the pending_ack gate caps concurrent in-flight
// actions per side to one, so it can't runaway regardless.
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

 private:
  struct SideState {
    bool has_resting = false;
    bool pending_ack = false;
    OrderId order_id = 0;
    Price resting_price = 0;
    Quantity resting_quantity = 0;
  };

  void RequoteSide(SideState& state, bool desired_has_quote, Price desired_price,
                   Quantity desired_quantity, Side side, Timestamp now,
                   sim::OrderIntentSink& intents);
  void ApplyAck(SideState& state, OrderId id, const AddOrderResult& result);
  void ApplyFill(SideState& state, Side side, const TradeEvent& trade, std::uint64_t event_ordinal);

  SideState bid_;
  SideState ask_;
  Inventory inventory_ = 0;
  double cash_ = 0.0;
  std::vector<Fill> fills_;
};

}  // namespace lob::mm
