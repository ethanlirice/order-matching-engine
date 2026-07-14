#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <vector>

#include "lob/order_book.hpp"
#include "support/percentile.hpp"

// Baseline benchmark suite (M3 Step 0) -- these numbers are the "before"
// column for every optimization that follows. Build with
// -DLOB_ENABLE_SANITIZERS=OFF (the default; see benchmarks/CMakeLists.txt):
// sanitizer instrumentation overhead would invalidate latency measurements.
//
// Iteration counts are fixed (not auto-calibrated) so every pre-populated
// setup is sized deterministically against a known iteration count.

namespace {

using lob::Order;
using lob::OrderBook;
using lob::OrderId;
using lob::OrderType;
using lob::Price;
using lob::Quantity;
using lob::Side;

constexpr std::int64_t kIterations = 100000;

Order MakeOrder(OrderId id, Side side, OrderType type, Price price, Quantity quantity) {
  Order order;
  order.id = id;
  order.side = side;
  order.type = type;
  order.price = price;
  order.quantity = quantity;
  order.timestamp = 0;
  return order;
}

// Resting-only adds: never crosses, so liquidity never depletes -- no
// replenishment needed across iterations.
void BM_AddNoCross(benchmark::State& state) {
  OrderBook book;
  OrderId next_id = 1;
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(state.max_iterations));

  for (auto _ : state) {
    auto start = std::chrono::steady_clock::now();
    benchmark::DoNotOptimize(
        book.add_order(MakeOrder(next_id++, Side::Buy, OrderType::Limit, 100, 10)));
    auto end = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::nano>(end - start).count());
  }

  lob::bench::ReportPercentiles(state, samples);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddNoCross)->Iterations(kIterations);

// Crosses a single resting maker with effectively unlimited depth, so the
// same maker never depletes across the whole run.
void BM_AddCrossSingleLevel(benchmark::State& state) {
  OrderBook book;
  book.add_order(
      MakeOrder(1, Side::Sell, OrderType::Limit, 100, std::numeric_limits<Quantity>::max() / 2));
  OrderId next_id = 2;
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(state.max_iterations));

  for (auto _ : state) {
    auto start = std::chrono::steady_clock::now();
    benchmark::DoNotOptimize(
        book.add_order(MakeOrder(next_id++, Side::Buy, OrderType::Limit, 100, 1)));
    auto end = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::nano>(end - start).count());
  }

  lob::bench::ReportPercentiles(state, samples);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddCrossSingleLevel)->Iterations(kIterations);

// Walks exactly kLevels price levels on every single timed call: each level
// is topped up to exactly 1 unit of resting liquidity between iterations
// (untimed, via Pause/ResumeTiming), and the taker's quantity equals
// kLevels so it takes exactly 1 unit from each level in turn, guaranteeing
// a genuine multi-level walk every time rather than satisfying the whole
// order from the best level alone.
void BM_AddCrossMultiLevel(benchmark::State& state) {
  OrderBook book;
  constexpr int kLevels = 5;
  OrderId next_maker_id = 1;
  OrderId next_taker_id = 1'000'000;

  auto replenish = [&]() {
    for (int i = 0; i < kLevels; ++i) {
      Price price = 100 + i;
      book.add_order(MakeOrder(next_maker_id++, Side::Sell, OrderType::Limit, price, 1));
    }
  };
  replenish();

  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(state.max_iterations));

  for (auto _ : state) {
    Order taker =
        MakeOrder(next_taker_id++, Side::Buy, OrderType::Limit, 100 + kLevels - 1, kLevels);

    auto start = std::chrono::steady_clock::now();
    benchmark::DoNotOptimize(book.add_order(taker));
    auto end = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::nano>(end - start).count());

    state.PauseTiming();
    replenish();
    state.ResumeTiming();
  }

  lob::bench::ReportPercentiles(state, samples);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddCrossMultiLevel)->Iterations(kIterations);

// Pre-populates kIterations distinct resting orders, then cancels through
// them one at a time -- cancellation consumes the order, so unlike the add
// benchmarks this needs a bounded, sized-up-front population.
void BM_Cancel(benchmark::State& state) {
  OrderBook book;
  auto n = static_cast<std::size_t>(state.max_iterations);
  std::vector<OrderId> ids;
  ids.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    OrderId id = static_cast<OrderId>(i + 1);
    book.add_order(MakeOrder(id, Side::Buy, OrderType::Limit, 100, 10));
    ids.push_back(id);
  }

  std::vector<double> samples;
  samples.reserve(n);
  std::size_t i = 0;
  for (auto _ : state) {
    auto start = std::chrono::steady_clock::now();
    benchmark::DoNotOptimize(book.cancel_order(ids[i++]));
    auto end = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::nano>(end - start).count());
  }

  lob::bench::ReportPercentiles(state, samples);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Cancel)->Iterations(kIterations);

// Repeatedly modifies the same single resting order between two
// non-crossing price/quantity pairs -- never consumed, so it's reusable
// indefinitely without any pre-population sizing concern.
void BM_Modify(benchmark::State& state) {
  OrderBook book;
  book.add_order(MakeOrder(1, Side::Buy, OrderType::Limit, 100, 10));
  const Price prices[2] = {100, 99};
  const Quantity quantities[2] = {10, 11};
  int idx = 0;

  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(state.max_iterations));

  for (auto _ : state) {
    auto start = std::chrono::steady_clock::now();
    benchmark::DoNotOptimize(book.modify_order(1, prices[idx], quantities[idx]));
    auto end = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::nano>(end - start).count());
    idx ^= 1;
  }

  lob::bench::ReportPercentiles(state, samples);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Modify)->Iterations(kIterations);

}  // namespace

BENCHMARK_MAIN();
