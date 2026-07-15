#include <gtest/gtest.h>

#include "lob/sim/event.hpp"
#include "lob/sim/virtual_clock.hpp"

namespace lob::sim {
namespace {

Event MakeReplayEvent(Timestamp t, std::uint64_t sequence, OrderId id) {
  Event event;
  event.timestamp = t;
  event.kind = EventKind::Replay;
  event.sequence = sequence;
  ReplayMessage message;
  message.kind = ReplayMessage::Kind::Cancel;
  message.cancel_id = id;
  event.payload = message;
  return event;
}

Event MakeStrategyEvent(Timestamp t, std::uint64_t sequence, OrderId id) {
  Event event;
  event.timestamp = t;
  event.kind = EventKind::StrategyOrderArrival;
  event.sequence = sequence;
  StrategyIntent intent;
  intent.kind = StrategyIntent::Kind::Cancel;
  intent.target_id = id;
  event.payload = intent;
  return event;
}

OrderId IdOf(const Event& event) {
  if (event.kind == EventKind::Replay) {
    return std::get<ReplayMessage>(event.payload).cancel_id;
  }
  return std::get<StrategyIntent>(event.payload).target_id;
}

TEST(EventOrderingTest, EarlierTimestampPopsFirst) {
  EventQueue queue;
  queue.push(MakeReplayEvent(200, 0, 1));
  queue.push(MakeReplayEvent(100, 1, 2));

  EXPECT_EQ(IdOf(queue.top()), 2u);
  queue.pop();
  EXPECT_EQ(IdOf(queue.top()), 1u);
}

TEST(EventOrderingTest, ReplaySortsBeforeStrategyAtEqualTimestamp) {
  EventQueue queue;
  // Push strategy event first to prove ordering doesn't depend on push
  // order or container-internal tie-breaking.
  queue.push(MakeStrategyEvent(100, 0, 1));
  queue.push(MakeReplayEvent(100, 1, 2));

  ASSERT_EQ(queue.top().kind, EventKind::Replay);
  EXPECT_EQ(IdOf(queue.top()), 2u);
  queue.pop();
  ASSERT_EQ(queue.top().kind, EventKind::StrategyOrderArrival);
  EXPECT_EQ(IdOf(queue.top()), 1u);
}

TEST(EventOrderingTest, SequencePreservesPushOrderAtEqualTimestampAndKind) {
  EventQueue queue;
  queue.push(MakeReplayEvent(100, 5, 1));
  queue.push(MakeReplayEvent(100, 2, 2));
  queue.push(MakeReplayEvent(100, 3, 3));

  EXPECT_EQ(IdOf(queue.top()), 2u);
  queue.pop();
  EXPECT_EQ(IdOf(queue.top()), 3u);
  queue.pop();
  EXPECT_EQ(IdOf(queue.top()), 1u);
}

TEST(VirtualClockTest, StartsAtZero) {
  VirtualClock clock;
  EXPECT_EQ(clock.now(), 0u);
}

TEST(VirtualClockTest, AdvancesToLaterTimestamp) {
  VirtualClock clock;
  clock.advance_to(100);
  EXPECT_EQ(clock.now(), 100u);
  clock.advance_to(100);  // advancing to the same timestamp is fine
  EXPECT_EQ(clock.now(), 100u);
  clock.advance_to(250);
  EXPECT_EQ(clock.now(), 250u);
}

TEST(VirtualClockDeathTest, MovingBackwardIsRejected) {
  VirtualClock clock;
  clock.advance_to(100);
  EXPECT_DEATH(clock.advance_to(50), "");
}

}  // namespace
}  // namespace lob::sim
