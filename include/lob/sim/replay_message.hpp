#pragma once

#include "lob/order.hpp"
#include "lob/order_command.hpp"

namespace lob::sim {

// A historical/synthetic order-flow message: either a new order arrives or
// an existing one is cancelled. Deliberately no Execute message type in
// this milestone -- see include/lob/sim/simulator.hpp's header comment for
// why "directly apply a recorded historical execution" is actively wrong
// once a strategy order can be interposed between historical makers, and
// what the correct approach for real L3 data (a future milestone) is.
struct ReplayMessage {
  enum class Kind { Add, Cancel };

  Kind kind = Kind::Add;
  NewOrderCommand add;    // valid iff kind == Add
  OrderId cancel_id = 0;  // valid iff kind == Cancel
};

}  // namespace lob::sim
