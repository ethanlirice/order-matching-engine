"""Parses LOBSTER-format L3 message/orderbook CSVs into normalized events.

LOBSTER (data.lobsterdata.com) reconstructs a Nasdaq ITCH order book into
two paired, header-less CSVs per trading day: a message file (one row per
book-changing event: Time, Type, Order_ID, Size, Price, Direction; Price in
units of 1/10000 dollar, e.g. $91.14 -> 911400) and an orderbook file (one
row per event, giving the resulting top-N levels as repeating
[Ask Price, Ask Size, Bid Price, Bid Size] quadruples; an unoccupied ask
(bid) level is the sentinel price 9999999999 (-9999999999) with size 0).

This module is deliberately Python-side and NOT yet wired into the C++
Simulator/ReplayMessage pipeline (see to_replay_events' docstring for
exactly which LOBSTER event types don't map onto the current {Add, Cancel}
ReplayMessage model, and why approximating them silently would violate
this project's no-unmeasured-claims principle). It's real, tested
groundwork for that wiring, not a throwaway stand-in -- ready to run
against a real sample day's files as soon as one is available.
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
    """Raised for LOBSTER event types that need a real design decision
    (matching-engine API work or ReplayMessage changes) before they can be
    converted without silently misrepresenting the historical book."""


def _side(direction):
    if direction == 1:
        return "Buy"
    if direction == -1:
        return "Sell"
    raise ValueError(f"unexpected LOBSTER direction {direction}")


def to_replay_events(messages):
    """Converts LOBSTER messages to (kind, ...) tuples matching
    lob::sim::ReplayMessage's Add/Cancel model 1:1 -- the only two kinds
    that model currently supports (include/lob/sim/replay_message.hpp).

    Handles: Submission (1) -> ("add", order_id, side, price, size).
             Deletion (3)   -> ("cancel", order_id).

    Deliberately raises UnsupportedLobsterEvent for every other type
    rather than approximating it:
      - Partial cancellation (2) reduces a resting order's size while
        preserving its FIFO queue position (real exchange semantics).
        OrderBook has no such operation today -- modify_order is
        cancel-then-re-add, which loses queue position instead of
        preserving it. Needs a new L1 API (with its own invariant tests)
        before this can be handled correctly, not a Python-side hack.
      - Execution (4/5) must synthesize an implicit aggressor Add and let
        the engine re-derive the match, never apply the recorded fill
        directly (see replay_message.hpp's header comment for why
        bypassing matching is actively wrong once a strategy order can be
        interposed). That needs grouping consecutive same-timestamp
        execution rows against one synthesized aggressor -- hidden-order
        executions (type 5) can only partially support this since the
        resting hidden order was never in the visible message stream to
        begin with.
      - Cross trade (6) and trading halt (7) have no equivalent in the
        current Add/Cancel/Execute-less replay model at all.
    Silently approximating any of these would produce a replay that looks
    plausible but isn't provably a faithful reconstruction -- exactly what
    this project's measure-everything/no-unmeasured-claims principle
    rules out.
    """
    events = []
    for m in messages:
        if m.event_type == SUBMISSION:
            events.append(("add", m.order_id, _side(m.direction), m.price, m.size))
        elif m.event_type == DELETION:
            events.append(("cancel", m.order_id))
        elif m.event_type in (
            PARTIAL_CANCELLATION,
            EXECUTION_VISIBLE,
            EXECUTION_HIDDEN,
            CROSS_TRADE,
            TRADING_HALT,
        ):
            raise UnsupportedLobsterEvent(
                f"event_type={m.event_type} at order_id={m.order_id}, "
                f"time={m.time_seconds} needs design work -- see to_replay_events' docstring"
            )
        else:
            raise ValueError(f"unknown LOBSTER event_type {m.event_type}")
    return events
