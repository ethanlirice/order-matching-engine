#pragma once

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstddef>
#include <vector>

// Google Benchmark's own timing reports mean/median over a whole loop, not
// per-operation latency percentiles (PROJECT_SPEC.md §10 wants p50/p90/p99/
// p99.9/p99.99). Callers collect one raw nanosecond sample per operation
// into a vector; this sorts it and reports percentiles as custom counters
// alongside Google Benchmark's own throughput/mean numbers.
namespace lob::bench {

inline void ReportPercentiles(benchmark::State& state, std::vector<double>& samples_ns) {
  if (samples_ns.empty()) {
    return;
  }
  std::sort(samples_ns.begin(), samples_ns.end());

  auto percentile = [&](double p) {
    std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(samples_ns.size() - 1));
    return samples_ns[idx];
  };

  state.counters["p50_ns"] = percentile(0.50);
  state.counters["p90_ns"] = percentile(0.90);
  state.counters["p99_ns"] = percentile(0.99);
  state.counters["p99.9_ns"] = percentile(0.999);
  state.counters["p99.99_ns"] = percentile(0.9999);
}

}  // namespace lob::bench
