#include "lob/order_book.hpp"

namespace lob {

OrderBook::OrderBook() = default;

void OrderBook::add_order(const Order& /*order*/) {
  // Matching + resting logic: implemented in M1.
}

void OrderBook::cancel_order(OrderId /*id*/) {
  // O(1) cancel via order_index_: implemented in M1.
}

void OrderBook::modify_order(OrderId /*id*/, Price /*new_price*/, Quantity /*new_quantity*/) {
  // cancel + re-insert-at-tail: implemented in M1.
}

bool OrderBook::best_bid(Price& /*out_price*/) const {
  // implemented in M1.
  return false;
}

bool OrderBook::best_ask(Price& /*out_price*/) const {
  // implemented in M1.
  return false;
}

}  // namespace lob
