#include "lob/order_pool.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace lob {
namespace {

TEST(OrderPoolTest, AcquireReturnsCleanDefaultSlot) {
  OrderPool pool(4);
  Order* order = pool.Acquire();
  ASSERT_NE(order, nullptr);
  EXPECT_EQ(order->id, 0u);
  EXPECT_EQ(order->quantity, 0u);
  EXPECT_EQ(order->prev, nullptr);
  EXPECT_EQ(order->next, nullptr);
}

TEST(OrderPoolTest, AcquireWithinInitialCapacityAllocatesNoNewChunk) {
  OrderPool pool(4);
  EXPECT_EQ(pool.chunk_count(), 1u);

  std::vector<Order*> orders;
  for (int i = 0; i < 4; ++i) {
    orders.push_back(pool.Acquire());
  }
  EXPECT_EQ(pool.chunk_count(), 1u);
}

TEST(OrderPoolTest, ExhaustingThePoolGrowsByOneChunk) {
  OrderPool pool(4);
  for (int i = 0; i < 4; ++i) {
    pool.Acquire();
  }
  EXPECT_EQ(pool.chunk_count(), 1u);

  pool.Acquire();  // 5th acquire exhausts the first chunk, triggers growth
  EXPECT_EQ(pool.chunk_count(), 2u);
}

TEST(OrderPoolTest, ReleasedSlotIsReusedInsteadOfGrowingAgain) {
  OrderPool pool(2);
  Order* first = pool.Acquire();
  pool.Acquire();
  EXPECT_EQ(pool.chunk_count(), 1u);

  pool.Release(first);
  Order* reused = pool.Acquire();
  EXPECT_EQ(reused, first);
  EXPECT_EQ(pool.chunk_count(), 1u);  // no growth needed -- the free slot covered it
}

TEST(OrderPoolTest, ReusedSlotIsCleanNotStaleFromThePriorOccupant) {
  OrderPool pool(2);
  Order* order = pool.Acquire();
  order->id = 42;
  order->quantity = 100;
  order->price = 500;

  pool.Release(order);
  Order* reused = pool.Acquire();
  EXPECT_EQ(reused, order);
  EXPECT_EQ(reused->id, 0u);
  EXPECT_EQ(reused->quantity, 0u);
  EXPECT_EQ(reused->price, 0);
}

TEST(OrderPoolTest, PointersAcrossChunkGrowthRemainValid) {
  OrderPool pool(2);
  Order* first = pool.Acquire();
  first->id = 1;
  Order* second = pool.Acquire();
  second->id = 2;

  // Forces growth -- first/second must not be invalidated by it.
  Order* third = pool.Acquire();
  third->id = 3;

  EXPECT_EQ(first->id, 1u);
  EXPECT_EQ(second->id, 2u);
  EXPECT_EQ(third->id, 3u);
}

}  // namespace
}  // namespace lob
