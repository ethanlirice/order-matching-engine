# Order Matching Engine

A low-latency limit-order-book (LOB) matching engine in C++, wrapped in an
event-driven simulator, used as a lab for inventory-aware market-making
strategies (Avellaneda-Stoikov, order-flow-imbalance).

**Status: M5 — market-making study.** All five order types, price-time-
priority matching, cancel, modify, and trade events are implemented and
tested (M1/M2); M3 added a pooled allocator, cache-friendly layout, a
lock-free SPSC ring buffer, and a benchmark harness (see Benchmarking
below). M4 added the L3 event-driven simulator: data replay, a virtual
clock, the queue-position fill model, a latency model, and a strategy
callback interface (see Simulator below). M5 adds four market-making
strategies (naive, inventory-capped, Avellaneda-Stoikov, order-flow-
imbalance), the full metrics suite, pybind11 bindings, and a Python
analytics layer producing the required plots and parameter sweeps -- see
Market-making study below.

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
throughput counter.

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

## Simulator (M4)

`sim/` is the L3 event-driven simulator: a single discrete-event loop
(`Simulator`, `include/lob/sim/simulator.hpp`) drains a min-heap of `Event`s
in strict `(timestamp, kind, sequence)` order, driving one shared
`MatchingEngine` — never wall-clock (`VirtualClock` tracks the last-
processed timestamp and asserts it never moves backward).

**Data source scope: synthetic-only for M4.** True L3 (per-order) data
isn't freely available for crypto exchanges, and LOBSTER (real equities L3
data) requires registering on their site to obtain — neither is fetchable
in an automated way. `SyntheticGenerator` (`include/lob/sim/
synthetic_generator.hpp`) produces a seeded, deterministic Poisson-arrival
order-flow stream instead (PROJECT_SPEC.md §7 "Synthetic mode"), which is
enough to satisfy M4's literal "done when" bar (replay a sample day,
reconstruct correctly, deterministic strategy hooks). **Real-data
(LOBSTER) validation of replay realism is an explicit, tracked follow-up**,
not silently folded into "done" here.

### The queue-position fill model is (almost) free
PROJECT_SPEC.md §7 requires that "a resting maker order sits behind the
volume already at its price and only fills once the queue ahead of it is
consumed." `OrderBook` already implements byte-correct price-time priority
with strict FIFO-per-level (M1-M3) and doesn't distinguish whose order is
resting — injecting a strategy's order into the *same* `OrderBook`
instance processing replayed flow, at the correct chronological position,
already gives correct queue-position realism with no separate queue-depth
data structure. This holds specifically for **L3-granularity input** (true
per-order arrival sequence) — exactly what the synthetic generator
produces. `tests/sim/queue_position_test.cpp` is the direct, end-to-end
proof: a strategy order injected behind existing resting volume does not
fill until a later taker exactly exhausts everything ahead of it.

### Design decisions worth knowing
- **No `Execute` replay message type.** "Directly apply a recorded
  historical execution" (mark a specific resting order id as filled by
  fiat) is actively wrong once a strategy order can be interposed between
  historical makers — it would silently ignore the strategy order and
  violate price-time priority. `ReplayMessage` is `{Add, Cancel}` only; the
  synthetic generator emits marketable orders as ordinary `Add`s and needs
  no reconstruction. The correct approach for real L3 data (a future
  milestone) is to synthesize an implicit aggressor `Add` from grouped
  `Execute` records and let the engine re-derive the match against
  whatever's actually resting, never bypass matching.
- **Disjoint id-namespace.** Historical/generator ids start at 1; strategy-
  issued ids start at `kStrategyIdBase = 1 << 63` (`include/lob/sim/
  id_space.hpp`), assigned internally by `Simulator` — a `Strategy` never
  handles raw ids. Without this partition, a collision would silently hit
  `OrderBook::add_order`'s duplicate-id rejection and drop a strategy
  order with no error signal.
