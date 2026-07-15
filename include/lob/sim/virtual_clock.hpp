#pragma once

#include <cassert>

#include "lob/order.hpp"

namespace lob::sim {

// All simulator timing is driven by this, never wall-clock (PROJECT_SPEC.md
// §7) -- required for reproducible runs. Tracks the last-processed event
// timestamp; advance_to() asserts monotonicity (time never moves backward).
class VirtualClock {
 public:
  Timestamp now() const { return current_; }

  void advance_to(Timestamp t) {
    assert(t >= current_ && "virtual clock must never move backward");
    current_ = t;
  }

 private:
  Timestamp current_ = 0;
};

}  // namespace lob::sim
