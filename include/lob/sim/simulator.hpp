#pragma once

#include <cstdint>
#include <vector>

#include "lob/matching_engine.hpp"
#include "lob/order.hpp"
#include "lob/sim/event.hpp"
#include "lob/sim/id_space.hpp"
#include "lob/sim/strategy.hpp"
#include "lob/sim/virtual_clock.hpp"

namespace lob::sim {

// Drives a MatchingEngine through a stream of scheduled Events in strict
// virtual-time order (PROJECT_SPEC.md §7), optionally with a Strategy
// attached. Deliberately no Execute message type: see
// include/lob/sim/replay_message.hpp for why "directly apply a recorded
// historical execution" would be wrong once a strategy order can be
// interposed between historical makers, and what the correct approach for
// real L3 data (a future milestone) is instead.
//
// Callback-firing rule (uniform regardless of event source): after every
// engine-state-changing call, OnTrade fires once per TradeEvent in fill
// order, followed by exactly one OnBookUpdate reflecting the final
// post-event state.
//
// Self-trade policy: the engine has no owner concept (L1 must not know
// about strategies) and this design adds no server-side self-trade
// prevention -- a strategy that would cross its own resting order is
// responsible for avoiding that itself, matching how many real venues
// don't offer STP by default.
class Simulator : public OrderIntentSink {
 public:
  // `latency` is the fixed configurable delay (PROJECT_SPEC.md §7) applied
  // to every strategy-issued Submit/Cancel/Modify -- the order only
  // actually reaches the book at now() + latency. `strategy` may be
  // nullptr for pure-replay use (no callbacks fire; trade_log() and
  // DebugBook() still work).
  explicit Simulator(Strategy* strategy = nullptr, Timestamp latency = 0)
      : strategy_(strategy), latency_(latency) {}

  // Adds events to the internal queue; safe to call multiple times,
  // including between Run() calls (e.g. to checkpoint intermediate state
  // in tests).
  void LoadEvents(std::vector<Event> events);

  // Drains whatever is currently queued, applying each event to the
  // engine in strict (timestamp, kind, sequence) order and advancing the
  // virtual clock as it goes. A Strategy's own Submit/Cancel/Modify calls
  // (made from within OnBookUpdate/OnTrade) push new events onto the same
  // queue, so they compete fairly for chronological position with
  // whatever's still queued.
  void Run();

  // OrderIntentSink overrides: schedule a StrategyOrderArrival event at
  // now() + latency_. See id_space.hpp -- Submit assigns the new order an
  // id from the disjoint strategy range and returns it immediately (only
  // the id assignment is synchronous; the order itself is still delayed).
  OrderId Submit(NewOrderCommand command) override;
  void Cancel(OrderId id) override;
  void Modify(OrderId id, Price new_price, Quantity new_quantity) override;

  // -- Read-only test/debug accessors --
  const OrderBook& DebugBook() const;
  const std::vector<TradeEvent>& trade_log() const { return trade_log_; }
  Timestamp now() const { return clock_.now(); }

 private:
  void ApplyReplayMessage(const ReplayMessage& message);
  void ApplyStrategyIntent(const StrategyIntent& intent);
  void HandleTrades(const std::vector<TradeEvent>& trades);
  Event MakeStrategyEvent(StrategyIntent intent);

  VirtualClock clock_;
  EventQueue queue_;
  MatchingEngine engine_;
  std::vector<TradeEvent> trade_log_;
  Strategy* strategy_ = nullptr;
  Timestamp latency_ = 0;
  OrderId next_strategy_id_ = kStrategyIdBase;
  std::uint64_t next_sequence_ = 0;
};

}  // namespace lob::sim
