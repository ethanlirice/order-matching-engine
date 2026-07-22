"""Tests for analysis/lobster_loader.py. Uses a small hand-crafted fixture
(tests/python/fixtures/lobster_sample_*.csv) built to match LOBSTER's
documented public format -- NOT real LOBSTER data (that requires
registering at lobsterdata.com; see PROJECT_SPEC.md's data-sources note
and README's tracked follow-up). Deliberately no pytest dependency, same
convention as test_smoke.py: run directly with the module's directory on
PYTHONPATH.

Fixture narrative (tests/python/fixtures/lobster_sample_message.csv):
  t=1  Submission   order=100 Buy  10 @ 100.00
  t=2  Submission   order=200 Sell 20 @ 100.10
  t=3  PartialCancel order=200 -5        -> resting 15
  t=4  Deletion     order=100 (full cancel)
  t=5  Submission   order=300 Sell 25 @ 100.10  (FIFO after 200 at that price)
  t=6  Execution    order=200 15 @ 100.10  \\_ grouped: same timestamp,
  t=6  Execution    order=300 10 @ 100.10  /   same direction
  t=7  HiddenExecution order=999999 50 @ 99.90 (zero visible-book impact)
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "analysis"))

from lobster_loader import (  # noqa: E402  (path must be set up first)
    AGGRESSOR_ID_BASE,
    LobsterMessage,
    UnsupportedLobsterEvent,
    parse_message_file,
    parse_orderbook_file,
    parse_orderbook_line,
    to_replay_events,
)

FIXTURES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
MESSAGE_FIXTURE = os.path.join(FIXTURES_DIR, "lobster_sample_message.csv")
ORDERBOOK_FIXTURE = os.path.join(FIXTURES_DIR, "lobster_sample_orderbook.csv")


def test_parse_message_file():
    messages = parse_message_file(MESSAGE_FIXTURE)
    assert len(messages) == 8

    submission = messages[0]
    assert submission.event_type == 1
    assert submission.order_id == 100
    assert submission.size == 10
    assert submission.price == 1000000
    assert submission.direction == 1

    partial_cancel = messages[2]
    assert partial_cancel.event_type == 2
    assert partial_cancel.order_id == 200
    assert partial_cancel.size == 5


def test_parse_orderbook_file():
    snapshots = parse_orderbook_file(ORDERBOOK_FIXTURE, num_levels=2)
    assert len(snapshots) == 2

    fully_populated = snapshots[0]
    assert len(fully_populated.levels) == 2
    assert fully_populated.levels[0].ask_price == 912600
    assert fully_populated.levels[0].bid_price == 911400
    assert not fully_populated.levels[0].ask_empty
    assert not fully_populated.levels[0].bid_empty

    thin_book = snapshots[1]
    assert thin_book.levels[1].ask_empty
    assert thin_book.levels[1].bid_empty
    assert thin_book.levels[1].ask_size == 0
    assert thin_book.levels[1].bid_size == 0


def test_to_replay_events_full_fixture_flow():
    messages = parse_message_file(MESSAGE_FIXTURE)

    events = to_replay_events(messages)

    assert events == [
        ("add", 100, "Buy", 1000000, 10),
        ("add", 200, "Sell", 1001000, 20),
        ("reduce", 200, 15),
        ("cancel", 100),
        ("add", 300, "Sell", 1001000, 25),
        ("add", AGGRESSOR_ID_BASE, "Buy", 1001000, 25),
    ], "type-4 group collapses to one synthesized aggressor; type-5 is skipped entirely"


def test_to_replay_events_row_trace_aligns_rows_to_snapshots():
    messages = parse_message_file(MESSAGE_FIXTURE)

    row_trace = []
    events = to_replay_events(messages, row_trace=row_trace)

    assert len(row_trace) == len(messages)
    # Rows 0-4 (submissions/reduce/deletion) are all 1:1 with an event.
    assert row_trace[:5] == [0, 1, 2, 3, 4]
    # Rows 5-6 are the grouped execution: only the group's last row (6)
    # lines up with the single post-group snapshot (event index 5).
    assert row_trace[5] is None
    assert row_trace[6] == 5
    assert events[row_trace[6]][0] == "add"
    # Row 7 (hidden execution) has no snapshot at all.
    assert row_trace[7] is None


def test_to_replay_events_groups_only_consecutive_same_timestamp_same_direction():
    messages = [
        LobsterMessage(time_seconds=1.0, event_type=1, order_id=1, size=10, price=100, direction=-1),
        LobsterMessage(time_seconds=2.0, event_type=1, order_id=2, size=10, price=100, direction=-1),
        # Two separate aggressor sweeps: different timestamps, must not merge.
        LobsterMessage(time_seconds=3.0, event_type=4, order_id=1, size=4, price=100, direction=-1),
        LobsterMessage(time_seconds=4.0, event_type=4, order_id=2, size=3, price=100, direction=-1),
    ]

    events = to_replay_events(messages)

    add_events = [e for e in events if e[0] == "add" and e[1] >= AGGRESSOR_ID_BASE]
    assert len(add_events) == 2, "different timestamps must produce two separate aggressors, not one group of 7"
    assert add_events[0][4] == 4
    assert add_events[1][4] == 3


def test_to_replay_events_raises_on_events_with_no_replay_equivalent():
    cross_trade = LobsterMessage(
        time_seconds=1.0, event_type=6, order_id=1, size=10, price=100, direction=1
    )
    trading_halt = LobsterMessage(
        time_seconds=1.0, event_type=7, order_id=0, size=0, price=0, direction=1
    )
    for unsupported in (cross_trade, trading_halt):
        try:
            to_replay_events([unsupported])
            raise AssertionError(f"expected UnsupportedLobsterEvent for event_type={unsupported.event_type}")
        except UnsupportedLobsterEvent:
            pass


def test_parse_orderbook_line_raises_on_wrong_field_count():
    try:
        parse_orderbook_line("100,10,90,5", num_levels=2)  # 4 fields, needs 8
        raise AssertionError("expected ValueError for a field-count mismatch")
    except ValueError:
        pass


def test_to_replay_events_raises_when_partial_cancellation_would_underflow():
    # A type-2 row that removes >= the order's entire resting size should
    # have been a Deletion (type 3) instead -- to_replay_events treats
    # that as malformed input, not a silent full-cancel reinterpretation.
    messages = [
        LobsterMessage(time_seconds=1.0, event_type=1, order_id=1, size=5, price=100, direction=1),
        LobsterMessage(time_seconds=2.0, event_type=2, order_id=1, size=5, price=100, direction=1),
    ]
    try:
        to_replay_events(messages)
        raise AssertionError("expected ValueError for a size-underflowing partial cancellation")
    except ValueError:
        pass


def test_to_replay_events_raises_on_execution_overfill():
    messages = [
        LobsterMessage(time_seconds=1.0, event_type=1, order_id=1, size=5, price=100, direction=1),
        LobsterMessage(time_seconds=2.0, event_type=4, order_id=1, size=10, price=100, direction=1),
    ]
    try:
        to_replay_events(messages)
        raise AssertionError("expected ValueError for an execution exceeding the order's tracked size")
    except ValueError:
        pass


def test_to_replay_events_raises_on_unknown_event_type():
    messages = [
        LobsterMessage(time_seconds=1.0, event_type=99, order_id=1, size=5, price=100, direction=1),
    ]
    try:
        to_replay_events(messages)
        raise AssertionError("expected ValueError for an unrecognized LOBSTER event_type")
    except ValueError:
        pass


def main():
    test_parse_message_file()
    test_parse_orderbook_file()
    test_to_replay_events_full_fixture_flow()
    test_to_replay_events_row_trace_aligns_rows_to_snapshots()
    test_to_replay_events_groups_only_consecutive_same_timestamp_same_direction()
    test_to_replay_events_raises_on_events_with_no_replay_equivalent()
    test_parse_orderbook_line_raises_on_wrong_field_count()
    test_to_replay_events_raises_when_partial_cancellation_would_underflow()
    test_to_replay_events_raises_on_execution_overfill()
    test_to_replay_events_raises_on_unknown_event_type()
    print("OK")


if __name__ == "__main__":
    main()
    sys.exit(0)
