#pragma once

#include <stdexcept>

#include "lob/order.hpp"

namespace lob::sim {

// All simulator timing is driven by this, never wall-clock (PROJECT_SPEC.md
// §7) -- required for reproducible runs. Tracks the last-processed event
// timestamp; advance_to() enforces monotonicity (time never moves
// backward) unconditionally rather than via assert() -- this is a core
// correctness invariant the whole determinism guarantee depends on, not a
// debug-only sanity check, so it must not be silently compiled out by
// NDEBUG (which both RelWithDebInfo and Release define).
class VirtualClock {
 public:
  Timestamp now() const { return current_; }

  void advance_to(Timestamp t) {
    if (t < current_) {
      throw std::logic_error("VirtualClock: time must never move backward");
    }
    current_ = t;
  }

 private:
  Timestamp current_ = 0;
};

}  // namespace lob::sim
