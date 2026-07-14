#pragma once

#include "lob/order.hpp"

namespace lob {

// Cross-thread wire format for a new-order request (feed thread -> matching
// thread, via SpscRingBuffer). Deliberately NOT lob::Order: Order's
// prev/next are intrusive-list-only fields with no meaning outside a
// Level's list, and reusing a list node as a cross-thread DTO conflates
// two distinct concerns. Only the matching thread ever constructs a real
// Order (via OrderPool), built from a NewOrderCommand it received.
struct NewOrderCommand {
  OrderId id = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;
  Quantity quantity = 0;
  Timestamp timestamp = 0;
};

}  // namespace lob
