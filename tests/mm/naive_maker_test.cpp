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

  // Seed mid = 100, so the maker wants roughly bid=95/ask=105 -- but the
  // exact settled prices depend on how many single-action reconciliation
  // rounds it takes to converge (MarketMaker allows only one Submit/Modify
  // in flight at a time; see its class comment), so this checks structural
  // properties (spread width, single order per level, quantity) rather
  // than hand-computed prices.
  simulator.LoadEvents({
      sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 90, 1000),
      sim::testing::MakeAddEvent(20, 1, 2, Side::Sell, 110, 1000),
  });
  simulator.Run();

  Price bid = 0, ask = 0;
  ASSERT_TRUE(simulator.DebugBook().best_bid(bid));
  ASSERT_TRUE(simulator.DebugBook().best_ask(ask));
  ASSERT_GT(bid, 90);
  ASSERT_LT(ask, 110);
  EXPECT_EQ(ask - bid, 2 * config.half_spread);
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, bid).size(), 1u);
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, ask).size(), 1u);
  EXPECT_EQ(simulator.DebugBook().quantity_at(Side::Buy, bid), 10u);
  EXPECT_EQ(simulator.DebugBook().quantity_at(Side::Sell, ask), 10u);
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
  Price initial_bid = 0;
  ASSERT_TRUE(simulator.DebugBook().best_bid(initial_bid));
  ASSERT_GT(initial_bid, 90);

  // Repeated aggressive taker sells (priced to guarantee crossing whatever
  // our current best bid happens to be) each lift 10 units of inventory --
  // with no inventory awareness, naive just keeps buying. Each full fill
  // briefly reverts best_bid to the seed order until we requote, so our
  // quoted bid drifts a little closer to the seed price every round; once
  // it lands exactly on the seed's price (90), price-time priority puts
  // our order behind the seed's much larger resting quantity, and it stops
  // absorbing fills. 4 rounds empirically stays within the region that
  // fills cleanly every time for this seed/config (verified directly
  // rather than re-derived by hand, since the exact number of clean
  // rounds depends on the reconciliation settling dynamics -- see
  // MarketMaker's class comment).
  for (int i = 0; i < 4; ++i) {
    Timestamp t = static_cast<Timestamp>(30 + i * 10);
    simulator.LoadEvents({sim::testing::MakeAddEvent(
        t, static_cast<std::uint64_t>(2 + i), static_cast<OrderId>(100 + i), Side::Sell, 1, 10)});
    simulator.Run();
  }

  EXPECT_EQ(maker.inventory(), 40);
  EXPECT_EQ(maker.fills().size(), 4u);
}

}  // namespace
}  // namespace lob::mm