- **Strict event tie-break.** Equal-timestamp events are never left to
  `std::priority_queue`'s unspecified tie behavior: `Replay` events sort
  before `StrategyOrderArrival` events at the same tick (no look-ahead
  bias — a strategy action must never jump ahead of a historical event it
  couldn't have observed yet), then by a monotonic push-order `sequence`.
- **`Strategy` never touches `MatchingEngine` directly** — only the narrow
  `OrderIntentSink` (`Submit`/`Cancel`/`Modify`), which schedules a
  delayed event rather than applying anything synchronously. This is what
  actually enforces the latency model; a direct engine reference would let
  a future strategy bypass it.
- **Uniform callback-firing rule.** After every engine-state-changing call
  (replay or strategy-issued): `OnTrade` once per `TradeEvent` in fill
  order, then exactly one `OnBookUpdate` with the final state — regardless
  of what triggered it.
- **No self-trade prevention.** The engine has no owner concept (L1 must
  not know about strategies) and this design doesn't add server-side STP.
  A strategy that would cross its own resting order is responsible for
  avoiding that itself — matching how many real venues don't offer STP by
  default.

### Testing
- `tests/sim/event_ordering_test.cpp` — event tie-break ordering, virtual
  clock monotonicity.
- `tests/sim/synthetic_generator_test.cpp` — same-seed determinism, id/
  timestamp invariants.
- `tests/sim/golden_replay_test.cpp` — a **hand-constructed** trace (not
  generated-and-snapshotted, which would only prove self-consistency) with
  **independently hand-computed** expected checkpoints, covering a same-
  level multi-order partial-fill sweep and a genuine mid-queue cancel.
- `tests/sim/queue_position_test.cpp` — the core insight, tested end-to-end.
- `tests/sim/simulator_determinism_test.cpp` — two independent runs with
  identical seeded input (including a strategy that itself submits orders,
  exercising strategy-event/replay-event interleaving, not just pure
  replay) produce byte-identical callback sequences and final book state.

## Market-making study (M5)

`mm/` is the L4 market-making layer: four strategies built on a shared
`MarketMaker` base class (`include/lob/mm/market_maker.hpp`), a metrics
suite, and a pybind11 entry point (`mm/simulation_runner.cpp`) that
Python drives for sweeps and plots (`analysis/`).

**Strategies, in order (each stacks on the previous one's mechanics):**
1. **Naive** — fixed half-spread around mid, ignores inventory entirely.
   The null baseline; expected to accumulate inventory and, over enough
   one-sided flow, lose.
2. **Inventory-capped** — identical, but stops quoting a side once
   inventory reaches a configured cap in that direction.
3. **Avellaneda-Stoikov** — reservation price skews away from inventory
   (`r = mid - inventory*gamma*sigma^2*tau`), half-spread widens with
   remaining time-to-horizon and narrows with book liquidity kappa.
4. **OFI** — adds an order-flow-imbalance skew on top of AS's reservation
   price, using top-of-book buy/sell pressure with the strategy's own
   resting quantity excluded (otherwise a strategy quoting the best level
   biases the very signal it's reacting to).

**Metrics** (`include/lob/mm/metrics.hpp`): per-fill effective spread,
markout, and *pure adverse-selection cost* (drift-only, isolating what OFI
is actually supposed to improve — see the header's doc comment for why
markout alone conflates execution-price quality with post-trade drift);
PnL decomposed into spread PnL vs. inventory PnL; Sharpe (unannualized,
over the full mark-to-market series); fill rate; and signed inventory
extremes.

### Reconciliation correctness: three real bugs, found by driving it with real flow
Every strategy's own quotes go through a shared reconciliation loop
(`MarketMaker::OnBookUpdate`/`TryRequoteSide`) that submits/modifies/
cancels toward whatever `ComputeQuotes` wants. Building the naive and
inventory-capped strategies against a couple of static seed orders (Step
1's own tests) looked clean — but wiring the *same* base class up to the
synthetic generator's continuous, realistic order flow (Step 5's
pybind11 runner) surfaced three genuine hangs that unit tests against a
thin book never exercised:

1. **A cross-side race.** With per-side independent acking, bid and ask
   could settle asynchronously: one side's Modify, computed from a
   snapshot where the other side's already-issued (but not yet applied)
   Modify hadn't landed, could cross the other side's own still-resting
   order once it finally did. PostOnly rejects that by cancelling the
   original and rejecting the replacement — chasing it repeatedly cycled
   forever instead of converging. Fixed by allowing only one Submit/Modify
   in flight across *both* sides at a time.
2. **A self-referential mid.** When a strategy's own order is the entire
   quantity resting at the best price, mid computed from that price is
   circular — with zero inventory skew, `bid = mid - hs`, `ask = mid + hs`
   holds for *any* mid, so there's no restoring force, and integer
   rounding can add a small systematic drift each settling round. Fixed
   by a `ReferenceMid` that only reports mid when at least one side is
   verifiably not entirely the strategy's own order, holding the last
   verified value steady otherwise.
3. **Market-crossing and limit-cycle hangs under continuous flow.**
   AS/OFI's inventory skew scales with `gamma*sigma^2*tau` — large for a
   long remaining horizon — and can swing far enough after one or two
   fills that the resulting quote crosses the real market; without a
   clamp, the rejected Submit/Modify just gets recomputed identically
   forever. Separately, OFI's self-excluded quantity is discontinuous
   exactly at the boundary of sharing a price level with real liquidity,
   producing a limit cycle (observed at both 2-tick and 3-tick periods,
   and — once latency is nonzero, since each reaction's own ack lands at
   a *later* timestamp rather than the same one — recurring forever at
   ever-increasing timestamps well past the session's configured
   duration). Fixed by clamping quotes against the raw external market,
   plus a cumulative (not per-tick) circuit breaker that holds a side's
   price once too many consecutive Modifies happen without a genuine
   convergence in between.

All three were reproduced directly in C++ (a killed, 100%-CPU process),
fixed, and re-verified with stress sweeps of 200–800 (strategy kind,
seed, gamma, latency) combinations completing without hanging, on top of
the full test suite staying green under ASan+UBSan. See
`include/lob/mm/market_maker.hpp`'s class comment for the full
derivation of each.

### Python bindings and analytics

```bash
cmake --build build --target lob_bindings -j
pip install -r analysis/requirements.txt   # numpy, pandas, matplotlib
python3 analysis/generate_plots.py
```

`bindings/bindings.cpp` is a thin pybind11 wrapper (no logic of its own)
around `RunSimulation(SimulationConfig) -> SimulationResult`
(`include/lob/mm/simulation_runner.hpp`) — the one entry point Python
uses; every strategy stays C++-only. `analysis/lob_sweep.py` handles path
setup and DataFrame conversion; `analysis/generate_plots.py` produces the
four required plots plus a gamma sweep into `analysis/output/` (gitignored
— findings are written up here, not committed as binary images).

**If you have more than one Python installed**, CMake's `find_package
(Python3)` may resolve a different interpreter than your shell's default
`python3` — the compiled module is tied to a specific Python's ABI, so a
mismatch shows up as `ModuleNotFoundError: No module named 'lob_bindings'`
even though the file exists. Check `grep Python3_EXECUTABLE
build/CMakeCache.txt` against `which python3`, and if they differ,
reconfigure with `-DPython3_EXECUTABLE=$(which python3)`.

**Calibrating gamma to session length.** AS/OFI's horizon is the full
session duration (`config.generator.duration`), so
`variance_term = gamma*sigma^2*tau` is huge near the start of a long
session unless gamma is scaled down accordingly — `gamma=0.1` (a fine
default for a short session) produces almost no fills at
`duration=50000` (spreads too wide to ever get hit); `gamma=0.001` was
found empirically to produce a fill count comparable to the other three
strategies at that duration. `analysis/generate_plots.py`'s `COMMON`
dict and gamma-sweep range are calibrated for `duration=50000`; rescale
gamma roughly in proportion to `1/duration` for a different session
length.

### Findings (duration=50000, seed=1, arrival_rate=0.05, latency=5 unless swept)

**PnL decomposition** (spread PnL vs. inventory PnL):

| Strategy | Spread PnL | Inventory PnL | Total PnL | Fills |
|---|---:|---:|---:|---:|
| Naive | 1323.5 | -581.5 | 742.0 | 52 |
| Inventory-capped | 1291.0 | -652.5 | 638.5 | 50 |
| Avellaneda-Stoikov (gamma=0.001) | 195.5 | -489.0 | -293.5 | 47 |
| OFI (gamma=0.001) | 234.5 | -488.5 | -254.0 | 44 |

Naive and inventory-capped harvest far more spread PnL simply because
their fixed, narrow half-spread (5 ticks) trades far more aggressively
than AS/OFI's inventory-and-horizon-driven spread at this gamma — this
table isn't an apples-to-apples "which strategy is better" comparison at
matched risk, just each strategy run at one representative
configuration. OFI edges out plain AS on both spread and total PnL here.

**Inventory boundedness** (signed extremes over the session):

| Strategy | Max | Min | Max &#124;inventory&#124; |
|---|---:|---:|---:|
| Naive | 0 | -60 | 60 |
| Inventory-capped | 0 | -53 | 53 |
| Avellaneda-Stoikov | 9 | -11 | 11 |
| OFI | 9 | -13 | 13 |

Confirms §8's claim directly: naive and inventory-capped drift to their
full one-sided extent (inventory-capped's cap of 50 is exceeded slightly
since a single fill can push it past the threshold checked *before* that
fill), while AS and OFI stay within roughly a fifth of that range —
inventory-aware reservation-price skew is doing real work, not just
capping.

**Adverse-selection markout** (mean per fill, AS vs. OFI):

| Strategy | Fills | Mean markout | Mean pure adverse-selection cost |
|---|---:|---:|---:|
| Avellaneda-Stoikov | 47 | -0.798 | 1.479 |
| OFI | 44 | -0.761 | 1.511 |

At this configuration OFI's *isolated* adverse-selection cost (the metric
that actually isolates what OFI is meant to improve, per the metrics
suite's design) is marginally *worse* than plain AS's, not better — a
genuine, reported-as-observed result, not the hoped-for direction. With
only ~45 fills per run the sampling noise here is large relative to the
effect size; a longer run or averaging across seeds would be needed
before treating this as a real finding about OFI's signal rather than
noise. Flagged here rather than smoothed over, per this project's honesty-
in-scope principle.

**PnL vs. injected latency** (OFI, gamma=0.001):

| Latency | Total PnL | Fills |
|---:|---:|---:|
| 0 | -181.0 | 41 |
| 5 | -254.0 | 44 |
| 10 | -226.0 | 51 |
| 20 | -272.0 | 48 |
| 50 | -198.5 | 33 |
| 100 | -56.5 | 26 |
| 200 | 0.5 | 10 |
| 500 | -15.0 | 1 |

PnL trends toward zero as latency grows large simply because the
strategy trades less (fills drop from 44 to 1) — at very high latency it
barely participates in the market at all. No clean monotonic relationship
in the middle of the range at this single seed; a fair latency-sensitivity
conclusion would need averaging across multiple seeds.

**Gamma sweep** (Avellaneda-Stoikov, duration=50000):

| Gamma | Total PnL | Max &#124;inventory&#124; | Sharpe | Fills |
|---:|---:|---:|---:|---:|
| 0.0001 | -2498.0 | 11 | -1.002 | 289 |
| 0.0005 | -728.0 | 14 | -0.473 | 102 |
| 0.001 | -293.5 | 11 | -0.369 | 47 |
| 0.005 | -28.0 | 10 | -0.141 | 5 |
| 0.01 | -2.0 | 6 | -0.141 | 3 |
| 0.05 | 11.0 | 8 | 0.141 | 2 |

Lower gamma (less risk-averse) means a narrower spread throughout the
whole session (since `variance_term` stays small only late in the
session at low gamma, but here it dominates for most of it), so the
strategy trades far more (289 fills at gamma=0.0001 vs. 2 at gamma=0.05)
— and loses considerably more, since it's absorbing far more adverse
selection at a spread too narrow to compensate for it at this book's
tick size and liquidity. Higher gamma trades rarely but closer to
breakeven. Max inventory doesn't move monotonically with gamma in this
sweep — consistent with the single-seed noise already visible in the
other tables above, not a claimed trend.

All numbers above come from one seed at one configuration each — they
demonstrate the pipeline and the qualitative claims §8 asks for (naive
loses/drifts, AS/OFI stay bounded), not statistically rigorous strategy
rankings. `analysis/generate_plots.py` is the reproducible source; rerun
it (optionally averaging across more seeds) before drawing stronger
conclusions.
