"""Parses LOBSTER-format L3 message/orderbook CSVs into normalized events.

LOBSTER (data.lobsterdata.com) reconstructs a Nasdaq ITCH order book into
two paired, header-less CSVs per trading day: a message file (one row per
book-changing event: Time, Type, Order_ID, Size, Price, Direction; Price in
units of 1/10000 dollar, e.g. $91.14 -> 911400) and an orderbook file (one
row per event, giving the resulting top-N levels as repeating
[Ask Price, Ask Size, Bid Price, Bid Size] quadruples; an unoccupied ask
(bid) level is the sentinel price 9999999999 (-9999999999) with size 0).

to_replay_events converts Submission/Deletion/Partial-cancellation/
Execution-of-a-visible-order onto lob::sim::ReplayMessage's Add/Cancel/
Reduce model (include/lob/sim/replay_message.hpp), now that
OrderBook::ReduceQuantity exists for priority-preserving partial cancels
and Simulator dispatches Reduce events. Still deliberately NOT wired into
a runnable C++ replay path here -- this module only produces the
normalized event list; feeding it through Simulator and diffing against
LOBSTER's own orderbook file is the next, separate step.
"""

from dataclasses import dataclass

# LOBSTER message event types (data.lobsterdata.com's public format doc).
SUBMISSION = 1
PARTIAL_CANCELLATION = 2
DELETION = 3
EXECUTION_VISIBLE = 4
EXECUTION_HIDDEN = 5
CROSS_TRADE = 6
TRADING_HALT = 7

# Orderbook-file sentinel for an unoccupied price level.
EMPTY_ASK_PRICE = 9999999999
EMPTY_BID_PRICE = -9999999999


@dataclass(frozen=True)
class LobsterMessage:
    time_seconds: float
    event_type: int
    order_id: int
    size: int
    price: int  # dollars * 10000
    direction: int  # -1 = sell/ask-side order, 1 = buy/bid-side order


def parse_message_line(line):
    time_str, type_str, order_id_str, size_str, price_str, direction_str = line.strip().split(",")[:6]
    return LobsterMessage(
        time_seconds=float(time_str),
        event_type=int(type_str),
        order_id=int(order_id_str),
        size=int(size_str),
        price=int(price_str),
        direction=int(direction_str),
    )


def parse_message_file(path):
    with open(path, newline="") as f:
        return [parse_message_line(line) for line in f if line.strip()]


@dataclass(frozen=True)
class OrderBookLevel:
    ask_price: int
    ask_size: int
    bid_price: int
    bid_size: int
    ask_empty: bool
    bid_empty: bool


@dataclass(frozen=True)
class OrderBookSnapshot:
    levels: list  # OrderBookLevel, best-to-worst


def parse_orderbook_line(line, num_levels):
    fields = [int(x) for x in line.strip().split(",")]
    expected = num_levels * 4
    if len(fields) != expected:
        raise ValueError(f"expected {expected} fields for {num_levels} levels, got {len(fields)}")
    levels = []
    for i in range(num_levels):
        ask_price, ask_size, bid_price, bid_size = fields[4 * i : 4 * i + 4]
        levels.append(
            OrderBookLevel(
                ask_price=ask_price,
                ask_size=ask_size,
                bid_price=bid_price,
                bid_size=bid_size,
                ask_empty=(ask_price == EMPTY_ASK_PRICE),
                bid_empty=(bid_price == EMPTY_BID_PRICE),
            )
        )
    return OrderBookSnapshot(levels=levels)


def parse_orderbook_file(path, num_levels):
    with open(path, newline="") as f:
        return [parse_orderbook_line(line, num_levels) for line in f if line.strip()]


class UnsupportedLobsterEvent(NotImplementedError):
    """Raised for LOBSTER event types with no equivalent at all in the
    current replay model -- not a to-do, a genuine model gap."""


def _side(direction):
    if direction == 1:
        return "Buy"
    if direction == -1:
        return "Sell"
    raise ValueError(f"unexpected LOBSTER direction {direction}")


