#include "lob/sim/time_utils.hpp"

#include <gtest/gtest.h>

namespace lob::sim {
namespace {

TEST(TimeUtilsTest, ReturnsRemainingDurationWhenNowIsBeforeHorizon) {
  EXPECT_DOUBLE_EQ(TimeRemaining(100, 40), 60.0);
}

TEST(TimeUtilsTest, ReturnsZeroExactlyAtHorizon) {
  EXPECT_DOUBLE_EQ(TimeRemaining(100, 100), 0.0);
}

TEST(TimeUtilsTest, ClampsToZeroWhenNowIsPastHorizonRatherThanUnderflowing) {
  // Timestamp is unsigned -- a naive `horizon - now` would wrap to a huge
  // positive number here instead of going negative.
  EXPECT_DOUBLE_EQ(TimeRemaining(100, 150), 0.0);
}

TEST(TimeUtilsTest, HandlesLargeTimestampsWithinDoublePrecision) {
  // double represents every integer exactly up to 2^53 (~9e15) --
  // comfortably larger than any realistic simulation timestamp, so large
  // (but not adversarially close to UINT64_MAX) values still resolve
  // exactly. Values within a few ticks of UINT64_MAX itself are beyond
  // double's exact-integer range and unresolvable by construction -- not
  // a scenario TimeRemaining needs to handle precisely.
  Timestamp large = 1'000'000'000'000ULL;  // 10^12
  EXPECT_DOUBLE_EQ(TimeRemaining(large, large - 10), 10.0);
  EXPECT_DOUBLE_EQ(TimeRemaining(large - 10, large), 0.0);
}

}  // namespace
}  // namespace lob::sim
