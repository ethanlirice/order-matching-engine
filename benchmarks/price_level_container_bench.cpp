#include <benchmark/benchmark.h>

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <random>
#include <vector>

#include "lob/level.hpp"
#include "support/percentile.hpp"

// PROJECT_SPEC.md §5.2: "evaluate a flat array indexed by price ticks for
// the dense near-touch region... benchmark both and record the result."
// This is a standalone container comparison -- NOT wired into OrderBook.
// Both benchmarks run the identical random workload (try-insert-if-absent,
// lookup, erase-if-we-just-inserted) over a fixed ±kRangeTicks window
// around a reference price, representative of near-touch order flow.

namespace {

using lob::Level;
using lob::Price;

constexpr Price kRangeTicks = 500;
constexpr Price kBasePrice = 10000;
constexpr std::int64_t kIterations = 100000;

// Flat array alternative: O(1) index arithmetic instead of a tree probe,
// contiguous/cache-friendly, at the cost of assuming a bounded price range
// up front (std::map has no such assumption).
class FlatLevels {
 public:
  FlatLevels(Price center, Price range)
      : base_(center - range), slots_(static_cast<std::size_t>(range * 2 + 1)) {}

  bool TryEmplace(Price price) {
    std::optional<Level>& slot = slots_[Index(price)];
    if (!slot.has_value()) {
      slot.emplace(price);
      return true;
    }
    return false;
  }

  Level* Find(Price price) {
    std::optional<Level>& slot = slots_[Index(price)];
    return slot.has_value() ? &*slot : nullptr;
  }

  void Erase(Price price) { slots_[Index(price)].reset(); }

 private:
  std::size_t Index(Price price) const { return static_cast<std::size_t>(price - base_); }

  Price base_;
  std::vector<std::optional<Level>> slots_;
};

void BM_MapInsertLookupErase(benchmark::State& state) {
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<Price> offset(-kRangeTicks, kRangeTicks);

  std::map<Price, Level> levels;
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(state.max_iterations));

  for (auto _ : state) {
    Price price = kBasePrice + offset(rng);

    auto start = std::chrono::steady_clock::now();
    auto [it, inserted] = levels.try_emplace(price, price);
    benchmark::DoNotOptimize(levels.find(price));
    if (inserted) {
      levels.erase(price);
    }
    auto end = std::chrono::steady_clock::now();

    samples.push_back(std::chrono::duration<double, std::nano>(end - start).count());
  }

  lob::bench::ReportPercentiles(state, samples);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MapInsertLookupErase)->Iterations(kIterations);

void BM_FlatArrayInsertLookupErase(benchmark::State& state) {
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<Price> offset(-kRangeTicks, kRangeTicks);

  FlatLevels levels(kBasePrice, kRangeTicks);
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(state.max_iterations));

  for (auto _ : state) {
    Price price = kBasePrice + offset(rng);

    auto start = std::chrono::steady_clock::now();
    bool inserted = levels.TryEmplace(price);
    benchmark::DoNotOptimize(levels.Find(price));
    if (inserted) {
      levels.Erase(price);
    }
    auto end = std::chrono::steady_clock::now();

    samples.push_back(std::chrono::duration<double, std::nano>(end - start).count());
  }

  lob::bench::ReportPercentiles(state, samples);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FlatArrayInsertLookupErase)->Iterations(kIterations);

}  // namespace
