#include "lob/order_book.hpp"

#include <gtest/gtest.h>

#include "support/test_helpers.hpp"

namespace lob {
namespace {

using testing::CheckInvariants;
using testing::MakeOrder;

// -- Core matching scenarios --------------------------------------------

TEST(OrderBookTest, LimitOrderRestsWhenNoCross) {
  OrderBook book;
  AddOrderResult result = book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));

  EXPECT_TRUE(result.trades.empty());
  EXPECT_EQ(result.filled_quantity, 0u);
  EXPECT_EQ(result.resting_quantity, 10u);
  EXPECT_EQ(result.cancelled_quantity, 0u);
  EXPECT_TRUE(book.contains(1));

  Price best = 0;
  ASSERT_TRUE(book.best_bid(best));
  EXPECT_EQ(best, 100);
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, SingleLevelFullMatch) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 10));
  AddOrderResult result = book.add_order(MakeOrder(2, Side::Buy, OrderType::Limit, 100, 10));

  ASSERT_EQ(result.trades.size(), 1u);
  EXPECT_EQ(result.trades[0].price, 100);
  EXPECT_EQ(result.trades[0].size, 10u);
  EXPECT_EQ(result.trades[0].maker_order_id, 1u);
  EXPECT_EQ(result.trades[0].taker_order_id, 2u);
  EXPECT_EQ(result.trades[0].aggressor_side, Side::Buy);
  EXPECT_EQ(result.filled_quantity, 10u);
  EXPECT_EQ(result.resting_quantity, 0u);
  EXPECT_FALSE(book.contains(1));
  EXPECT_FALSE(book.contains(2));
  EXPECT_TRUE(book.ask_prices().empty());
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, MultiLevelMatchMakerPricesAreOrdered) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 5));
  book.add_order(MakeOrder(2, Side::Sell, OrderType::Limit, 101, 5));
  book.add_order(MakeOrder(3, Side::Sell, OrderType::Limit, 102, 5));

  AddOrderResult result = book.add_order(MakeOrder(4, Side::Buy, OrderType::Limit, 102, 15));

  ASSERT_EQ(result.trades.size(), 3u);
  EXPECT_EQ(result.trades[0].price, 100);
  EXPECT_EQ(result.trades[1].price, 101);
  EXPECT_EQ(result.trades[2].price, 102);
  for (std::size_t i = 1; i < result.trades.size(); ++i) {
    EXPECT_LE(result.trades[i - 1].price, result.trades[i].price)
        << "maker prices must be non-decreasing for a Buy taker walking asks_";
  }
  EXPECT_EQ(result.filled_quantity, 15u);
  EXPECT_EQ(result.resting_quantity, 0u);
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, MultiLevelSellMatchMakerPricesAreOrdered) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 102, 5));
  book.add_order(MakeOrder(2, Side::Buy, OrderType::Limit, 101, 5));
  book.add_order(MakeOrder(3, Side::Buy, OrderType::Limit, 100, 5));

  AddOrderResult result = book.add_order(MakeOrder(4, Side::Sell, OrderType::Limit, 100, 15));

  ASSERT_EQ(result.trades.size(), 3u);
  EXPECT_EQ(result.trades[0].price, 102);
  EXPECT_EQ(result.trades[1].price, 101);
  EXPECT_EQ(result.trades[2].price, 100);
  for (std::size_t i = 1; i < result.trades.size(); ++i) {
    EXPECT_GE(result.trades[i - 1].price, result.trades[i].price)
        << "maker prices must be non-increasing for a Sell taker walking bids_";
  }
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, PartialFillMakerRestsWithReducedQuantity) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 10));
  AddOrderResult result = book.add_order(MakeOrder(2, Side::Buy, OrderType::Limit, 100, 4));

  ASSERT_EQ(result.trades.size(), 1u);
  EXPECT_EQ(result.trades[0].size, 4u);
  EXPECT_EQ(result.filled_quantity, 4u);
  EXPECT_EQ(result.resting_quantity, 0u);
  EXPECT_TRUE(book.contains(1));

  const Order* remaining = book.debug_peek(1);
  ASSERT_NE(remaining, nullptr);
  EXPECT_EQ(remaining->quantity, 6u);
  EXPECT_FALSE(book.contains(2));
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, ExactSimultaneousExhaustionRemovesBothOrders) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 10));
  AddOrderResult result = book.add_order(MakeOrder(2, Side::Buy, OrderType::Limit, 100, 10));

  EXPECT_FALSE(book.contains(1));
  EXPECT_FALSE(book.contains(2));
  EXPECT_EQ(result.resting_quantity, 0u);
  EXPECT_TRUE(book.ask_prices().empty());
  EXPECT_TRUE(book.bid_prices().empty());
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, MarketOrderAgainstEmptyBookIsFullyCancelled) {
  OrderBook book;
  AddOrderResult result = book.add_order(MakeOrder(1, Side::Buy, OrderType::Market, 0, 10));

  EXPECT_TRUE(result.trades.empty());
  EXPECT_EQ(result.filled_quantity, 0u);
  EXPECT_EQ(result.resting_quantity, 0u);
  EXPECT_EQ(result.cancelled_quantity, 10u);
  EXPECT_FALSE(book.contains(1));
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, MarketOrderPartialFillThenRemainderDropped) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 4));
  AddOrderResult result = book.add_order(MakeOrder(2, Side::Buy, OrderType::Market, 0, 10));

  ASSERT_EQ(result.trades.size(), 1u);
  EXPECT_EQ(result.filled_quantity, 4u);
  EXPECT_EQ(result.cancelled_quantity, 6u);
  EXPECT_EQ(result.resting_quantity, 0u);
  EXPECT_FALSE(book.contains(2));
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, MarketOrderNeverRests) {
  OrderBook book;
  AddOrderResult result = book.add_order(MakeOrder(1, Side::Sell, OrderType::Market, 0, 10));

  EXPECT_EQ(result.cancelled_quantity, 10u);
  EXPECT_EQ(result.resting_quantity, 0u);
  EXPECT_TRUE(book.ask_prices().empty());
}

