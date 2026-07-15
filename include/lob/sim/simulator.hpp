#pragma once

#include <vector>

#include "lob/matching_engine.hpp"
#include "lob/order.hpp"
#include "lob/sim/event.hpp"
#include "lob/sim/virtual_clock.hpp"

namespace lob::sim {

// Drives a MatchingEngine through a stream of scheduled Events in strict
// virtual-time order (PROJECT_SPEC.md §7). M4 Step 2: pure replay only (no
// strategy yet -- that lands in Step 3, which adds StrategyOrderArrival
// handling here). Deliberately no Execute message type: see
// include/lob/sim/replay_message.hpp for why "directly apply a recorded
// historical execution" would be wrong once a strategy order can be
// interposed between historical makers, and what the correct approach for
// real L3 data (a future milestone) is instead.
class Simulator {
 public:
  Simulator() = default;

  // Adds events to the internal queue; safe to call multiple times,
  // including between Run() calls (e.g. to checkpoint intermediate state
  // in tests).
  void LoadEvents(std::vector<Event> events);

  // Drains whatever is currently queued, applying each event to the
  // engine in strict (timestamp, kind, sequence) order and advancing the
  // virtual clock as it goes.
  void Run();

  // -- Read-only test/debug accessors --
  const OrderBook& DebugBook() const;
  const std::vector<TradeEvent>& trade_log() const { return trade_log_; }
  Timestamp now() const { return clock_.now(); }

 private:
  void ApplyReplayMessage(const ReplayMessage& message);

  VirtualClock clock_;
  EventQueue queue_;
  MatchingEngine engine_;
  std::vector<TradeEvent> trade_log_;
};

}  // namespace lob::sim
