#include "lob/sim/market_data_log.hpp"

#include <gtest/gtest.h>

namespace lob::sim {
namespace {

TEST(MarketDataLogTest, EmptyLogReturnsNulloptForAnyQuery) {
  MarketDataLog log;
  EXPECT_FALSE(log.MidAsOf(100, 0).has_value());
  EXPECT_FALSE(log.MidStrictlyBefore(100, 0).has_value());
}

TEST(MarketDataLogTest, SingleSampleBoundaries) {
  MarketDataLog log;
  log.Record(/*timestamp=*/100, /*event_ordinal=*/5, true, 99, true, 101);  // mid = 100.0

  EXPECT_FALSE(log.MidAsOf(50, 0).has_value()) << "before any sample";
  ASSERT_TRUE(log.MidAsOf(100, 5).has_value()) << "at-or-before is inclusive of an exact match";
  EXPECT_DOUBLE_EQ(*log.MidAsOf(100, 5), 100.0);
  ASSERT_TRUE(log.MidAsOf(200, 0).has_value());
  EXPECT_DOUBLE_EQ(*log.MidAsOf(200, 0), 100.0);

  EXPECT_FALSE(log.MidStrictlyBefore(100, 5).has_value())
      << "nothing precedes the only sample at its own key";
  ASSERT_TRUE(log.MidStrictlyBefore(200, 0).has_value());
  EXPECT_DOUBLE_EQ(*log.MidStrictlyBefore(200, 0), 100.0);
}

TEST(MarketDataLogTest, PicksLatestSampleAtOrBeforeAcrossMultipleTimestamps) {
  MarketDataLog log;
  log.Record(100, 0, true, 98, true, 102);   // mid = 100.0
  log.Record(200, 1, true, 108, true, 112);  // mid = 110.0
  log.Record(300, 2, true, 118, true, 122);  // mid = 120.0

  ASSERT_TRUE(log.MidAsOf(250, 0).has_value());
  EXPECT_DOUBLE_EQ(*log.MidAsOf(250, 0), 110.0);

  ASSERT_TRUE(log.MidStrictlyBefore(200, 1).has_value())
      << "strictly-before at exactly the second sample's key must return the first";
  EXPECT_DOUBLE_EQ(*log.MidStrictlyBefore(200, 1), 100.0);
}

TEST(MarketDataLogTest, SameTimestampOrderedByEventOrdinal) {
  MarketDataLog log;
  log.Record(100, 0, true, 98, true, 102);   // mid = 100.0
  log.Record(100, 1, true, 108, true, 112);  // mid = 110.0 -- later event, same tick

  ASSERT_TRUE(log.MidStrictlyBefore(100, 1).has_value());
  EXPECT_DOUBLE_EQ(*log.MidStrictlyBefore(100, 1), 100.0)
      << "must resolve via event_ordinal, not just timestamp, at the same tick";
  ASSERT_TRUE(log.MidAsOf(100, 0).has_value());
  EXPECT_DOUBLE_EQ(*log.MidAsOf(100, 0), 100.0);
}

TEST(MarketDataLogTest, InvalidNearestSampleReturnsNulloptRatherThanSearchingFurtherBack) {
  MarketDataLog log;
  log.Record(100, 0, true, 98, true, 102);  // valid, mid = 100.0
  log.Record(200, 1, true, 150, false, 0);  // one-sided (no ask) -- invalid

  // The nearest sample at-or-before t=250 is the invalid one at t=200; a
  // stale mid from t=100 is deliberately not substituted for it.
  EXPECT_FALSE(log.MidAsOf(250, 0).has_value());
}

}  // namespace
}  // namespace lob::sim
