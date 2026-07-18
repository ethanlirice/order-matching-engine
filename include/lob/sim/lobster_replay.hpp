#pragma once

#include <vector>

#include "lob/order.hpp"

namespace lob::sim {

// One normalized LOBSTER-derived event, produced Python-side by
// analysis/lobster_loader.py::to_replay_events and passed across the
// pybind11 boundary for replay. Mirrors ReplayMessage's Add/Cancel/Reduce
// kinds as a flat, pybind11-friendly DTO -- deliberately not
// ReplayMessage itself (that's a std::variant-backed Event payload built
// for Simulator's virtual-clock event queue, which this bypasses: pure
// historical order-flow replay has no strategy to interpose and no
// latency to model, so a bare MatchingEngine driven in file order is the
// right tool, not the full Simulator).
struct LobsterReplayEvent {
  enum class Kind { Add, Cancel, Reduce };

  Kind kind = Kind::Add;
  OrderId order_id = 0;
  Side side = Side::Buy;    // valid iff kind == Add
  Price price = 0;          // valid iff kind == Add
  Quantity quantity = 0;    // valid iff kind == Add (size) or Reduce (new_quantity)
};

struct BookLevelSnapshot {
  Price ask_price = 0;
  Quantity ask_size = 0;
  Price bid_price = 0;
  Quantity bid_size = 0;
  bool ask_empty = true;
  bool bid_empty = true;
};

struct BookSnapshotRow {
  std::vector<BookLevelSnapshot> levels;  // best-to-worst, size == num_levels
};

// Replays `events` in file order through a fresh MatchingEngine and
// returns one BookSnapshotRow per input event, reflecting the top
// `num_levels` of the book immediately after that event applied --
// directly comparable, index-for-index, to the LOBSTER orderbook CSV rows
// analysis/lobster_loader.py kept alongside each emitted event.
std::vector<BookSnapshotRow> ReplayLobsterEvents(const std::vector<LobsterReplayEvent>& events,
                                                  int num_levels);

}  // namespace lob::sim
