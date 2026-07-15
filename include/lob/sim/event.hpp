#pragma once

#include <cstdint>
#include <queue>
#include <variant>
#include <vector>

#include "lob/order.hpp"
#include "lob/order_command.hpp"
#include "lob/sim/replay_message.hpp"

namespace lob::sim {

// A strategy's scheduled action, to be applied once its latency delay has
// elapsed (Simulator schedules these internally; Strategy itself never
// constructs or sees this type -- only through OrderIntentSink).
struct StrategyIntent {
  enum class Kind { Submit, Cancel, Modify };

  Kind kind = Kind::Submit;
  NewOrderCommand submit;        // valid iff kind == Submit
  OrderId target_id = 0;         // valid iff kind == Cancel or Modify
  Price modify_price = 0;        // valid iff kind == Modify
  Quantity modify_quantity = 0;  // valid iff kind == Modify
};

// Replay events sort before strategy-order-arrival events at an equal
// timestamp: a strategy action must never "jump ahead" of a historical
// event it couldn't have observed at the same virtual tick (no look-ahead
// bias). The underlying values (0, 1) are exactly the sort key, not just
// labels -- see EventOrder below.
enum class EventKind : std::uint8_t { Replay = 0, StrategyOrderArrival = 1 };

struct Event {
  Timestamp timestamp = 0;
  EventKind kind = EventKind::Replay;
  // Monotonically increasing, assigned at push time (by the synthetic
  // generator in generation order for Replay events, by the Simulator's
  // own counter for StrategyOrderArrival events). Never reassigned. This
  // is what makes the total order below strict and independent of
  // std::priority_queue's internal tie-breaking, which is otherwise
  // unspecified for equal keys -- a real determinism leak if relied upon.
  std::uint64_t sequence = 0;
  std::variant<ReplayMessage, StrategyIntent> payload;
};

// Strict total order: (timestamp, kind, sequence), ascending. Equal
// timestamps are broken by kind (Replay first), then by sequence (push
// order) -- never left to unspecified heap/container behavior.
struct EventOrder {
  bool operator()(const Event& a, const Event& b) const {
    if (a.timestamp != b.timestamp) {
      return a.timestamp > b.timestamp;
    }
    if (a.kind != b.kind) {
      return a.kind > b.kind;
    }
    return a.sequence > b.sequence;
  }
};

using EventQueue = std::priority_queue<Event, std::vector<Event>, EventOrder>;

}  // namespace lob::sim
