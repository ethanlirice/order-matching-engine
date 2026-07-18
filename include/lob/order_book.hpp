#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "lob/level.hpp"
#include "lob/order.hpp"
#include "lob/order_pool.hpp"

namespace lob {

// Result of add_order/modify_order: every unit of submitted quantity ends
// up in exactly one of filled/resting/cancelled (quantity conservation,
// PROJECT_SPEC.md §5.5).
struct AddOrderResult {
  std::vector<TradeEvent> trades;
  Quantity filled_quantity = 0;
  Quantity resting_quantity = 0;    // still in the book after this call
  Quantity cancelled_quantity = 0;  // dropped without resting
};

// Single-symbol limit order book: two sides (bids, asks), each a price ->
// Level map ordered so the best bid is the highest price and the best ask
// is the lowest. Supports Limit, Market, IOC, FOK, and PostOnly orders
// (PROJECT_SPEC.md §2/§14 M1 and M2).
class OrderBook {
 public:
  // `pool_capacity` sizes the initial Order-object memory pool (M3 §6);
  // the pool grows automatically (an infrequent event, not on the hot
  // path) if a run exceeds it, so this is a performance tuning knob, not a
  // correctness-affecting hard cap.
  explicit OrderBook(std::size_t pool_capacity = 65536) : pool_(pool_capacity) {}

  // Aggressive (crossing) orders walk the opposing book in price-time
  // priority; any remainder rests (Limit) or is dropped (Market/IOC).
  // FOK is all-or-nothing (pre-checked, never partially filled) and
  // PostOnly is rejected outright if it would cross. Also rejects (as
  // fully cancelled) duplicate order ids.
  AddOrderResult add_order(Order incoming);

  // Cancels a resting order by id. Returns its remaining quantity, or
  // nullopt if the id is unknown.
  std::optional<Quantity> cancel_order(OrderId id);

  // cancel + re-insert at the tail of the (possibly new) price level --
  // loses time priority. new_quantity == 0 is treated as a full cancel.
  // Returns nullopt if the id is unknown.
  std::optional<AddOrderResult> modify_order(OrderId id, Price new_price, Quantity new_quantity);

  // In-place quantity reduction that preserves the order's FIFO position
  // (unlike modify_order, which always re-inserts at the tail) -- the
  // correct primitive for a venue's "partial cancellation" semantics
  // (e.g. LOBSTER message type 2), where a resting order keeps its queue
  // priority when it shrinks. Requires 0 < new_quantity < current
  // quantity: new_quantity == 0 is a full cancel (use cancel_order) and
  // new_quantity >= current is not a reduction at all -- both are
  // rejected (nullopt) rather than silently reinterpreted, since callers
  // that meant something else should find out, not have this API guess.
  // Returns the new resting quantity, or nullopt if the id is unknown or
  // new_quantity is out of the valid reduction range.
  std::optional<Quantity> ReduceQuantity(OrderId id, Quantity new_quantity);

  bool best_bid(Price& out_price) const;
  bool best_ask(Price& out_price) const;

  // Total resting quantity at a price level on the given side, or 0 if
  // there's no such level. A real production accessor (not test-only) --
  // aggregate price-level depth, like best_bid/best_ask, not order-level
  // detail.
  Quantity quantity_at(Side side, Price price) const;

  // -- Read-only testability accessors (unit/fuzz/determinism tests only;
  // no strategy/simulator code should depend on these). --
  bool contains(OrderId id) const;
  const Order* debug_peek(OrderId id) const;
  std::vector<OrderId> resting_order_ids(Side side, Price price) const;
  std::vector<Price> bid_prices() const;
  std::vector<Price> ask_prices() const;
  std::size_t order_count() const;
  std::size_t pool_chunk_count() const;

 private:
  template <typename OppositeMap>
  void match_against(OppositeMap& opposite, Order& incoming, std::vector<TradeEvent>& trades,
                     Quantity& filled_quantity);

  // Read-only: true iff the resting liquidity crossing incoming's price on
  // the opposite side sums to at least `needed`. Used by FOK's all-or-
  // nothing pre-check; walks the same map, in the same order, using the
  // same crossing rule as match_against, so it can never disagree with the
  // real match that follows it.
  template <typename OppositeMap>
  bool CanFullyFill(const OppositeMap& opposite, Side side, Price price, Quantity needed) const;

  template <typename LevelMap>
  void finalize_removal(LevelMap& side_map, typename LevelMap::iterator level_it, Order* order);

  // Order memory is owned by pool_ (M3 §6); order_index_ holds only
  // non-owning raw Order*. Safe under rehashing: a rehash only moves the
  // unordered_map's own bucket/node bookkeeping, never a pool-owned Order,
  // so raw pointers held elsewhere (here and in Level) stay valid.
  //
  // Note on scope: this pools Order *objects* only. order_index_ itself is
  // a standard node-based unordered_map, which still heap-allocates a node
  // per emplace() regardless of reserve() -- a smaller, separately
  // documented residual cost this milestone doesn't eliminate (see
  // README's Benchmarking section).
  OrderPool pool_;
  std::unordered_map<OrderId, Order*> order_index_;
  std::map<Price, Level, std::greater<>> bids_;
  std::map<Price, Level> asks_;
};

}  // namespace lob
