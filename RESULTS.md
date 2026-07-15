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
gamma sweep) are in the README. The qualitative headline results, from one
representative seed/configuration each:

- **Inventory-aware strategies actually bound inventory.** Naive and
  inventory-capped drift to roughly their full one-sided extent (60 and 53
  units respectively, over the session); AS and OFI stay within about a
  fifth of that range (11 and 13). The reservation-price skew is doing
  real work, not just capping late.
- **Naive/inventory-capped harvest more spread PnL than AS/OFI at this
  configuration** — expected, since their half-spread is fixed and narrow
  while AS/OFI's spread widens with remaining horizon and risk aversion.
  This is *not* an apples-to-apples "better strategy" comparison at
  matched risk; it's each strategy run once at one setting.
- **OFI's isolated adverse-selection improvement did not show up at this
  seed** — its pure adverse-selection cost was marginally *worse* than
  plain AS's, the opposite of the hoped-for direction. Reported as
  observed rather than smoothed over: with ~45 fills in this run, sampling
  noise is large relative to the effect size, and this shouldn't be read
  as a claim about OFI's signal without averaging across more seeds.
- **Gamma trades off fill rate against per-fill risk exactly as the model
  predicts** — low gamma (less risk-averse) trades far more often (289
  fills at gamma=0.0001 vs. 2 at gamma=0.05) and loses more, since a
  narrower spread absorbs more adverse selection than it earns back in
  spread capture at this book's tick size and liquidity.

## Honest limitations

- All findings above are from **one seed at one configuration each** —
  they demonstrate the pipeline and confirm §8's qualitative claims
  (naive loses/drifts, AS/OFI stay bounded), not statistically rigorous
  strategy rankings. Sharpe is unannualized (no real calendar mapping for
  virtual ticks exists) and is reported as one more number among several,
  not a single ranking metric.
- **Synthetic order flow only.** Real L3 exchange data (e.g. LOBSTER)
  requires registration and wasn't fetchable in an automated way; replay
  realism against real data is a tracked, explicit follow-up, not folded
  into "done."
- **No self-trade prevention**, and no true multi-trader concept at all —
  "no self-cross" is checked as "book never crosses," which is equivalent
  only in the absence of a trader-id concept.
