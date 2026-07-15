#pragma once

#include "lob/order.hpp"

namespace lob::sim {

// Disjoint id-namespace partition between historical/generator-issued
// order ids and strategy-issued order ids, sharing one OrderBook. Without
// this, a colliding id would silently hit OrderBook::add_order's
// duplicate-id rejection and drop a strategy order with no error signal.
// The synthetic generator assigns ids starting at 1; Simulator assigns
// strategy ids starting here, well clear of any realistic generation
// count.
inline constexpr OrderId kStrategyIdBase = OrderId{1} << 63;

}  // namespace lob::sim
