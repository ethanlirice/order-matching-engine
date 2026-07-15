#include "lob/mm/naive_maker.hpp"

#include <gtest/gtest.h>

#include "lob/sim/simulator.hpp"
#include "sim/support/sim_test_helpers.hpp"

namespace lob::mm {
namespace {

TEST(NaiveMakerTest, QuotesSymmetricallyAroundMidWithAFixedSpread) {
  NaiveMakerConfig config;
  config.half_spread = 5;
  config.quote_size = 10;
  NaiveMaker maker(config);
  sim::Simulator simulator(&maker, /*latency=*/0);

  // mid = (90 + 110) / 2 = 100 -> bid=95, ask=105.
  simulator.LoadEvents({
      sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 90, 1000),
      sim::testing::MakeAddEvent(20, 1, 2, Side::Sell, 110, 1000),
  });
  simulator.Run();

  ASSERT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, 95).size(), 1u);
  ASSERT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, 105).size(), 1u);
  EXPECT_EQ(simulator.DebugBook().quantity_at(Side::Buy, 95), 10u);
  EXPECT_EQ(simulator.DebugBook().quantity_at(Side::Sell, 105), 10u);
}

TEST(NaiveMakerTest, AccumulatesInventoryUnderRepeatedOneSidedFlowSinceItIgnoresInventory) {
  NaiveMakerConfig config;
  config.half_spread = 5;
  config.quote_size = 10;
  NaiveMaker maker(config);
  sim::Simulator simulator(&maker, /*latency=*/0);

  simulator.LoadEvents({
      sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 90, 1000),
      sim::testing::MakeAddEvent(20, 1, 2, Side::Sell, 110, 1000),
  });
  simulator.Run();
  ASSERT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, 95).size(), 1u);

  // Repeated aggressive taker sells (priced to guarantee crossing whatever
  // our current best bid happens to be) each lift 10 units of inventory --
  // with no inventory awareness, naive just keeps buying. Each full fill
  // briefly reverts best_bid to the seed order until we requote, so our
  // quoted bid drifts a little closer to the seed price every round; after
  // 3 rounds it lands exactly on the seed's price (90) and, from then on,
  // price-time priority puts our order behind the seed's much larger
  // resting quantity, so it stops absorbing fills. 3 rounds keeps this
  // test within the region that fills cleanly every time.
  for (int i = 0; i < 3; ++i) {
    Timestamp t = static_cast<Timestamp>(30 + i * 10);
    simulator.LoadEvents({sim::testing::MakeAddEvent(
        t, static_cast<std::uint64_t>(2 + i), static_cast<OrderId>(100 + i), Side::Sell, 1, 10)});
    simulator.Run();
  }

  EXPECT_EQ(maker.inventory(), 30);
  EXPECT_EQ(maker.fills().size(), 3u);
}

}  // namespace
}  // namespace lob::mm
