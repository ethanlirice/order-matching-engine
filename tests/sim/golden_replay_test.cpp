#include <gtest/gtest.h>

#include <vector>

#include "lob/sim/simulator.hpp"
#include "sim/support/sim_test_helpers.hpp"

namespace lob::sim {
namespace {

using testing::MakeAddEvent;
using testing::MakeCancelEvent;
using testing::MakeReduceEvent;

// Reduce (LOBSTER-style partial cancellation) through the full Simulator
// path -- proves priority survives real event-queue processing, not just a
// direct OrderBook::ReduceQuantity call (see order_book_test.cpp for that).
TEST(GoldenReplayTest, ReduceEventPreservesPriorityThroughASubsequentMatch) {
  Simulator simulator;

  simulator.LoadEvents({
      MakeAddEvent(100, 0, 1, Side::Sell, 105, 10),
      MakeAddEvent(110, 1, 2, Side::Sell, 105, 5),
      MakeReduceEvent(120, 2, 1, 3),  // order 1: 10 -> 3, keeps its slot
      MakeAddEvent(130, 3, 3, Side::Buy, 105, 4),
  });
  simulator.Run();

  // Order 1 (now only 3) must still be consumed before order 2.
  ASSERT_EQ(simulator.trade_log().size(), 2u);
  EXPECT_EQ(simulator.trade_log()[0].maker_order_id, 1u);
  EXPECT_EQ(simulator.trade_log()[0].size, 3u);
  EXPECT_EQ(simulator.trade_log()[1].maker_order_id, 2u);
  EXPECT_EQ(simulator.trade_log()[1].size, 1u);
  EXPECT_FALSE(simulator.DebugBook().contains(1));
  ASSERT_NE(simulator.DebugBook().debug_peek(2), nullptr);
  EXPECT_EQ(simulator.DebugBook().debug_peek(2)->quantity, 4u);
}

// Hand-constructed trace with independently hand-computed expected
// checkpoints (NOT "run the engine once and snapshot its own output",
// which would only prove self-consistency, not correctness). Covers:
// a same-level multi-order partial-fill sweep and a genuine mid-queue
// cancel (removing an order that has others both before and after it in
// the same level's FIFO).
//
// Trace:
//   t=100  Add id=1 Sell 105 qty=10
//   t=110  Add id=2 Sell 105 qty=4
//   t=120  Add id=3 Sell 105 qty=6      -- level 105 FIFO: [1, 2, 3]
//   t=130  Cancel id=2                  -- mid-queue cancel -> [1, 3]
//   t=140  Add id=4 Buy  105 qty=13     -- sweeps id=1 (10) then id=3 (3 of 6)
//   t=150  Add id=5 Buy  104 qty=7      -- rests
//   t=160  Add id=6 Sell 106 qty=5      -- rests
TEST(GoldenReplayTest, HandComputedReconstructionMatchesExpectedCheckpoints) {
  Simulator simulator;

  simulator.LoadEvents({
      MakeAddEvent(100, 0, 1, Side::Sell, 105, 10),
      MakeAddEvent(110, 1, 2, Side::Sell, 105, 4),
      MakeAddEvent(120, 2, 3, Side::Sell, 105, 6),
      MakeCancelEvent(130, 3, 2),
  });
  simulator.Run();

  // Checkpoint A: the mid-queue cancel preserved the relative FIFO order
  // of the orders on either side of it.
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, 105), (std::vector<OrderId>{1, 3}));
  EXPECT_FALSE(simulator.DebugBook().contains(2));
  ASSERT_NE(simulator.DebugBook().debug_peek(1), nullptr);
  EXPECT_EQ(simulator.DebugBook().debug_peek(1)->quantity, 10u);
  ASSERT_NE(simulator.DebugBook().debug_peek(3), nullptr);
  EXPECT_EQ(simulator.DebugBook().debug_peek(3)->quantity, 6u);
  Price checkpoint_a_ask = 0;
  ASSERT_TRUE(simulator.DebugBook().best_ask(checkpoint_a_ask));
  EXPECT_EQ(checkpoint_a_ask, 105);
  Price unused = 0;
  EXPECT_FALSE(simulator.DebugBook().best_bid(unused));

  simulator.LoadEvents({MakeAddEvent(140, 4, 4, Side::Buy, 105, 13)});
  simulator.Run();

  // Checkpoint B: multi-order same-level sweep -- id=1 fully consumed,
  // id=3 partially, taker id=4 fully filled and does not rest.
  ASSERT_EQ(simulator.trade_log().size(), 2u);
  EXPECT_EQ(simulator.trade_log()[0].price, 105);
  EXPECT_EQ(simulator.trade_log()[0].size, 10u);
  EXPECT_EQ(simulator.trade_log()[0].maker_order_id, 1u);
  EXPECT_EQ(simulator.trade_log()[0].taker_order_id, 4u);
  EXPECT_EQ(simulator.trade_log()[1].price, 105);
  EXPECT_EQ(simulator.trade_log()[1].size, 3u);
  EXPECT_EQ(simulator.trade_log()[1].maker_order_id, 3u);
  EXPECT_EQ(simulator.trade_log()[1].taker_order_id, 4u);
  EXPECT_FALSE(simulator.DebugBook().contains(1));
  EXPECT_FALSE(simulator.DebugBook().contains(4));
  ASSERT_NE(simulator.DebugBook().debug_peek(3), nullptr);
  EXPECT_EQ(simulator.DebugBook().debug_peek(3)->quantity, 3u);

  simulator.LoadEvents({
      MakeAddEvent(150, 5, 5, Side::Buy, 104, 7),
      MakeAddEvent(160, 6, 6, Side::Sell, 106, 5),
  });
  simulator.Run();

  // Checkpoint C: final two-sided book state.
  EXPECT_EQ(simulator.DebugBook().bid_prices(), (std::vector<Price>{104}));
  EXPECT_EQ(simulator.DebugBook().ask_prices(), (std::vector<Price>{105, 106}));
  EXPECT_EQ(simulator.DebugBook().order_count(), 3u);
  Price final_bid = 0;
  Price final_ask = 0;
  ASSERT_TRUE(simulator.DebugBook().best_bid(final_bid));
  ASSERT_TRUE(simulator.DebugBook().best_ask(final_ask));
  EXPECT_EQ(final_bid, 104);
  EXPECT_EQ(final_ask, 105);
  EXPECT_EQ(simulator.trade_log().size(), 2u);  // unchanged -- no more crosses
  EXPECT_EQ(simulator.now(), 160u);
}

}  // namespace
}  // namespace lob::sim
