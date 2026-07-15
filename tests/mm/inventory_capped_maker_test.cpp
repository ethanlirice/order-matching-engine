#include "lob/mm/inventory_capped_maker.hpp"

#include <gtest/gtest.h>

#include "lob/sim/simulator.hpp"
#include "sim/support/sim_test_helpers.hpp"

namespace lob::mm {
namespace {

TEST(InventoryCappedMakerTest, StopsQuotingTheBuySideOnceInventoryHitsTheCap) {
  InventoryCappedMakerConfig config;
  config.half_spread = 5;
  config.quote_size = 10;
  config.max_inventory = 10;
  InventoryCappedMaker maker(config);
  sim::Simulator simulator(&maker, /*latency=*/0);

  // Seed liquidity settles the maker's quotes on both sides (exact prices
  // depend on how many single-action reconciliation rounds it takes to
  // converge -- see MarketMaker's class comment -- so this is checked via
  // best_bid/best_ask rather than hand-computed from the seed mid).
  simulator.LoadEvents({
      sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 90, 1000),
      sim::testing::MakeAddEvent(20, 1, 2, Side::Sell, 110, 1000),
  });
  simulator.Run();
  Price settled_bid = 0;
  ASSERT_TRUE(simulator.DebugBook().best_bid(settled_bid));
  ASSERT_GT(settled_bid, 90);
  ASSERT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, settled_bid).size(), 1u);

  // An aggressive taker sell (priced to guarantee crossing whatever our
  // current bid happens to be) for exactly the quote size -- inventory
  // lands exactly at the cap. Our bid is now gone, so best_bid reverts to
  // the seed order@90, shifting mid (and thus the still-active ask) too.
  simulator.LoadEvents({sim::testing::MakeAddEvent(30, 2, 3, Side::Sell, 1, 10)});
  simulator.Run();

  EXPECT_EQ(maker.inventory(), 10);
  // At the cap: stop quoting the buy side entirely.
  EXPECT_TRUE(simulator.DebugBook().resting_order_ids(Side::Buy, settled_bid).empty());
  // The sell side (which would unwind the position) keeps quoting: the
  // best ask must be tighter than the seed order@110, so it can only be
  // ours, and it should be the only order resting there.
  Price ask_price = 0;
  ASSERT_TRUE(simulator.DebugBook().best_ask(ask_price));
  EXPECT_LT(ask_price, 110);
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, ask_price).size(), 1u);
}

TEST(InventoryCappedMakerTest, StopsQuotingTheSellSideOnceShortInventoryHitsTheCap) {
  InventoryCappedMakerConfig config;
  config.half_spread = 5;
  config.quote_size = 10;
  config.max_inventory = 10;
  InventoryCappedMaker maker(config);
  sim::Simulator simulator(&maker, /*latency=*/0);

  simulator.LoadEvents({
      sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 90, 1000),
      sim::testing::MakeAddEvent(20, 1, 2, Side::Sell, 110, 1000),
  });
  simulator.Run();
  Price settled_ask = 0;
  ASSERT_TRUE(simulator.DebugBook().best_ask(settled_ask));
  ASSERT_LT(settled_ask, 110);
  ASSERT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, settled_ask).size(), 1u);

  // An aggressive taker buy (priced to guarantee crossing whatever our
  // current ask happens to be) for exactly the quote size -- inventory
  // lands exactly at -cap.
  simulator.LoadEvents({sim::testing::MakeAddEvent(30, 2, 3, Side::Buy, 1'000'000, 10)});
  simulator.Run();

  EXPECT_EQ(maker.inventory(), -10);
  EXPECT_TRUE(simulator.DebugBook().resting_order_ids(Side::Sell, settled_ask).empty());
  // The buy side (which would unwind the position) keeps quoting: the
  // best bid must be tighter than the seed order@90, so it can only be
  // ours, and it should be the only order resting there.
  Price bid_price = 0;
  ASSERT_TRUE(simulator.DebugBook().best_bid(bid_price));
  EXPECT_GT(bid_price, 90);
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, bid_price).size(), 1u);
}

}  // namespace
}  // namespace lob::mm
