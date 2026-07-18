"""Validates the engine's LOBSTER replay reconstruction against LOBSTER's
own published orderbook file for a real sample day -- the M4 "Real-data
(LOBSTER) validation of replay realism" follow-up (see README/RESULTS.md).

Not part of ctest: needs a real LOBSTER sample file, which isn't ours to
redistribute (see .gitignore's data/lobster/ entry) -- this is a manual,
reproducible analysis script, same category as generate_plots.py.

Run from repo root after building:
    cmake --build build --target lob_bindings -j
    python3 analysis/lobster_validate.py [message_csv] [orderbook_csv] [num_levels]

Defaults to the AAPL 2012-06-21 Level-5 sample day
(data/lobster/AAPL_2012-06-21_{message,orderbook}_5.csv) if no arguments
are given.

Reports two different metrics, deliberately not just one -- see README's
"Real-data (LOBSTER) validation" section for the full writeup of why an
exact full-depth match is not achievable from this data at all (it's a
property of any finite-depth LOBSTER export, not a reconstruction bug):

  1. Exact full-depth match rate: what fraction of comparable rows have
     our num_levels-deep snapshot byte-for-byte identical to LOBSTER's.
     Expected to be near-zero and IS near-zero -- LOBSTER's book is
     already fully populated at row 0 from opening-auction/pre-market
     activity with no Submission record anywhere in this file, so almost
     every row's deeper levels reflect liquidity we have zero information
     about.
  2. Quantity-soundness rate: of the price levels where BOTH sides show
     the same price, what fraction have our size <= LOBSTER's true size
     (never more). This is the metric that actually distinguishes
     "missing information" (expected, our size can only be a subset of
     the truth) from "the engine fabricated liquidity that isn't real"
     (a genuine bug, if it ever happened). It is not 100% either, for a
     second, subtler reason confirmed by manually tracing a specific
     order (see the README): a price level that temporarily falls outside
     the top-N reporting window can have its true size change with zero
     message-file evidence, then reappear at a different size once back
     in view -- a LOBSTER Level-N reporting gap, not a matching-engine
     defect.
"""

import sys
import time

from lob_sweep import lob
from lobster_loader import parse_message_file, parse_orderbook_file, to_replay_events

SIDE_MAP = {"Buy": lob.Side.Buy, "Sell": lob.Side.Sell}


def build_pybind_events(events):
    out = []
    for e in events:
        pe = lob.LobsterReplayEvent()
        if e[0] == "add":
            _, order_id, side, price, qty = e
            pe.kind = lob.LobsterEventKind.Add
            pe.order_id = order_id
            pe.side = SIDE_MAP[side]
            pe.price = price
            pe.quantity = qty
        elif e[0] == "cancel":
            _, order_id = e
            pe.kind = lob.LobsterEventKind.Cancel
            pe.order_id = order_id
        else:
            _, order_id, new_qty = e
            pe.kind = lob.LobsterEventKind.Reduce
            pe.order_id = order_id
            pe.quantity = new_qty
        out.append(pe)
    return out


def compare_row(ours, theirs, num_levels):
    """Returns (exact_match, known_price_levels_checked, quantity_violations)
    for one row -- known_price_levels_checked/quantity_violations only
    count *our* non-empty levels whose price also appears on the same
    side of LOBSTER's row (see module docstring for why)."""
    exact_match = True
    checked = 0
    violations = 0

    their_bids = {lvl.bid_price: lvl.bid_size for lvl in theirs.levels if not lvl.bid_empty}
    their_asks = {lvl.ask_price: lvl.ask_size for lvl in theirs.levels if not lvl.ask_empty}

    for level in range(num_levels):
        a, b = ours.levels[level], theirs.levels[level]
        if a.ask_empty != b.ask_empty or a.bid_empty != b.bid_empty:
            exact_match = False
        elif not a.ask_empty and (a.ask_price != b.ask_price or a.ask_size != b.ask_size):
            exact_match = False
        elif not a.bid_empty and (a.bid_price != b.bid_price or a.bid_size != b.bid_size):
            exact_match = False

        if not a.bid_empty and a.bid_price in their_bids:
            checked += 1
            if a.bid_size > their_bids[a.bid_price]:
                violations += 1
        if not a.ask_empty and a.ask_price in their_asks:
            checked += 1
            if a.ask_size > their_asks[a.ask_price]:
                violations += 1

    return exact_match, checked, violations


def main():
    message_path = sys.argv[1] if len(sys.argv) > 1 else "data/lobster/AAPL_2012-06-21_message_5.csv"
    orderbook_path = (
        sys.argv[2] if len(sys.argv) > 2 else "data/lobster/AAPL_2012-06-21_orderbook_5.csv"
    )
    num_levels = int(sys.argv[3]) if len(sys.argv) > 3 else 5

    t0 = time.time()
    messages = parse_message_file(message_path)
    orderbook_rows = parse_orderbook_file(orderbook_path, num_levels)
    if len(messages) != len(orderbook_rows):
        raise ValueError(
            f"message/orderbook file row-count mismatch: {len(messages)} vs {len(orderbook_rows)}"
        )
    print(f"parsed {len(messages)} paired rows in {time.time() - t0:.2f}s")

    stats = {}
    row_trace = []
    t0 = time.time()
    events = to_replay_events(messages, stats=stats, row_trace=row_trace)
    print(f"converted to {len(events)} replay events in {time.time() - t0:.2f}s")
    print("conversion stats:", stats)

    t0 = time.time()
    snapshots = lob.replay_lobster_events(build_pybind_events(events), num_levels)
    print(f"replayed through the real engine in {time.time() - t0:.2f}s")

    comparable = 0
    exact_matches = 0
    checked_levels = 0
    violations = 0
    for row_idx, event_idx in enumerate(row_trace):
        if event_idx is None:
            continue
        comparable += 1
        exact_match, checked, row_violations = compare_row(
            snapshots[event_idx], orderbook_rows[row_idx], num_levels
        )
        exact_matches += exact_match
        checked_levels += checked
        violations += row_violations

    print(f"\ntotal rows: {len(messages)}")
    print(f"comparable rows (have a post-event snapshot): {comparable} ({100 * comparable / len(messages):.1f}%)")
    print(f"exact full-depth match: {exact_matches} / {comparable} ({100 * exact_matches / comparable:.3f}%)")
    print(f"known-price levels checked: {checked_levels}")
    print(
        f"quantity soundness (our size never exceeds LOBSTER's true size): "
        f"{100 * (1 - violations / checked_levels):.2f}% ({violations} violations)"
    )


if __name__ == "__main__":
    main()
