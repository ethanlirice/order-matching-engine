"""Tests for analysis/lobster_loader.py. Uses a small hand-crafted fixture
(tests/python/fixtures/lobster_sample_*.csv) built to match LOBSTER's
documented public format -- NOT real LOBSTER data (that requires
registering at lobsterdata.com; see PROJECT_SPEC.md's data-sources note
and README's tracked follow-up). Deliberately no pytest dependency, same
convention as test_smoke.py: run directly with the module's directory on
PYTHONPATH.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "analysis"))

from lobster_loader import (  # noqa: E402  (path must be set up first)
    UnsupportedLobsterEvent,
    parse_message_file,
    parse_orderbook_file,
    to_replay_events,
)

FIXTURES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
MESSAGE_FIXTURE = os.path.join(FIXTURES_DIR, "lobster_sample_message.csv")
ORDERBOOK_FIXTURE = os.path.join(FIXTURES_DIR, "lobster_sample_orderbook.csv")


def test_parse_message_file():
    messages = parse_message_file(MESSAGE_FIXTURE)
    assert len(messages) == 5

    submission = messages[0]
    assert submission.event_type == 1
    assert submission.order_id == 10000001
    assert submission.size == 100
    assert submission.price == 911400
    assert submission.direction == 1

    sell_submission = messages[1]
    assert sell_submission.direction == -1

    deletion = messages[2]
    assert deletion.event_type == 3
    assert deletion.order_id == 10000001


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


def test_to_replay_events_handles_submission_and_deletion():
    messages = parse_message_file(MESSAGE_FIXTURE)
    supported = [m for m in messages if m.event_type in (1, 3)]

    events = to_replay_events(supported)

    assert events == [
        ("add", 10000001, "Buy", 911400, 100),
        ("add", 10000002, "Sell", 912600, 50),
        ("cancel", 10000001),
    ]


def test_to_replay_events_raises_on_unsupported_event_types():
    messages = parse_message_file(MESSAGE_FIXTURE)
    # index 3 is a type-2 partial cancellation, index 4 is a type-4 execution.
    for unsupported in (messages[3], messages[4]):
        try:
            to_replay_events([unsupported])
            raise AssertionError(f"expected UnsupportedLobsterEvent for event_type={unsupported.event_type}")
        except UnsupportedLobsterEvent:
            pass


def main():
    test_parse_message_file()
    test_parse_orderbook_file()
    test_to_replay_events_handles_submission_and_deletion()
    test_to_replay_events_raises_on_unsupported_event_types()
    print("OK")


if __name__ == "__main__":
    main()
    sys.exit(0)