# Aggressor ids synthesized for grouped Execution (type 4) events -- kept
# well clear of both real LOBSTER order_ids (small increasing integers)
# and Simulator's own kStrategyIdBase = 1 << 63 (include/lob/sim/
# id_space.hpp), so a converted LOBSTER replay could run alongside a live
# strategy without an id collision.
AGGRESSOR_ID_BASE = 1 << 61


def to_replay_events(messages, stats=None, row_trace=None):
    """Converts LOBSTER messages to (kind, ...) tuples matching
    lob::sim::ReplayMessage's Add/Cancel/Reduce model
    (include/lob/sim/replay_message.hpp).

    Handles:
      - Submission (1) -> ("add", order_id, side, price, size).
      - Deletion (3) -> ("cancel", order_id).
      - Partial cancellation (2) -> ("reduce", order_id, new_size), where
        new_size = the locally-tracked resting size minus this message's
        Size (LOBSTER's Size field on a type-2 row is the delta removed,
        not the resulting total) -- preserves FIFO priority via
        OrderBook::ReduceQuantity, unlike a cancel-and-re-add.
      - Execution of a visible order (4) -> one synthesized aggressor
        ("add", ...) per group of consecutive same-timestamp,
        same-direction type-4 rows (LOBSTER gives every resting order an
        incoming aggressor sweep touches the identical Time value, empi-
        rically verified against the AAPL 2012-06-21 sample: groups up to
        43 rows). The synthesized order is a marketable Limit priced at
        the group's worst touched level, sized to the group's total --
        never applies the recorded fill directly (see
        replay_message.hpp's header comment for why bypassing matching is
        wrong once a strategy order can be interposed); the *engine*
        re-derives the match against whatever is actually resting in the
        replayed book, so any earlier divergence shows up as a real
        matching difference instead of being silently papered over.
      - Execution of a hidden order (5) -> skipped, not approximated. A
        hidden order was never displayed, so by definition its execution
        cannot change the displayed book -- correctly zero ReplayMessages,
        not a gap. Documented cost: the resulting trade print doesn't
        appear in the replayed engine's trade_log, so trade-tape-level
        (as opposed to book-state-level) fidelity is reduced by exactly
        this many events.

    Pre-existing ("untracked") resting orders: a finite-depth LOBSTER
    export (e.g. Level 1) only records a Submission for an order once it's
    within the requested depth -- an order resting deeper that later gets
    partially cancelled or executed (without ever crossing into view) has
    no Submission row at all in that file, even though it was genuinely
    resting the whole time. Empirically ~12.6% of type-2/3/4 rows in the
    AAPL 2012-06-21 Level-1 sample reference such an id (verified against
    the real downloaded file, not assumed). This isn't a bug to raise on
    -- it's a real, quantifiable gap in what a shallow export can
    reconstruct. Handling, all counted into `stats` if provided:
      - Deletion (3) of an untracked id: already a no-op both here and in
        OrderBook::cancel_order on an unknown id, so nothing extra needed.
      - Partial cancellation (2) of an untracked id: dropped (can't reduce
        an order our book was never given).
      - Execution (4) rows against an untracked id are filtered out of
        their group before the aggressor is synthesized (both from the
        group's total size and from its worst-price computation) --
        including them would make the aggressor consume *other*,
        genuinely-tracked resting orders that the real aggressor never
        touched, corrupting the reconstruction further rather than
        approximating it. A group left with zero known rows emits no
        aggressor at all.

    Still raises UnsupportedLobsterEvent for cross trade (6) and trading
    halt (7): neither has any equivalent in the current replay model, and
    approximating either would risk a replay that looks plausible but
    isn't provably faithful -- exactly what this project's
    no-unmeasured-claims principle rules out.

    If `row_trace` (a list) is passed, it's populated with one entry per
    input message, in order: the index into the returned events list whose
    resulting book state that message's row is comparable against, or None
    if there isn't one (a skip of any kind above, or a non-last row within
    a grouped execution -- the group produces exactly one post-group
    snapshot, not one per swept order, so only the group's last row lines
    up with a real post-event state). This is what lets a caller diff
    against LOBSTER's own per-row orderbook CSV without misattributing an
    expected, already-documented gap as a reconstruction bug.
    """
    if stats is None:
        stats = {}
    stats.setdefault("hidden_executions_skipped", 0)
    stats.setdefault("untracked_reduce_skipped", 0)
    stats.setdefault("untracked_execution_rows_skipped", 0)
    stats.setdefault("execution_groups", 0)
    stats.setdefault("execution_groups_fully_untracked", 0)
    if row_trace is not None:
        row_trace.clear()

    events = []
    resting_size = {}  # order_id -> current resting size, tracked as we go
    next_aggressor_id = AGGRESSOR_ID_BASE
    i = 0
    n = len(messages)
    while i < n:
        m = messages[i]
        if m.event_type == SUBMISSION:
            events.append(("add", m.order_id, _side(m.direction), m.price, m.size))
            resting_size[m.order_id] = m.size
            if row_trace is not None:
                row_trace.append(len(events) - 1)
            i += 1
        elif m.event_type == DELETION:
            events.append(("cancel", m.order_id))
            resting_size.pop(m.order_id, None)
            if row_trace is not None:
                row_trace.append(len(events) - 1)
            i += 1
        elif m.event_type == PARTIAL_CANCELLATION:
            current = resting_size.get(m.order_id)
            if current is None:
                stats["untracked_reduce_skipped"] += 1
                if row_trace is not None:
                    row_trace.append(None)
                i += 1
                continue
            new_size = current - m.size
            if new_size <= 0:
                raise ValueError(
                    f"partial cancellation left non-positive size ({new_size}) for "
                    f"order_id={m.order_id} at time={m.time_seconds} -- expected a Deletion "
                    "(type 3), not a type-2, for a full cancel"
                )
            events.append(("reduce", m.order_id, new_size))
            resting_size[m.order_id] = new_size
            if row_trace is not None:
                row_trace.append(len(events) - 1)
            i += 1
        elif m.event_type == EXECUTION_HIDDEN:
            stats["hidden_executions_skipped"] += 1
            if row_trace is not None:
                row_trace.append(None)
            i += 1
        elif m.event_type == EXECUTION_VISIBLE:
            group_end = i + 1
            while (
                group_end < n
                and messages[group_end].event_type == EXECUTION_VISIBLE
                and messages[group_end].time_seconds == m.time_seconds
                and messages[group_end].direction == m.direction
            ):
                group_end += 1
            group = messages[i:group_end]
            stats["execution_groups"] += 1

            known_rows = [row for row in group if row.order_id in resting_size]
            stats["untracked_execution_rows_skipped"] += len(group) - len(known_rows)

            if known_rows:
                aggressor_side = _side(-m.direction)
                total_size = sum(row.size for row in known_rows)
                # Must be at least as extreme as every touched known level
                # so the engine's own matching walks through exactly those
                # levels -- max price touched for a buy aggressor
                # (sweeping asks), min for a sell aggressor (sweeping
                # bids). Computed from the known rows' own prices, not
                # assumed pre-sorted by row order.
                prices = [row.price for row in known_rows]
                aggressor_price = max(prices) if aggressor_side == "Buy" else min(prices)

                events.append(
                    ("add", next_aggressor_id, aggressor_side, aggressor_price, total_size)
                )
                next_aggressor_id += 1
                group_event_index = len(events) - 1
            else:
                stats["execution_groups_fully_untracked"] += 1
                group_event_index = None

            if row_trace is not None:
                last = len(group) - 1
                for idx_in_group in range(len(group)):
                    row_trace.append(group_event_index if idx_in_group == last else None)

            for row in known_rows:
                current = resting_size[row.order_id]
                remaining = current - row.size
                if remaining < 0:
                    raise ValueError(
                        f"execution over-fills order_id={row.order_id} at time={row.time_seconds} "
                        f"(tracked size {current}, executed {row.size})"
                    )
                if remaining == 0:
                    resting_size.pop(row.order_id, None)
                else:
                    resting_size[row.order_id] = remaining

            i = group_end
        elif m.event_type in (CROSS_TRADE, TRADING_HALT):
            raise UnsupportedLobsterEvent(
                f"event_type={m.event_type} at order_id={m.order_id}, time={m.time_seconds} "
                "has no equivalent in the current replay model -- see to_replay_events' docstring"
            )
        else:
            raise ValueError(f"unknown LOBSTER event_type {m.event_type}")
    return events
