# Design & Results Summary

A short, standalone recap of the engineering decisions and findings behind
this project — for a reader who wants the substance without the full
README/`analysis/` walkthrough. See the README for build/run instructions,
the complete benchmark table, and the full findings write-up with every
sweep table.

## What this is

A C++20 limit-order-book matching engine (L1), an event-driven simulator
with a virtual clock and queue-position fill model (L3), and four
market-making strategies of increasing sophistication (L4), built strictly
in that order: correctness before speed, one milestone fully green before
the next begins. Four strategies — naive, inventory-capped, Avellaneda-
Stoikov, order-flow-imbalance — share one reconciliation base class and are
driven by identical synthetic order flow so their behavior is directly
comparable.

## Key engineering decisions

- **Correctness gated every optimization.** M1's matching invariants (no
  crossed book, quantity conservation, price-time priority) were covered by
  unit tests and a libFuzzer harness *before* M3's memory pool or cache
  layout work started. No optimization landed without a before/after
  latency number.
- **Order-object pooling, not allocator tricks.** `OrderPool` is a chunked
  free-list keyed by the observation that `Order`'s lifetime is fully
  owned/controlled internally — no need for a general-purpose allocator
  replacement. It removed ASan's use-after-free safety net for `Order`
  (a stale pointer now reads valid, reused memory), which was compensated
  with debug-build poisoning on release rather than left as a silent gap.
- **Array-vs-tree was benchmarked and *not* adopted.** A flat-array price
  index measured ~25-33% faster than `std::map` in isolation, but was kept
  out of production: a fixed-range array needs a re-centering policy for
  prices drifting outside the window, and there was no profiling evidence
  yet (single-threaded, no live feed) that the tree is an actual
  bottleneck. Written up as a rejected-with-reasons decision, not silently
  dropped.
- **The queue-position fill model turned out to be (almost) free.** The
  naive worry going into M4 was that realistic fill modeling would need a
  separate queue-depth data structure. It didn't: `OrderBook` already
  enforces strict FIFO-per-level and doesn't distinguish whose order is
  resting, so injecting a strategy order into the *same* book at the
  correct chronological position gives correct queue-position realism for
  free, as long as the input is true per-order (L3) granularity.
- **No `Execute` replay message type.** Directly applying a recorded
  historical execution would silently ignore a strategy order interposed
  in front of it, violating price-time priority. Replay is `{Add, Cancel}`
  only; a future real-L3-data replay must synthesize an implicit aggressor
  `Add` from grouped executions and let the engine re-derive the match,
  never bypass matching.
- **The latency model is enforced structurally, not by convention.** A
  `Strategy` can only reach `OrderIntentSink` (`Submit`/`Cancel`/`Modify`),
  never `MatchingEngine` directly — every action schedules a delayed
  event. A direct engine reference would have made "respect the latency
  model" an easily-violated house rule instead of something the type
  system prevents.
- **No self-trade prevention, by design.** L1 has no owner/trader concept
  (a strategy's own resting order is indistinguishable from anyone else's),
  matching real venues that don't offer STP by default. This is a scope
  decision, not an oversight — documented so it isn't mistaken for a bug.

## Reconciliation correctness: three bugs found by driving it with real flow

Every strategy submits/modifies/cancels through one shared reconciliation
loop. It looked correct against a couple of static seed orders (the first
strategies' own unit tests) — but wiring the identical code up to
continuous synthetic order flow (needed for the pybind11 runner) produced
three genuine infinite-hang bugs that a thin, static book never exercised:

1. **Cross-side race.** Bid and ask acked independently, so one side could
   compute its next quote from a snapshot where the other side's
   already-issued Modify hadn't landed yet — occasionally producing a
   quote that crossed the other side's own still-resting order once that
   Modify did land. PostOnly's crossing-rejection response to that (cancel
   original, reject replacement) then got recomputed identically forever.
   **Fix:** at most one Submit/Modify in flight across both sides at once.
2. **Self-referential mid.** When a strategy's own order is the entire
   resting quantity at the best price, "mid" computed from that price has
   no external anchor — `bid = mid - half_spread`, `ask = mid +
   half_spread` is satisfied by *any* mid, so nothing pulls it back to a
   true value, and per-round integer rounding could drift it slowly.
   **Fix:** report mid only when at least one side is verifiably not
   entirely the strategy's own order; hold the last verified value steady
   otherwise.
3. **Limit cycles under continuous flow.** AS/OFI's inventory skew can
   legitimately swing enough after one or two fills to cross the real
   market; rejecting and immediately recomputing the identical crossing
   quote is an unbounded loop. OFI's excluded-quantity signal is also
   discontinuous exactly at the boundary of sharing a price level with
   real liquidity, producing multi-tick cycles that (once latency is
   nonzero) recur forever at ever-later timestamps rather than converging.
   **Fix:** clamp quotes against the raw external market before
   reconciling, plus a cumulative (not per-tick) circuit breaker that
   holds a side's price once too many consecutive Modifies happen without
   a genuine convergence.

