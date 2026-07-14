#include "lob/spsc_ring_buffer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

namespace lob {
namespace {

TEST(SpscRingBufferTest, PopOnEmptyBufferFails) {
  SpscRingBuffer<int, 4> buffer;
  int out = 0;
  EXPECT_FALSE(buffer.TryPop(out));
}

TEST(SpscRingBufferTest, PushThenPopReturnsSameValue) {
  SpscRingBuffer<int, 4> buffer;
  EXPECT_TRUE(buffer.TryPush(42));
  int out = 0;
  ASSERT_TRUE(buffer.TryPop(out));
  EXPECT_EQ(out, 42);
}

TEST(SpscRingBufferTest, PreservesFifoOrder) {
  SpscRingBuffer<int, 8> buffer;
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(buffer.TryPush(i));
  }
  for (int i = 0; i < 5; ++i) {
    int out = -1;
    ASSERT_TRUE(buffer.TryPop(out));
    EXPECT_EQ(out, i);
  }
}

TEST(SpscRingBufferTest, OnlyCapacityMinusOneSlotsAreUsable) {
  // Capacity 4 -> exactly 3 usable slots, by design (disambiguates empty
  // from full without a separate count field).
  SpscRingBuffer<int, 4> buffer;
  EXPECT_TRUE(buffer.TryPush(1));
  EXPECT_TRUE(buffer.TryPush(2));
  EXPECT_TRUE(buffer.TryPush(3));
  EXPECT_FALSE(buffer.TryPush(4));  // full at Capacity - 1 items

  int out = 0;
  ASSERT_TRUE(buffer.TryPop(out));
  EXPECT_EQ(out, 1);
  EXPECT_TRUE(buffer.TryPush(4));  // freed one slot, now succeeds
}

TEST(SpscRingBufferTest, WrapsAroundCorrectly) {
  SpscRingBuffer<int, 4> buffer;
  // Push/pop repeatedly past the physical end of the backing array to
  // exercise the modulo wraparound in both TryPush and TryPop.
  for (int round = 0; round < 10; ++round) {
    EXPECT_TRUE(buffer.TryPush(round));
    int out = -1;
    ASSERT_TRUE(buffer.TryPop(out));
    EXPECT_EQ(out, round);
  }
}

TEST(SpscRingBufferTest, TwoRealThreadsProduceAndConsumeInOrder) {
  constexpr int kCount = 200000;
  SpscRingBuffer<int, 1024> buffer;
  std::vector<int> received;
  received.reserve(kCount);

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      while (!buffer.TryPush(i)) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    int out = 0;
    for (int i = 0; i < kCount; ++i) {
      while (!buffer.TryPop(out)) {
        std::this_thread::yield();
      }
      received.push_back(out);
    }
  });

  producer.join();
  consumer.join();

  ASSERT_EQ(received.size(), static_cast<std::size_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    EXPECT_EQ(received[static_cast<std::size_t>(i)], i);
  }
}

}  // namespace
}  // namespace lob
