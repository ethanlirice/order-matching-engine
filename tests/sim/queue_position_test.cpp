#include <gtest/gtest.h>

#include <vector>

#include "lob/sim/simulator.hpp"
#include "sim/support/sim_test_helpers.hpp"

namespace lob::sim {
namespace {

// Submits one resting Sell 105 qty=5 order the first time it's asked to
// react, and records every fill of that specific order by id.
class QueuePositionTestStrategy : public Strategy {
 public:
  void OnBookUpdate(const BookSnapshot& /*snapshot*/, Timestamp now,
                    std::uint64_t /*event_ordinal*/, OrderIntentSink& intents) override {
    if (submitted_) {
      return;
    }
    NewOrderCommand command;
    command.side = Side::Sell;
    command.type = OrderType::Limit;
    command.price = 105;
    command.quantity = 5;
    command.timestamp = now;
    order_id_ = intents.Submit(command);
    submitted_ = true;
  }

  void OnTrade(const TradeEvent& trade, Timestamp /*now*/, std::uint64_t /*event_ordinal*/,
               OrderIntentSink& /*intents*/) override {
    if (trade.maker_order_id == order_id_) {
      fills_.push_back(trade.size);
    }
  }

  OrderId order_id() const { return order_id_; }
  const std::vector<Quantity>& fills() const { return fills_; }

 private:
  bool submitted_ = false;
  OrderId order_id_ = 0;
  std::vector<Quantity> fills_;
};

// The direct, end-to-end test of the core M4 insight: a strategy order
// injected behind existing resting volume must not fill until everything
// ahead of it in the queue is consumed -- this emerges from correctly
// interleaving the strategy's (latency-delayed) order arrival with replay
// events on one shared OrderBook, not from any separate queue-depth
// tracking.
TEST(QueuePositionTest, StrategyOrderDoesNotFillUntilAheadVolumeIsConsumed) {
  QueuePositionTestStrategy strategy;
  Simulator simulator(&strategy, /*latency=*/5);

  // t=100: existing resting ask ahead of where the strategy will join.
  // Processing this fires OnBookUpdate, which submits the strategy's own
  // order scheduled to arrive at t=105 -- Run() drains both in this call.
  simulator.LoadEvents({testing::MakeAddEvent(100, 0, 1, Side::Sell, 105, 10)});
  simulator.Run();

  ASSERT_NE(strategy.order_id(), 0u);
  EXPECT_TRUE(simulator.DebugBook().contains(strategy.order_id()));
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, 105),
            (std::vector<OrderId>{1, strategy.order_id()}));

  // t=110: taker qty=10 exactly matches id=1's resting quantity -- the
  // strategy's order, though it arrived first from a wall-clock view of
  // scheduling, is correctly still behind id=1 in the book and must not
  // be touched.
  simulator.LoadEvents({testing::MakeAddEvent(110, 1, 3, Side::Buy, 105, 10)});
  simulator.Run();

  EXPECT_TRUE(strategy.fills().empty());
  EXPECT_FALSE(simulator.DebugBook().contains(1));
  ASSERT_TRUE(simulator.DebugBook().contains(strategy.order_id()));
  EXPECT_EQ(simulator.DebugBook().debug_peek(strategy.order_id())->quantity, 5u);

  // t=120: nothing ahead of the strategy's order anymore -- it now fills.
  simulator.LoadEvents({testing::MakeAddEvent(120, 2, 4, Side::Buy, 105, 3)});
  simulator.Run();

  ASSERT_EQ(strategy.fills().size(), 1u);
  EXPECT_EQ(strategy.fills()[0], 3u);
  ASSERT_TRUE(simulator.DebugBook().contains(strategy.order_id()));
  EXPECT_EQ(simulator.DebugBook().debug_peek(strategy.order_id())->quantity, 2u);
}

}  // namespace
}  // namespace lob::sim
