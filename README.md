# Order Matching Engine

A low-latency limit-order-book (LOB) matching engine in C++, wrapped in an
event-driven simulator, used as a lab for inventory-aware market-making
strategies (Avellaneda-Stoikov, order-flow-imbalance).

**Status: M3 — make it fast.** All five order types, price-time-priority
matching, cancel, modify, and trade events are implemented and tested
(M1/M2). M3 adds a pooled allocator, a cache-friendly `Order` layout, a
lock-free SPSC ring buffer, a percentile-reporting benchmark harness, and
an array-vs-tree evaluation for price levels -- see Benchmarking below.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Sanitized build (ASan + UBSan)

```bash
cmake -S . -B build -DLOB_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Fuzzing

The matching engine has a libFuzzer harness (`tests/fuzz/fuzz_matching.cpp`)
that decodes a mutated byte buffer into a sequence of add/cancel/modify
operations and checks every invariant after each one. It requires real LLVM
clang (libFuzzer's runtime isn't shipped by AppleClang on macOS); a short
smoke run is wired into `ctest` automatically whenever the compiler
qualifies. To fuzz for longer locally (e.g. via Homebrew's llvm on macOS):

```bash
cmake -S . -B build-fuzz \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang
cmake --build build-fuzz -j
./build-fuzz/tests/lob_fuzz_matching -max_total_time=60
```

## Concurrency testing (ThreadSanitizer)

The SPSC ring buffer (`include/lob/spsc_ring_buffer.hpp`) is the project's
first genuine concurrent component, so it gets a dedicated ThreadSanitizer
build -- TSan can't coexist with ASan/UBSan in the same binary, so this is
a separate, minimal executable that doesn't even link the main `lob`
library (the ring buffer is header-only and needs nothing from it):

```bash
cmake -S . -B build-tsan -DLOB_BUILD_TSAN_TESTS=ON -DLOB_BUILD_BENCHMARKS=OFF -DLOB_BUILD_FUZZERS=OFF
cmake --build build-tsan --target lob_tsan_tests -j
ctest --test-dir build-tsan --output-on-failure -R SpscRingBufferTest
```

## Benchmarking

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/benchmarks/lob_bench
```

Build **without** `-DLOB_ENABLE_SANITIZERS=ON` (the default) for meaningful
numbers: `lob` links ASan/UBSan flags publicly when that option is on, and
sanitizer instrumentation overhead would otherwise skew every measurement.
CI still builds and runs the benchmark binary once under sanitizers as a
smoke check (crash-free, not for timing) -- see `.github/workflows/ci.yml`.

**Environmental controls, disclosed honestly:** these numbers were
collected on a shared dev laptop (macOS, Apple Silicon) and on GitHub-
hosted CI runners -- neither is an isolated bare-metal lab environment.
Thread pinning/affinity and CPU frequency-scaling control aren't reliably
available in either (`lob_bench` prints a "failed to set thread affinity"
warning on this machine, confirming as much). What's actually done: each
benchmark pre-populates a realistic book depth (warm-up), then samples
100,000 individual operation latencies via `std::chrono::steady_clock`,
reporting p50/p90/p99/p99.9/p99.99 alongside Google Benchmark's own
throughput counter -- a repeatable measurement methodology under real-world
noise, not a claim of rigorous lab conditions.

### Matching engine (this machine, unsanitized Release build)

| Operation | Baseline p50 | +Pool p50 | +Cache layout p50 | Baseline throughput | Final throughput |
|---|---|---|---|---|---|
| Add (no cross) | 41ns | 41ns | 41ns | 15.3M/s | 16.5M/s |
| Add (single-level cross) | 41ns | 41ns | 41ns | 20.0M/s | 19.4M/s |
| Add (5-level cross) | 375ns | 292ns | 292ns | 977K/s | 1.04M/s |
| Cancel | 41ns | 41ns | 41ns | 17.6M/s | 20.4M/s |
| Modify | 83ns | 42ns | 42ns | 10.7M/s | 12.8M/s |

### Optimization → latency delta attribution

| Optimization | Operation | Before | After | Delta |
|---|---|---|---|---|
| M3 Step 1: Order-object memory pool | Modify (cancel+re-add path) | 83ns | 42ns | **-49%** |
| M3 Step 1: Order-object memory pool | Add (5-level cross) | 375ns | 292ns | **-22%** |
| M3 Step 1: Order-object memory pool | Add/Cancel (simple, no churn) | 41ns | 41ns | ~0% (already at this platform's clock-resolution floor) |
| M3 Step 2: cache-friendly `Order` layout (`alignas(64)`) | all ops | — | — | flat, as expected: single-threaded, so there's no concurrent access to have false-shared a cache line yet. Structural groundwork for M4's thread split, not a win measurable now. |

Scope note: "zero allocation after warm-up" (PROJECT_SPEC.md §6) is scoped
precisely to `Order`-object allocation. `order_index_` remains a standard
node-based `unordered_map`, which still heap-allocates a node per
`emplace()` regardless of `reserve()` -- a smaller, separately documented
residual cost this milestone doesn't eliminate (a custom allocator for that
would add real portability risk across libstdc++/libc++ for a comparatively
small marginal win).

### SPSC ring buffer (new component, not a delta)

| Benchmark | Result |
|---|---|
| Single-threaded push+pop round trip | p50 41ns |
| Genuine two-thread throughput | ~49M items/sec |

### Array-vs-tree price levels (§5.2 — evaluated, not adopted)

Standalone comparison (not wired into `OrderBook`): identical random
insert/lookup/erase workload over a realistic ±500-tick near-touch window.

| Container | Mean | Throughput |
|---|---|---|
| `std::map<Price, Level>` (current, production) | 48.3ns | 20.7M/s |
| Flat array (`std::vector<std::optional<Level>>`) | 36.3ns | 27.5M/s |

**Decision: keep `std::map` in production.** The flat array's ~25-33% win
is real and reproducible across repeated runs, but a fixed-range array
needs a policy for prices that drift outside the pre-allocated window
(real exchanges typically re-center or fall back to a secondary structure)
-- added complexity and re-verification surface (all of M1/M2's invariant/
determinism/fuzz coverage would need re-checking against a container swap)
that isn't justified yet: this is still a single-threaded engine with no
live feed, so there's no real usage data showing the tree is an actual
bottleneck. Revisit in M4/M5 if that changes.

## Architecture

Four layers, each independently testable:

```
L4  Research / analytics   (Python, pybind11)
L3  Simulation / event loop (C++)
L2  Strategy interface      (C++)
L1  Matching engine / order book (C++)  <- the core
```

Architecture diagram and result plots (from the M5 market-making study)
land in M6; the benchmark table above is maintained as of M3.
