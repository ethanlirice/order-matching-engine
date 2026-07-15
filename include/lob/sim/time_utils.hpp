#pragma once

#include <algorithm>

#include "lob/order.hpp"

namespace lob::sim {

// Safe `horizon - now`, clamped to >= 0.0. Timestamp is uint64_t --
// subtracting directly underflows to a huge positive number whenever
// `now > horizon`, which is a normal occurrence (e.g. a latency-delayed
// strategy event routinely lands past a configured session horizon), not
// an edge case, and casting an overflowed result into Price (int64_t)
// downstream would be undefined behavior if out of range.
inline double TimeRemaining(Timestamp horizon, Timestamp now) {
  return std::max(0.0, static_cast<double>(horizon) - static_cast<double>(now));
}

}  // namespace lob::sim
