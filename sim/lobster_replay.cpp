#include "lob/sim/lobster_replay.hpp"

#include "lob/matching_engine.hpp"

namespace lob::sim {

namespace {

// EMPTY_ASK_PRICE/EMPTY_BID_PRICE sentinels match analysis/
// lobster_loader.py's LOBSTER orderbook-file convention, so a level our
// book doesn't have populated compares as "empty" the same way a real
// unoccupied LOBSTER level does, without either side needing to special-
// case the other.
constexpr Price kEmptyAskPrice = 9999999999;
constexpr Price kEmptyBidPrice = -9999999999;

BookSnapshotRow SnapshotBook(const OrderBook& book, int num_levels) {
  std::vector<Price> bids = book.bid_prices();
  std::vector<Price> asks = book.ask_prices();

  BookSnapshotRow row;
  row.levels.reserve(static_cast<std::size_t>(num_levels));
  for (int i = 0; i < num_levels; ++i) {
    BookLevelSnapshot level;
    if (static_cast<std::size_t>(i) < asks.size()) {
      level.ask_price = asks[static_cast<std::size_t>(i)];
      level.ask_size = book.quantity_at(Side::Sell, level.ask_price);
      level.ask_empty = false;
    } else {
      level.ask_price = kEmptyAskPrice;
      level.ask_size = 0;
      level.ask_empty = true;
    }
    if (static_cast<std::size_t>(i) < bids.size()) {
      level.bid_price = bids[static_cast<std::size_t>(i)];
      level.bid_size = book.quantity_at(Side::Buy, level.bid_price);
      level.bid_empty = false;
    } else {
      level.bid_price = kEmptyBidPrice;
      level.bid_size = 0;
      level.bid_empty = true;
    }
    row.levels.push_back(level);
  }
  return row;
}

}  // namespace

std::vector<BookSnapshotRow> ReplayLobsterEvents(const std::vector<LobsterReplayEvent>& events,
                                                 int num_levels) {
  MatchingEngine engine;
  std::vector<BookSnapshotRow> snapshots;
  snapshots.reserve(events.size());

  for (const LobsterReplayEvent& event : events) {
    if (event.kind == LobsterReplayEvent::Kind::Add) {
      Order order;
      order.id = event.order_id;
      order.side = event.side;
      order.type = OrderType::Limit;
      order.price = event.price;
      order.quantity = event.quantity;
      engine.submit_order(order);
    } else if (event.kind == LobsterReplayEvent::Kind::Cancel) {
      engine.cancel_order(event.order_id);
    } else {
      engine.ReduceQuantity(event.order_id, event.quantity);
    }
    snapshots.push_back(SnapshotBook(engine.book(), num_levels));
  }

  return snapshots;
}

}  // namespace lob::sim
