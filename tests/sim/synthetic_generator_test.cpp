#include "lob/sim/synthetic_generator.hpp"

#include <gtest/gtest.h>

#include "lob/sim/id_space.hpp"

namespace lob::sim {
namespace {

SyntheticGeneratorConfig TestConfig(std::uint64_t seed) {
  SyntheticGeneratorConfig config;
  config.seed = seed;
  config.duration = 10000;
  config.arrival_rate = 0.05;
  return config;
}

TEST(SyntheticGeneratorTest, SameSeedProducesByteIdenticalOutput) {
  SyntheticGenerator generator_a(TestConfig(42));
  SyntheticGenerator generator_b(TestConfig(42));

  std::vector<Event> events_a = generator_a.Generate();
  std::vector<Event> events_b = generator_b.Generate();

  ASSERT_EQ(events_a.size(), events_b.size());
  ASSERT_GT(events_a.size(), 0u);
  for (std::size_t i = 0; i < events_a.size(); ++i) {
    EXPECT_EQ(events_a[i].timestamp, events_b[i].timestamp);
    EXPECT_EQ(events_a[i].sequence, events_b[i].sequence);
    EXPECT_EQ(events_a[i].kind, events_b[i].kind);
    ASSERT_EQ(events_a[i].payload.index(), events_b[i].payload.index());
    const auto& message_a = std::get<ReplayMessage>(events_a[i].payload);
    const auto& message_b = std::get<ReplayMessage>(events_b[i].payload);
    EXPECT_EQ(message_a.kind, message_b.kind);
    if (message_a.kind == ReplayMessage::Kind::Add) {
      EXPECT_EQ(message_a.add.id, message_b.add.id);
      EXPECT_EQ(message_a.add.side, message_b.add.side);
      EXPECT_EQ(message_a.add.price, message_b.add.price);
      EXPECT_EQ(message_a.add.quantity, message_b.add.quantity);
    } else {
      EXPECT_EQ(message_a.cancel_id, message_b.cancel_id);
    }
  }
}

TEST(SyntheticGeneratorTest, DifferentSeedProducesDifferentOutput) {
  SyntheticGenerator generator_a(TestConfig(1));
  SyntheticGenerator generator_b(TestConfig(2));

  std::vector<Event> events_a = generator_a.Generate();
  std::vector<Event> events_b = generator_b.Generate();

  bool any_difference = events_a.size() != events_b.size();
  for (std::size_t i = 0; !any_difference && i < events_a.size(); ++i) {
    if (events_a[i].timestamp != events_b[i].timestamp) {
      any_difference = true;
    }
  }
  EXPECT_TRUE(any_difference);
}

TEST(SyntheticGeneratorTest, TimestampsAreNonDecreasing) {
  SyntheticGenerator generator(TestConfig(7));
  std::vector<Event> events = generator.Generate();

  ASSERT_GT(events.size(), 0u);
  for (std::size_t i = 1; i < events.size(); ++i) {
    EXPECT_GE(events[i].timestamp, events[i - 1].timestamp);
  }
}

TEST(SyntheticGeneratorTest, SequenceNumbersMatchGenerationOrder) {
  SyntheticGenerator generator(TestConfig(7));
  std::vector<Event> events = generator.Generate();

  for (std::size_t i = 0; i < events.size(); ++i) {
    EXPECT_EQ(events[i].sequence, i);
  }
}

TEST(SyntheticGeneratorTest, GeneratedIdsStayBelowStrategyIdSpace) {
  SyntheticGenerator generator(TestConfig(7));
  std::vector<Event> events = generator.Generate();

  for (const Event& event : events) {
    const auto& message = std::get<ReplayMessage>(event.payload);
    if (message.kind == ReplayMessage::Kind::Add) {
      EXPECT_LT(message.add.id, kStrategyIdBase);
    } else {
      EXPECT_LT(message.cancel_id, kStrategyIdBase);
    }
  }
}

TEST(SyntheticGeneratorTest, RespectsConfiguredDuration) {
  SyntheticGeneratorConfig config = TestConfig(3);
  config.duration = 500;
  SyntheticGenerator generator(config);
  std::vector<Event> events = generator.Generate();

  for (const Event& event : events) {
    EXPECT_LE(event.timestamp, config.duration);
  }
}

}  // namespace
}  // namespace lob::sim