**Lesson:** static, hand-picked test scenarios validated the *formulas*
correctly but couldn't have found any of these three — all three are
properties of sustained, continuous self-interaction that only show up
under real flow. Concrete takeaway carried forward: once a stateful
component reacts to its own prior output in a loop, test it end-to-end
against realistic, continuous input, not just isolated static cases, even
after the static cases all pass.

## Findings, in brief

Full tables (PnL decomposition, inventory bounds, markout, latency sweep,
gamma sweep) are in the README, each a mean ± 95% CI over 300 independent
seeds (raised from an initial 30-seed pass specifically to settle two
questions that pass left open). Averaging across seeds — and then
increasing the seed count 10x — changed some conclusions, not just
tightened the numbers:

- **Inventory-aware strategies bound inventory tightly and consistently.**
  Inventory-capped, AS, and OFI all show narrow CIs on their max
  |inventory| across seeds (53.0±0.9, 10.8±0.3, 11.9±0.5 at 300 seeds) —
  the reservation-price skew (and the cap) is doing real, reliable work,
  not just capping late in a favorable run. Unchanged in kind from the
  30-seed pass, just tighter.
- **Naive carries real unbounded tail risk, and it keeps getting worse
  with more sampling, not less.** Single-seed max |inventory| was 60; at
  30 seeds the worst case was 992; at 300 seeds it's 4508 — every larger
  sample has found a worse outlier than the last, with no sign of a
  ceiling. The single-seed PnL table showed naive as the *best*-PnL
  strategy (742, positive); by 300 seeds the mean is solidly negative
  (-678, CI still wider than the mean). That one favorable seed was never
  representative — it's the clearest example in this project of why
  single-seed findings can point the wrong direction entirely.
- **OFI's isolated adverse-selection improvement still doesn't show up,
  now backed by a materially larger seed count.** OFI − AS pure
  adverse-selection cost is +0.252 with a 95% CI half-width of 1.039 at
  300 seeds (was +0.347 ± 1.213 at 30) — still not distinguishable from
  zero. The CI barely tightened despite 10x the seeds, because the larger
  sample revealed more per-fill variance than the smaller one had
  captured, not less. This is now a reasonably solid negative result, not
  an under-powered one.
- **The gamma sweep's PnL noise at 0.005/0.01 wasn't sampling variance
  that would average out — it was a real, previously invisible tail-risk
  failure mode.** At 300 seeds, gamma ≥ 0.005 produces occasional
  catastrophic losses (one seed at gamma=0.05 loses $3.46M from 13
  fills), traced to the reservation-price skew becoming self-reinforcing
  at this uncalibrated a gamma when synthetic liquidity is thin enough
  that there's no real opposing quote for the existing crossing-clamp to
  clamp against. Re-running the same 300 seeds at the calibrated
  gamma=0.001 shows zero such outliers (worst case -25,702), confirming
  this is specific to sweeping gamma outside its calibrated range, not a
  standing correctness bug. The 30-seed pass's read of gamma=0.05 as "the
  only point close to breakeven and tightly bounded" was simply wrong —
  an artifact of not sampling enough seeds to hit the failure mode.

## Honest limitations

- Findings above are averaged over **300 seeds per configuration**
  (`analysis/generate_plots.py`, `NUM_SEEDS = 300`), raised from an
  initial 30-seed pass. Some of what looked like "just sampling noise" at
  30 seeds turned out, at 300, to be a real tail-risk phenomenon (the
  gamma-sweep finding above) rather than something more data would have
  smoothed out — a reminder that "the CI is wide" and "this is noise that
  will average away" are not the same claim. Even at 300, this is a
  normal-approximation CI on a finite sample, not a rigorous hypothesis
  test — treat "not distinguishable from zero" as "no evidence found,"
  not "proven equal." Sharpe is unannualized (no real calendar mapping
  for virtual ticks exists) and is reported as one more number among
  several, not a single ranking metric.
- **Synthetic order flow for the market-making study itself** (M5's
  findings above are all synthetic-generator runs). Real-data replay was
  separately attempted against a real LOBSTER sample day (AAPL,
  2012-06-21) and is no longer an open follow-up so much as a closed one
  with a genuine negative result: exact full-depth book reconstruction
  against a finite-depth LOBSTER export isn't achievable *at all*, for any
  engine, because the file itself doesn't carry enough information (the
  book isn't empty at file-start, and price levels that leave the top-N
  reporting window can change size with zero message-file evidence). See
  README's "Real-data (LOBSTER) validation" section for the full
  investigation, including a specific order traced through the raw file
  to confirm the mechanism rather than assert it. The conversion/replay
  pipeline itself (`analysis/lobster_loader.py`,
  `sim/lobster_replay.cpp`) runs correctly end-to-end on real data; what
  it can't do is what no pipeline could do with this input.
- **No self-trade prevention**, and no true multi-trader concept at all —
  "no self-cross" is checked as "book never crosses," which is equivalent
  only in the absence of a trader-id concept.
