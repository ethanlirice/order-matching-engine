#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "lob/spsc_ring_buffer.hpp"
#include "support/percentile.hpp"

// SpscRingBuffer's own numbers -- not a delta against something
// pre-existing, since this is a new standalone component (M3 Step 3).

namespace {

using lob::SpscRingBuffer;

constexpr std::int64_t kIterations = 100000;

// Single-threaded round-trip (push immediately followed by pop): the
// cleanest possible latency number, uncontended by a second thread.
void BM_SpscPushPopLoopback(benchmark::State& state) {
  SpscRingBuffer<int, 1024> buffer;
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(state.max_iterations));

  for (auto _ : state) {
    auto start = std::chrono::steady_clock::now();
    buffer.TryPush(1);
    int out = 0;
    benchmark::DoNotOptimize(buffer.TryPop(out));
    auto end = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::nano>(end - start).count());
  }

  lob::bench::ReportPercentiles(state, samples);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SpscPushPopLoopback)->Iterations(kIterations);

// Genuine two-thread throughput: a real producer and a real consumer
// thread, contending on the ring buffer exactly as M4's feed/matching
// thread split will.
void BM_SpscTwoThreadThroughput(benchmark::State& state) {
  constexpr int kCount = 200000;

  for (auto _ : state) {
    SpscRingBuffer<int, 4096> buffer;

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
      }
    });
    producer.join();
    consumer.join();
  }

  state.SetItemsProcessed(state.iterations() * kCount);
}
BENCHMARK(BM_SpscTwoThreadThroughput)->Iterations(20)->UseRealTime();

}  // namespace
