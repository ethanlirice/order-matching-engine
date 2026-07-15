#pragma once

#include <cstdint>

#include "lob/order.hpp"
#include "lob/order_book.hpp"
#include "lob/order_command.hpp"

namespace lob::sim {

// Narrow, read-only view of top-of-book state handed to a Strategy --
// never OrderBook itself (PROJECT_SPEC.md §4: L2 must not know how the
// engine stores orders). Quantities are total resting size at the best
// price (M5: top-of-book buy/sell pressure for the OFI strategy).
struct BookSnapshot {
  bool has_bid = false;
  Price best_bid = 0;
  Quantity best_bid_quantity = 0;
  bool has_ask = false;
  Price best_ask = 0;
  Quantity best_ask_quantity = 0;
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
//
// `event_ordinal` is Simulator's monotonic per-processed-event counter
// (see market_data_log.hpp) -- strategies that log fills for later
// markout/effective-spread analysis need this to correlate a fill with
// MarketDataLog's as-of queries, which are keyed on (timestamp,
// event_ordinal) rather than timestamp alone.
class Strategy {
 public:
  virtual ~Strategy() = default;

  virtual void OnBookUpdate(const BookSnapshot& snapshot, Timestamp now,
                            std::uint64_t event_ordinal, OrderIntentSink& intents) = 0;
  virtual void OnTrade(const TradeEvent& trade, Timestamp now, std::uint64_t event_ordinal,
                       OrderIntentSink& intents) = 0;

  // Fired right after every Submit/Modify the strategy itself issued
  // actually applies to the book (before OnTrade/OnBookUpdate) -- gives
  // ground truth on what happened, since a PostOnly modify-to-a-crossing-
  // price can silently cancel the original order AND reject the
  // replacement (both gone) with no other signal. Default no-op: opt-in
  // for strategies that care about tracking their own resting state
  // reliably (all of M5's market makers do; a hypothetical simpler
  // strategy that always re-derives everything fresh doesn't have to).
  virtual void OnOrderAck(OrderId /*id*/, const AddOrderResult& /*result*/) {}
};

}  // namespace lob::sim
