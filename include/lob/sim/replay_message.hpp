#pragma once

#include "lob/order.hpp"
#include "lob/order_command.hpp"

namespace lob::sim {

// A historical/synthetic order-flow message: a new order arrives, an
// existing one is cancelled, or an existing one's resting quantity shrinks
// in place (Reduce -- e.g. LOBSTER's partial-cancellation event type;
// preserves FIFO priority via OrderBook::ReduceQuantity, unlike a
// cancel-and-re-add). Deliberately still no Execute message type -- see
// include/lob/sim/simulator.hpp's header comment for why "directly apply a
// recorded historical execution" is actively wrong once a strategy order
// can be interposed between historical makers, and what the correct
// approach for real L3 data is (synthesize an implicit aggressor Add and
// let the engine re-derive the match).
struct ReplayMessage {
  enum class Kind { Add, Cancel, Reduce };

  Kind kind = Kind::Add;
  NewOrderCommand add;          // valid iff kind == Add
  OrderId cancel_id = 0;        // valid iff kind == Cancel
  OrderId reduce_id = 0;        // valid iff kind == Reduce
  Quantity reduce_to_quantity = 0;  // valid iff kind == Reduce
};

}  // namespace lob::sim