// -- Cancel ---------------------------------------------------------------

TEST(OrderBookTest, CancelRestingOrderRemovesIt) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));

  std::optional<Quantity> cancelled = book.cancel_order(1);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(*cancelled, 10u);
  EXPECT_FALSE(book.contains(1));
  EXPECT_TRUE(book.bid_prices().empty());
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, CancelUnknownIdReturnsNullopt) {
  OrderBook book;
  EXPECT_FALSE(book.cancel_order(999).has_value());
}

TEST(OrderBookTest, CancelOneOfTwoOrdersAtSameLevelKeepsTheOther) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));
  book.add_order(MakeOrder(2, Side::Buy, OrderType::Limit, 100, 5));

  book.cancel_order(1);

  std::vector<OrderId> ids = book.resting_order_ids(Side::Buy, 100);
  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], 2u);
  EXPECT_TRUE(CheckInvariants(book));
}

// -- Modify -----------------------------------------------------------------

TEST(OrderBookTest, ModifySamePriceChangesQuantityAndLosesTimePriority) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));
  book.add_order(MakeOrder(2, Side::Buy, OrderType::Limit, 100, 5));

  std::optional<AddOrderResult> result = book.modify_order(1, 100, 20);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->resting_quantity, 20u);

  std::vector<OrderId> ids = book.resting_order_ids(Side::Buy, 100);
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], 2u) << "order 2 now has priority; order 1 moved to the tail";
  EXPECT_EQ(ids[1], 1u);
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, ModifyChangesPriceLevel) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));

  std::optional<AddOrderResult> result = book.modify_order(1, 105, 10);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->resting_quantity, 10u);
  EXPECT_TRUE(book.resting_order_ids(Side::Buy, 100).empty());

  std::vector<OrderId> ids = book.resting_order_ids(Side::Buy, 105);
  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], 1u);
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, ModifyThatBecomesMarketableMatchesImmediately) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 10));
  book.add_order(MakeOrder(2, Side::Buy, OrderType::Limit, 90, 10));

  std::optional<AddOrderResult> result = book.modify_order(2, 100, 10);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->trades.size(), 1u);
  EXPECT_EQ(result->trades[0].maker_order_id, 1u);
  EXPECT_EQ(result->trades[0].taker_order_id, 2u);
  EXPECT_EQ(result->filled_quantity, 10u);
  EXPECT_FALSE(book.contains(1));
  EXPECT_FALSE(book.contains(2));
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, ModifyToZeroQuantityIsFullCancel) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));

  std::optional<AddOrderResult> result = book.modify_order(1, 100, 0);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->cancelled_quantity, 10u);
  EXPECT_EQ(result->filled_quantity, 0u);
  EXPECT_EQ(result->resting_quantity, 0u);
  EXPECT_FALSE(book.contains(1));
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, ModifyUnknownIdReturnsNullopt) {
  OrderBook book;
  EXPECT_FALSE(book.modify_order(999, 100, 10).has_value());
}

// -- Rejections (defined, testable behavior -- not undefined states) -------

