#pragma once

#include <cstdint>

namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;  // integer ticks, not floating point
using Quantity = std::uint64_t;
using Timestamp = std::uint64_t;  // simulated/virtual clock ticks

enum class Side { Buy, Sell };

enum class OrderType { Limit, Market, IOC, FOK, PostOnly };

// Intrusive-list linkage (owned/used by Level) plus the order's economic
// fields. No behavior here yet -- matching logic lands in M1.
struct Order {
  OrderId id = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;
  Quantity quantity = 0;
  Timestamp timestamp = 0;

  Order* prev = nullptr;
  Order* next = nullptr;
};

// Emitted on every fill. Feeds L3/L4 (simulator, strategies, analytics).
struct TradeEvent {
  Price price = 0;
  Quantity size = 0;
  Side aggressor_side = Side::Buy;
  OrderId maker_order_id = 0;
  OrderId taker_order_id = 0;
  Timestamp timestamp = 0;
};

}  // namespace lob
