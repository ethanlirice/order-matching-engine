#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "lob/order_book.hpp"
#include "support/test_helpers.hpp"

// Portable (non-libFuzzer) seeded random op-sequence generator used only by
// the determinism test -- PROJECT_SPEC.md §9 "identical input -> identical
// output across runs". The libFuzzer harness in tests/fuzz/ has its own,
// separate byte-driven generator for coverage-guided exploration.

namespace lob {
namespace {

using testing::MakeOrder;

enum class OpKind { Add, Cancel, Modify };

struct GeneratedOp {
  OpKind kind = OpKind::Add;
  Order order;                // used for Add
  OrderId target_id = 0;      // used for Cancel/Modify
  Price new_price = 0;        // used for Modify
  Quantity new_quantity = 0;  // used for Modify
};

std::vector<GeneratedOp> GenerateOps(std::uint64_t seed, std::size_t count) {
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> choice_dist(0, 9);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> type_dist(0, 4);
  std::uniform_int_distribution<Price> price_dist(95, 105);
  std::uniform_int_distribution<Quantity> qty_dist(1, 20);

  std::vector<GeneratedOp> ops;
  ops.reserve(count);
  std::vector<OrderId> live_ids;
  OrderId next_id = 1;

  for (std::size_t i = 0; i < count; ++i) {
    int choice = choice_dist(rng);
    if (choice <= 6 || live_ids.empty()) {
      GeneratedOp op;
      op.kind = OpKind::Add;
      Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
      OrderType type = (type_dist(rng) == 0) ? OrderType::Market : OrderType::Limit;
      op.order = MakeOrder(next_id, side, type, price_dist(rng), qty_dist(rng));
      live_ids.push_back(next_id);
      ++next_id;
      ops.push_back(op);
    } else if (choice <= 8) {
      GeneratedOp op;
      op.kind = OpKind::Cancel;
      op.target_id = live_ids[static_cast<std::size_t>(rng()) % live_ids.size()];
      ops.push_back(op);
    } else {
      GeneratedOp op;
      op.kind = OpKind::Modify;
      op.target_id = live_ids[static_cast<std::size_t>(rng()) % live_ids.size()];
      op.new_price = price_dist(rng);
      op.new_quantity = qty_dist(rng);
      ops.push_back(op);
    }
  }
  return ops;
}

// Uniform per-op outcome so Add/Cancel/Modify results can be compared
// across two independent runs with one equality check.
struct OpOutcome {
  bool valid = true;  // false iff Cancel/Modify targeted an unknown id
  std::vector<TradeEvent> trades;
  Quantity filled = 0;
  Quantity resting = 0;
  Quantity cancelled = 0;
};

bool TradeEventEqual(const TradeEvent& a, const TradeEvent& b) {
  return a.price == b.price && a.size == b.size && a.aggressor_side == b.aggressor_side &&
         a.maker_order_id == b.maker_order_id && a.taker_order_id == b.taker_order_id &&
         a.timestamp == b.timestamp;
}

bool OpOutcomeEqual(const OpOutcome& a, const OpOutcome& b) {
  if (a.valid != b.valid || a.filled != b.filled || a.resting != b.resting ||
      a.cancelled != b.cancelled || a.trades.size() != b.trades.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.trades.size(); ++i) {
    if (!TradeEventEqual(a.trades[i], b.trades[i])) {
      return false;
    }
  }
  return true;
}

OpOutcome Apply(OrderBook& book, const GeneratedOp& op) {
  switch (op.kind) {
    case OpKind::Add: {
      AddOrderResult result = book.add_order(op.order);
      return OpOutcome{true, result.trades, result.filled_quantity, result.resting_quantity,
                       result.cancelled_quantity};
    }
    case OpKind::Cancel: {
      std::optional<Quantity> cancelled = book.cancel_order(op.target_id);
      if (!cancelled.has_value()) {
        return OpOutcome{false, {}, 0, 0, 0};
      }
      return OpOutcome{true, {}, 0, 0, *cancelled};
    }
    case OpKind::Modify: {
      std::optional<AddOrderResult> result =
          book.modify_order(op.target_id, op.new_price, op.new_quantity);
      if (!result.has_value()) {
        return OpOutcome{false, {}, 0, 0, 0};
      }
      return OpOutcome{true, result->trades, result->filled_quantity, result->resting_quantity,
                       result->cancelled_quantity};
    }
  }
  return OpOutcome{};
}

struct BookSnapshot {
  std::vector<Price> bid_prices;
  std::vector<Price> ask_prices;
  std::vector<std::vector<OrderId>> bid_orders_per_level;
  std::vector<std::vector<OrderId>> ask_orders_per_level;
};

BookSnapshot Snapshot(const OrderBook& book) {
  BookSnapshot snapshot;
  snapshot.bid_prices = book.bid_prices();
  snapshot.ask_prices = book.ask_prices();
  for (Price price : snapshot.bid_prices) {
    snapshot.bid_orders_per_level.push_back(book.resting_order_ids(Side::Buy, price));
  }
  for (Price price : snapshot.ask_prices) {
    snapshot.ask_orders_per_level.push_back(book.resting_order_ids(Side::Sell, price));
  }
  return snapshot;
}

TEST(DeterminismTest, IdenticalSeededSequenceProducesIdenticalOutputAndState) {
  std::vector<GeneratedOp> ops = GenerateOps(/*seed=*/0xC0FFEE, /*count=*/2000);

  OrderBook book_a;
  OrderBook book_b;
  std::vector<OpOutcome> outcomes_a;
  std::vector<OpOutcome> outcomes_b;
  outcomes_a.reserve(ops.size());
  outcomes_b.reserve(ops.size());

  for (const GeneratedOp& op : ops) {
    outcomes_a.push_back(Apply(book_a, op));
  }
  for (const GeneratedOp& op : ops) {
    outcomes_b.push_back(Apply(book_b, op));
  }

  ASSERT_EQ(outcomes_a.size(), outcomes_b.size());
  for (std::size_t i = 0; i < outcomes_a.size(); ++i) {
    EXPECT_TRUE(OpOutcomeEqual(outcomes_a[i], outcomes_b[i]))
        << "divergence at op index " << i << " (a divergent intermediate result is a "
        << "determinism bug even if terminal book state still matches)";
  }

  BookSnapshot snapshot_a = Snapshot(book_a);
  BookSnapshot snapshot_b = Snapshot(book_b);
  EXPECT_EQ(snapshot_a.bid_prices, snapshot_b.bid_prices);
  EXPECT_EQ(snapshot_a.ask_prices, snapshot_b.ask_prices);
  EXPECT_EQ(snapshot_a.bid_orders_per_level, snapshot_b.bid_orders_per_level);
  EXPECT_EQ(snapshot_a.ask_orders_per_level, snapshot_b.ask_orders_per_level);

  for (Price price : snapshot_a.bid_prices) {
    for (OrderId id : book_a.resting_order_ids(Side::Buy, price)) {
      ASSERT_TRUE(book_b.contains(id));
      EXPECT_EQ(book_a.debug_peek(id)->quantity, book_b.debug_peek(id)->quantity);
    }
  }
}

}  // namespace
}  // namespace lob