TEST(OrderBookTest, DuplicateOrderIdIsRejectedAndOriginalUntouched) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));
  AddOrderResult result = book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 5));

  EXPECT_EQ(result.cancelled_quantity, 5u);
  EXPECT_EQ(result.filled_quantity, 0u);
  EXPECT_EQ(result.resting_quantity, 0u);

  const Order* original = book.debug_peek(1);
  ASSERT_NE(original, nullptr);
  EXPECT_EQ(original->quantity, 10u);
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookTest, ZeroQuantityOrderIsANoOp) {
  OrderBook book;
  AddOrderResult result = book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 0));

  EXPECT_TRUE(result.trades.empty());
  EXPECT_EQ(result.filled_quantity, 0u);
  EXPECT_EQ(result.resting_quantity, 0u);
  EXPECT_EQ(result.cancelled_quantity, 0u);
  EXPECT_FALSE(book.contains(1));
}

TEST(OrderBookTest, OutOfScopeOrderTypesAreRejected) {
  for (OrderType type : {OrderType::IOC, OrderType::FOK, OrderType::PostOnly}) {
    OrderBook book;
    AddOrderResult result = book.add_order(MakeOrder(1, Side::Buy, type, 100, 10));
    EXPECT_EQ(result.cancelled_quantity, 10u);
    EXPECT_EQ(result.filled_quantity, 0u);
    EXPECT_EQ(result.resting_quantity, 0u);
    EXPECT_FALSE(book.contains(1));
  }
}

// -- Invariants (PROJECT_SPEC.md §5.5) --------------------------------------

TEST(OrderBookInvariantTest, BestBidNeverAtOrAboveBestAsk) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 99, 10));
  book.add_order(MakeOrder(2, Side::Sell, OrderType::Limit, 101, 10));
  Price bid = 0;
  Price ask = 0;
  ASSERT_TRUE(book.best_bid(bid));
  ASSERT_TRUE(book.best_ask(ask));
  EXPECT_LT(bid, ask);
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookInvariantTest, QuantityConservationAcrossAMixedSequence) {
  OrderBook book;
  // Resting liquidity on both sides.
  book.add_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 5));
  book.add_order(MakeOrder(2, Side::Sell, OrderType::Limit, 101, 5));
  book.add_order(MakeOrder(3, Side::Buy, OrderType::Limit, 95, 5));

  // A marketable buy that partially fills, partially rests.
  AddOrderResult result = book.add_order(MakeOrder(4, Side::Buy, OrderType::Limit, 100, 8));
  EXPECT_EQ(8u, result.filled_quantity + result.resting_quantity + result.cancelled_quantity);

  // A market sell that exceeds available bid liquidity.
  AddOrderResult market_result = book.add_order(MakeOrder(5, Side::Sell, OrderType::Market, 0, 20));
  EXPECT_EQ(20u, market_result.filled_quantity + market_result.resting_quantity +
                     market_result.cancelled_quantity);

  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookInvariantTest, PriceTimePriorityFifoWithinLevel) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Sell, OrderType::Limit, 100, 4));
  book.add_order(MakeOrder(2, Side::Sell, OrderType::Limit, 100, 4));
  book.add_order(MakeOrder(3, Side::Sell, OrderType::Limit, 100, 4));

  // Only enough to fill the first two makers.
  AddOrderResult result = book.add_order(MakeOrder(4, Side::Buy, OrderType::Limit, 100, 8));

  ASSERT_EQ(result.trades.size(), 2u);
  EXPECT_EQ(result.trades[0].maker_order_id, 1u);
  EXPECT_EQ(result.trades[1].maker_order_id, 2u);
  EXPECT_TRUE(book.contains(3));
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookInvariantTest, EmptyLevelsAreAlwaysRemoved) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));
  book.cancel_order(1);
  EXPECT_TRUE(book.bid_prices().empty());

  book.add_order(MakeOrder(2, Side::Sell, OrderType::Limit, 100, 10));
  book.add_order(MakeOrder(3, Side::Buy, OrderType::Limit, 100, 10));
  EXPECT_TRUE(book.ask_prices().empty());
  EXPECT_TRUE(CheckInvariants(book));
}

TEST(OrderBookInvariantTest, StructuralConsistencyBetweenIndexAndLevels) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));
  book.add_order(MakeOrder(2, Side::Buy, OrderType::Limit, 99, 5));
  book.add_order(MakeOrder(3, Side::Sell, OrderType::Limit, 105, 7));
  book.modify_order(2, 98, 5);
  book.cancel_order(3);

  EXPECT_EQ(book.order_count(), 2u);
  EXPECT_TRUE(CheckInvariants(book));
}

}  // namespace
}  // namespace lob
