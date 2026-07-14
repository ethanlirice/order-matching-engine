#pragma once

#include <functional>
#include <map>
#include <unordered_map>

#include "lob/level.hpp"
#include "lob/order.hpp"

namespace lob {

// Single-symbol limit order book: two sides (bids, asks), each a price ->
// Level map ordered so the best bid is the highest price and the best ask
// is the lowest. An order_id -> Order* hash map gives O(1) cancel/modify
// lookups. Interface only for M0 -- add/cancel/modify/match semantics
// (price-time priority, order-type handling) land in M1-M2.
class OrderBook {
 public:
  OrderBook();

  // Adds a new order. Aggressive (crossing) orders are matched against the
  // opposing side; any remainder rests, is cancelled, or is rejected
  // depending on order type. No-op stub for M0.
  void add_order(const Order& order);

  // Cancels a resting order by id. No-op stub for M0.
  void cancel_order(OrderId id);

  // Modify = cancel + re-insert at the tail of the (possibly new) price
  // level -- loses time priority. No-op stub for M0.
  void modify_order(OrderId id, Price new_price, Quantity new_quantity);

  bool best_bid(Price& out_price) const;
  bool best_ask(Price& out_price) const;

 private:
  std::map<Price, Level, std::greater<>> bids_;
  std::map<Price, Level> asks_;
  std::unordered_map<OrderId, Order*> order_index_;
};

}  // namespace lob
