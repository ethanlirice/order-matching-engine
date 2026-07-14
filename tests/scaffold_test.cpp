#include <gtest/gtest.h>

#include "lob/matching_engine.hpp"
#include "lob/order.hpp"
#include "lob/order_book.hpp"

// M0 scaffold: proves the build/test harness works end to end. Real
// matching-invariant tests land in M1.

TEST(Scaffold, HarnessRuns) {
  EXPECT_TRUE(true);
}

TEST(Scaffold, OrderDefaultConstructs) {
  lob::Order order{};
  EXPECT_EQ(order.quantity, 0u);
  EXPECT_EQ(order.side, lob::Side::Buy);
}

TEST(Scaffold, StubTypesConstruct) {
  lob::OrderBook book;
  lob::Price price = 0;
  EXPECT_FALSE(book.best_bid(price));

  lob::MatchingEngine engine;
  engine.submit_order(lob::Order{});
}
