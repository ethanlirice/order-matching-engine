#include "lob/matching_engine.hpp"

#include <gtest/gtest.h>

#include "support/test_helpers.hpp"

namespace lob {
namespace {

using testing::MakeOrder;

TEST(MatchingEngineTest, SubmitOrderReturnsResultAndFiresCallbackPerTrade) {
  MatchingEngine engine;
  std::vector<TradeEvent> received;
  engine.set_trade_callback([&](const TradeEvent& trade) { received.push_back(trade); });

  engine.submit_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 5));
  engine.submit_order(MakeOrder(2, Side::Sell, OrderType::Limit, 101, 5));

  AddOrderResult result = engine.submit_order(MakeOrder(3, Side::Buy, OrderType::Limit, 101, 10));

  ASSERT_EQ(result.trades.size(), 2u);
  ASSERT_EQ(received.size(), 2u);
  EXPECT_EQ(received[0].maker_order_id, 1u);
  EXPECT_EQ(received[1].maker_order_id, 2u);
  EXPECT_EQ(result.filled_quantity, 10u);
}

TEST(MatchingEngineTest, NoCallbackRegisteredDoesNotCrash) {
  MatchingEngine engine;
  engine.submit_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 5));
  AddOrderResult result = engine.submit_order(MakeOrder(2, Side::Buy, OrderType::Limit, 100, 5));
  EXPECT_EQ(result.filled_quantity, 5u);
}

TEST(MatchingEngineTest, CancelOrderDelegatesToOrderBook) {
  MatchingEngine engine;
  engine.submit_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));

  std::optional<Quantity> cancelled = engine.cancel_order(1);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(*cancelled, 10u);
  EXPECT_FALSE(engine.cancel_order(1).has_value());
}

TEST(MatchingEngineTest, ModifyOrderDelegatesToOrderBookAndFiresCallback) {
  MatchingEngine engine;
  std::vector<TradeEvent> received;
  engine.set_trade_callback([&](const TradeEvent& trade) { received.push_back(trade); });

  engine.submit_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 10));
  engine.submit_order(MakeOrder(2, Side::Buy, OrderType::Limit, 90, 10));

  std::optional<AddOrderResult> result = engine.modify_order(2, 100, 10);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->filled_quantity, 10u);
  ASSERT_EQ(received.size(), 1u);
  EXPECT_EQ(received[0].maker_order_id, 1u);
}

}  // namespace
}  // namespace lob
