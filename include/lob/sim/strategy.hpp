#pragma once

#include "lob/order.hpp"
#include "lob/order_command.hpp"

namespace lob::sim {

// Narrow, read-only view of top-of-book state handed to a Strategy --
// never OrderBook itself (PROJECT_SPEC.md §4: L2 must not know how the
// engine stores orders).
struct BookSnapshot {
  bool has_bid = false;
  Price best_bid = 0;
  bool has_ask = false;
  Price best_ask = 0;
};

// The ONLY thing a Strategy can touch to act -- implemented by Simulator.
// Scheduling (not direct engine access) is what enforces the latency
// model: every action here is only actually applied after the configured
// delay, never synchronously mid-callback -- a Strategy holding a
// MatchingEngine reference directly would be able to bypass that.
class OrderIntentSink {
 public:
  virtual ~OrderIntentSink() = default;

  // `command.id` is ignored -- Simulator assigns strategy order ids
  // internally from the disjoint id-space partition (id_space.hpp) and
  // returns the assigned id immediately so the strategy can track it for
  // a later Cancel/Modify. Only the id assignment is synchronous; the
  // order itself only actually reaches the book after the latency delay.
  virtual OrderId Submit(NewOrderCommand command) = 0;
  virtual void Cancel(OrderId id) = 0;
  virtual void Modify(OrderId id, Price new_price, Quantity new_quantity) = 0;
};

// L2 interface (architecture §4): on_book_update()/on_trade() -> emit
// orders. A plain virtual interface, not a template/CRTP -- strategies
// fire once per simulator event (coarse-grained), not on L1's per-
// operation matching hot path, so §6's "avoid virtual dispatch on the hot
// path" guidance doesn't apply here.
class Strategy {
 public:
  virtual ~Strategy() = default;

  virtual void OnBookUpdate(const BookSnapshot& snapshot, Timestamp now,
                            OrderIntentSink& intents) = 0;
  virtual void OnTrade(const TradeEvent& trade, Timestamp now, OrderIntentSink& intents) = 0;
};

}  // namespace lob::sim
