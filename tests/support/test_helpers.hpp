#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "lob/order.hpp"
#include "lob/order_book.hpp"

// Shared, header-only test support used by the GoogleTest suite and the
// libFuzzer harness alike -- kept dependency-free (no gtest) so both can
// include it.
namespace lob::testing {

inline Order MakeOrder(OrderId id, Side side, OrderType type, Price price, Quantity quantity,
                       Timestamp timestamp = 0) {
  Order order;
  order.id = id;
  order.side = side;
  order.type = type;
  order.price = price;
  order.quantity = quantity;
  order.timestamp = timestamp;
  return order;
}

// Checks the PROJECT_SPEC.md §5.5 invariants plus one structural invariant
// (order_index_'s size matches the total node count reachable by walking
// every level, and each reachable order's side/price matches the map/level
// it's stored under) that catches intrusive-list bugs the other five
// don't. Returns true iff every invariant holds; on failure, writes a
// description of the first violation found to *reason (if non-null).
//
// "No self-cross" is not checked independently here: there is no
// trader/owner-id concept on Order in this model, so it collapses to
// "book never crosses" -- already covered by the best-bid-less-than-
// best-ask check below. Revisit if a trader id is ever added (true
// self-trade prevention is a distinct mechanism).
inline bool CheckInvariants(const OrderBook& book, std::string* reason = nullptr) {
  auto fail = [&](const char* msg) {
    if (reason != nullptr) {
      *reason = msg;
    }
    return false;
  };

  Price best_bid_price = 0;
  Price best_ask_price = 0;
  bool has_bid = book.best_bid(best_bid_price);
  bool has_ask = book.best_ask(best_ask_price);
  if (has_bid && has_ask && !(best_bid_price < best_ask_price)) {
    return fail("book crossed: best_bid >= best_ask");
  }

  std::size_t reachable = 0;
  for (Price price : book.bid_prices()) {
    std::vector<OrderId> ids = book.resting_order_ids(Side::Buy, price);
    if (ids.empty()) {
      return fail("empty bid level was not removed from the book");
    }
    for (OrderId id : ids) {
      const Order* order = book.debug_peek(id);
      if (order == nullptr) {
        return fail("resting bid id not found in order index");
      }
      if (order->side != Side::Buy || order->price != price) {
        return fail("resting bid order's side/price does not match its level");
      }
      ++reachable;
    }
  }
  for (Price price : book.ask_prices()) {
    std::vector<OrderId> ids = book.resting_order_ids(Side::Sell, price);
    if (ids.empty()) {
      return fail("empty ask level was not removed from the book");
    }
    for (OrderId id : ids) {
      const Order* order = book.debug_peek(id);
      if (order == nullptr) {
        return fail("resting ask id not found in order index");
      }
      if (order->side != Side::Sell || order->price != price) {
        return fail("resting ask order's side/price does not match its level");
      }
      ++reachable;
    }
  }

  if (reachable != book.order_count()) {
    return fail("order_count() does not match the number of reachable resting orders");
  }

  return true;
}

}  // namespace lob::testing
