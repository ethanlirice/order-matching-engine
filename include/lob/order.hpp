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
// fields. Fields are ordered hot-first (everything the matching loop
// touches) with timestamp -- informational only, never read by matching --
// last. alignas(64) pads each Order to a full cache line: with pool-backed
// storage (OrderPool) placing Orders contiguously, this keeps two different
// orders from ever sharing a cache line, eliminating a false-sharing risk
// class outright once multiple threads touch the pool (M4's ring-buffer/
// matching-thread split) at the cost of some memory (PROJECT_SPEC.md §6).
struct alignas(64) Order {
  OrderId id = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;
  Quantity quantity = 0;

  Order* prev = nullptr;
  Order* next = nullptr;

  Timestamp timestamp = 0;
};
static_assert(sizeof(Order) == 64, "Order must occupy exactly one cache line");
static_assert(alignof(Order) == 64, "Order must be cache-line aligned");

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
