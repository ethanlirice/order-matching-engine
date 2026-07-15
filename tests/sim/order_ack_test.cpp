#include <gtest/gtest.h>

#include <vector>

#include "lob/sim/simulator.hpp"
#include "sim/support/sim_test_helpers.hpp"

namespace lob::sim {
namespace {

struct AckRecord {
  OrderId id = 0;
  AddOrderResult result;
};

// Records every OnOrderAck it receives; submits/modifies exactly on
// command from the test (via public helper methods), not automatically
// from OnBookUpdate, so each test can drive one specific scenario.
class AckRecordingStrategy : public Strategy {
 public:
  void OnBookUpdate(const BookSnapshot&, Timestamp, std::uint64_t, OrderIntentSink&) override {}
  void OnTrade(const TradeEvent&, Timestamp, std::uint64_t, OrderIntentSink&) override {}

  void OnOrderAck(OrderId id, const AddOrderResult& result) override {
    acks_.push_back(AckRecord{id, result});
  }

  const std::vector<AckRecord>& acks() const { return acks_; }

 private:
  std::vector<AckRecord> acks_;
};

TEST(OrderAckTest, PostOnlySubmitThatRestsAcksWithFullRestingQuantity) {
  AckRecordingStrategy strategy;
  Simulator simulator(&strategy, /*latency=*/0);

  NewOrderCommand command;
  command.side = Side::Buy;
  command.type = OrderType::PostOnly;
  command.price = 100;
  command.quantity = 10;
  OrderId id = simulator.Submit(command);
  simulator.Run();

  ASSERT_EQ(strategy.acks().size(), 1u);
  EXPECT_EQ(strategy.acks()[0].id, id);
  EXPECT_EQ(strategy.acks()[0].result.resting_quantity, 10u);
  EXPECT_EQ(strategy.acks()[0].result.cancelled_quantity, 0u);
  EXPECT_TRUE(simulator.DebugBook().contains(id));
}

TEST(OrderAckTest, PostOnlySubmitThatWouldCrossAcksWithFullCancellation) {
  AckRecordingStrategy strategy;
  Simulator simulator(&strategy, /*latency=*/0);

  simulator.LoadEvents({testing::MakeAddEvent(10, 0, 1, Side::Sell, 100, 10)});
  simulator.Run();

  NewOrderCommand command;
  command.side = Side::Buy;
  command.type = OrderType::PostOnly;
  command.price = 100;  // would cross the resting ask -- PostOnly rejects
  command.quantity = 5;
  OrderId id = simulator.Submit(command);
  simulator.Run();

  ASSERT_EQ(strategy.acks().size(), 1u);
  EXPECT_EQ(strategy.acks()[0].id, id);
  EXPECT_EQ(strategy.acks()[0].result.resting_quantity, 0u);
  EXPECT_EQ(strategy.acks()[0].result.cancelled_quantity, 5u);
  EXPECT_FALSE(simulator.DebugBook().contains(id));
}

TEST(OrderAckTest, ModifyToNonCrossingPriceAcksWithNewRestingQuantity) {
  AckRecordingStrategy strategy;
  Simulator simulator(&strategy, /*latency=*/0);

  NewOrderCommand command;
  command.side = Side::Buy;
  command.type = OrderType::PostOnly;
  command.price = 100;
  command.quantity = 10;
  OrderId id = simulator.Submit(command);
  simulator.Run();

  simulator.Modify(id, 99, 7);
  simulator.Run();

  ASSERT_EQ(strategy.acks().size(), 2u);
  EXPECT_EQ(strategy.acks()[1].id, id);
  EXPECT_EQ(strategy.acks()[1].result.resting_quantity, 7u);
  ASSERT_TRUE(simulator.DebugBook().contains(id));
  EXPECT_EQ(simulator.DebugBook().debug_peek(id)->price, 99);
}

// The critical scenario the OnOrderAck interface exists for: without it, a
// strategy has no way to learn that a PostOnly modify-to-a-crossing-price
// silently cancelled the original order AND rejected the replacement.
TEST(OrderAckTest, ModifyToCrossingPriceAcksWithFullCancellationNotSilentLoss) {
  AckRecordingStrategy strategy;
  Simulator simulator(&strategy, /*latency=*/0);

  simulator.LoadEvents({testing::MakeAddEvent(10, 0, 1, Side::Sell, 105, 10)});
  simulator.Run();

  NewOrderCommand command;
  command.side = Side::Buy;
  command.type = OrderType::PostOnly;
  command.price = 100;
  command.quantity = 10;
  OrderId id = simulator.Submit(command);
  simulator.Run();

  simulator.Modify(id, 105, 10);  // now crosses -- PostOnly modify rejects
  simulator.Run();

  ASSERT_EQ(strategy.acks().size(), 2u);
  EXPECT_EQ(strategy.acks()[1].id, id);
  EXPECT_EQ(strategy.acks()[1].result.resting_quantity, 0u);
  EXPECT_EQ(strategy.acks()[1].result.cancelled_quantity, 10u);
  EXPECT_FALSE(simulator.DebugBook().contains(id))
      << "both the original and the replacement are gone -- the ack is what "
         "tells the strategy this happened";
}

}  // namespace
}  // namespace lob::sim
