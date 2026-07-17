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
gamma sweep) are in the README, each now a mean ± 95% CI over 30
independent seeds rather than one representative run. Averaging across
seeds changed some conclusions, not just tightened the numbers:

- **Inventory-aware strategies bound inventory tightly and consistently.**
  Inventory-capped, AS, and OFI all show narrow CIs on their max
  |inventory| across seeds (53.6±1.8, 10.7±0.7, 11.2±1.0) — the
  reservation-price skew (and the cap) is doing real, reliable work, not
  just capping late in a favorable run.
- **Naive carries real unbounded tail risk that a single seed can hide
  entirely.** Its single-seed max |inventory| was 60, in line with
  inventory-capped's cap; across 30 seeds its worst case was 992 — nearly
  7x its own 30-seed mean (150.3±67.4) and over 16x inventory-capped's
  cap. The single-seed PnL table previously showed naive as the
  *best*-PnL strategy (742, positive); the 30-seed mean is negative
  (-844.7) with a CI wider than the mean itself. That one favorable seed
  was not representative — it's the clearest example in this project of
  why single-seed findings can point the wrong direction entirely.
- **OFI's isolated adverse-selection improvement still doesn't show up,
  now with a real seed count behind that conclusion.** OFI − AS pure
  adverse-selection cost is +0.347 with a 95% CI half-width of 1.213 —
  not distinguishable from zero. The single-seed pass already guessed
  this was noise; 30 seeds confirms the guess rather than resolving it
  either way. This is reported as an open question, not smoothed into
  either "OFI works" or "OFI doesn't work."
- **Gamma's inventory trend is real and monotonic** (clean, tight CIs at
  every point); **its PnL trend is not**, at least not in the middle of
  the swept range — gamma=0.005 and 0.01 have CIs several times wider
  than their means, meaning a handful of seeds take large losses while
  most don't. Only the two ends of the sweep are solid: very low gamma
  reliably loses a lot, and gamma=0.05 is the only point close to
  breakeven *and* tightly bounded.

## Honest limitations

- Findings above are now averaged over **30 seeds per configuration**
  (`analysis/generate_plots.py`, `NUM_SEEDS = 30`) rather than one — this
  resolved some open questions (gamma's monotonic inventory trend) and
  left others genuinely open (OFI's adverse-selection edge, PnL in the
  middle of the gamma sweep) rather than manufacturing false confidence.
  30 seeds is still a normal-approximation CI on a fairly small sample,
  not a rigorous hypothesis test — treat "not distinguishable from zero"
  as "no evidence found," not "proven equal." Sharpe is unannualized (no
  real calendar mapping for virtual ticks exists) and is reported as one
  more number among several, not a single ranking metric.
- **Synthetic order flow only.** Real L3 exchange data (e.g. LOBSTER)
  requires registration and wasn't fetchable in an automated way; replay
  realism against real data is a tracked, explicit follow-up, not folded
  into "done."
- **No self-trade prevention**, and no true multi-trader concept at all —
  "no self-cross" is checked as "book never crosses," which is equivalent
  only in the absence of a trader-id concept.
