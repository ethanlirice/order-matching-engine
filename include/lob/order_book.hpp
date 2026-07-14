#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "lob/level.hpp"
#include "lob/order.hpp"

namespace lob {

// Result of add_order/modify_order: every unit of submitted quantity ends
// up in exactly one of filled/resting/cancelled (quantity conservation,
// PROJECT_SPEC.md §5.5).
struct AddOrderResult {
  std::vector<TradeEvent> trades;
  Quantity filled_quantity = 0;
  Quantity resting_quantity = 0;    // still in the book after this call
  Quantity cancelled_quantity = 0;  // dropped without resting
};

// Single-symbol limit order book: two sides (bids, asks), each a price ->
// Level map ordered so the best bid is the highest price and the best ask
// is the lowest. Interface only covers Limit and Market orders in M1;
// IOC/FOK/PostOnly are explicitly rejected until M2.
class OrderBook {
 public:
  OrderBook() = default;

  // Aggressive (crossing) orders walk the opposing book in price-time
  // priority; any remainder rests (Limit) or is dropped (Market). Rejects
  // (as fully cancelled) duplicate order ids and out-of-scope order types.
  AddOrderResult add_order(Order incoming);

  // Cancels a resting order by id. Returns its remaining quantity, or
  // nullopt if the id is unknown.
  std::optional<Quantity> cancel_order(OrderId id);

  // cancel + re-insert at the tail of the (possibly new) price level --
  // loses time priority. new_quantity == 0 is treated as a full cancel.
  // Returns nullopt if the id is unknown.
  std::optional<AddOrderResult> modify_order(OrderId id, Price new_price, Quantity new_quantity);

  bool best_bid(Price& out_price) const;
  bool best_ask(Price& out_price) const;

  // -- Read-only testability accessors (unit/fuzz/determinism tests only;
  // no strategy/simulator code should depend on these). --
  bool contains(OrderId id) const;
  const Order* debug_peek(OrderId id) const;
  std::vector<OrderId> resting_order_ids(Side side, Price price) const;
  std::vector<Price> bid_prices() const;
  std::vector<Price> ask_prices() const;
  std::size_t order_count() const;

 private:
  template <typename OppositeMap>
  void match_against(OppositeMap& opposite, Order& incoming, std::vector<TradeEvent>& trades,
                     Quantity& filled_quantity);

  template <typename LevelMap>
  void finalize_removal(LevelMap& side_map, typename LevelMap::iterator level_it, Order* order);

  // Sole owner of every resting Order's memory. Level and matching code
  // hold non-owning raw Order* into it. Safe under rehashing: a rehash only
  // moves the bucket array of unique_ptrs, never the heap-allocated Order
  // each one points to, so raw pointers held elsewhere stay valid.
  std::unordered_map<OrderId, std::unique_ptr<Order>> order_index_;
  std::map<Price, Level, std::greater<>> bids_;
  std::map<Price, Level> asks_;
};

}  // namespace lob
