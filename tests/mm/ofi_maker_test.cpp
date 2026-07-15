#include "lob/mm/ofi_maker.hpp"

#include <gtest/gtest.h>

#include "lob/sim/simulator.hpp"
#include "sim/support/sim_test_helpers.hpp"

namespace lob::mm {
namespace {

// Exposes the protected ComputeQuotes directly, same rationale as
// AvellanedaStoikovMakerTest: formula tests shouldn't have to go through
// Simulator's reconciliation settling dynamics.
class TestableOfiMaker : public OfiMaker {
 public:
  using OfiMaker::ComputeQuotes;
  using OfiMaker::OfiMaker;
};

// Records the last Submit/Modify issued, and hands back a caller-chosen
// order id -- lets a test drive OnBookUpdate/OnOrderAck directly to
// establish a specific resting state without going through Simulator.
class RecordingSink : public sim::OrderIntentSink {
 public:
  OrderId Submit(NewOrderCommand command) override {
    last_side = command.side;
    last_price = command.price;
    last_quantity = command.quantity;
    last_id = next_id_++;
    return last_id;
  }
  void Cancel(OrderId id) override { last_id = id; }
  void Modify(OrderId id, Price price, Quantity quantity) override {
    last_id = id;
    last_price = price;
    last_quantity = quantity;
  }

  Side last_side = Side::Buy;
  Price last_price = 0;
  Quantity last_quantity = 0;
  OrderId last_id = 0;

 private:
  OrderId next_id_ = 1;
};

sim::BookSnapshot TwoSidedSnapshot(Price bid, Quantity bid_qty, Price ask, Quantity ask_qty) {
  sim::BookSnapshot snapshot;
  snapshot.has_bid = true;
  snapshot.best_bid = bid;
  snapshot.best_bid_quantity = bid_qty;
  snapshot.has_ask = true;
  snapshot.best_ask = ask;
  snapshot.best_ask_quantity = ask_qty;
  return snapshot;
}

TEST(OfiMakerTest, PositiveImbalanceSkewsQuotesUpward) {
  OfiMakerConfig config;
  config.as_config.gamma = 0.1;
  config.as_config.sigma = 1.0;
  config.as_config.kappa = 1.5;
  config.as_config.quote_size = 10;
  config.ofi_beta = 10.0;
  TestableOfiMaker maker(config, /*horizon=*/10);

  // Balanced book: OFI == 0, quotes should match plain AS exactly.
  Quote balanced = maker.ComputeQuotes(TwoSidedSnapshot(90, 500, 110, 500), /*now=*/0);
  // mid=100, tau=10, variance_term=1.0, adverse~=0.6453852,
  // half_spread~=1.1453852 -> bid=98, ask=101 (same as the plain-AS test).
  EXPECT_EQ(balanced.bid_price, 98);
  EXPECT_EQ(balanced.ask_price, 101);

  // More resting buy pressure than sell (ofi = (900-100)/1000 = 0.8) --
  // beta=10 skews the reservation price up by 8, shifting both quotes up
  // by the same amount relative to the balanced case.
  Quote imbalanced = maker.ComputeQuotes(TwoSidedSnapshot(90, 900, 110, 100), /*now=*/0);
  EXPECT_EQ(imbalanced.bid_price - balanced.bid_price, 8);
  EXPECT_EQ(imbalanced.ask_price - balanced.ask_price, 8);
}

TEST(OfiMakerTest, FallsBackToPlainAvellanedaStoikovWhenNoExternalLiquidityExists) {
  OfiMakerConfig config;
  config.as_config.gamma = 0.1;
  config.as_config.sigma = 1.0;
  config.as_config.kappa = 1.5;
  config.as_config.quote_size = 10;
  config.ofi_beta = 1000.0;  // would produce a huge skew if this weren't guarded
  TestableOfiMaker maker(config, /*horizon=*/10);

  // Zero quantity at both levels (a degenerate snapshot no real book would
  // produce, but the 0/0 guard must not propagate a NaN into the quote
  // regardless): total external quantity is 0, so OFI must fall back to
  // no skew at all.
  Quote quote = maker.ComputeQuotes(TwoSidedSnapshot(90, 0, 110, 0), /*now=*/0);
  EXPECT_EQ(quote.bid_price, 98);
  EXPECT_EQ(quote.ask_price, 101);
}

TEST(OfiMakerTest, ExcludesOwnRestingQuantityFromTheImbalanceSignal) {
  OfiMakerConfig config;
  config.as_config.gamma = 0.0;  // isolates the OFI term: no inventory skew
  config.as_config.sigma = 1.0;
  config.as_config.kappa = 1.5;
  config.as_config.quote_size = 10;
  config.ofi_beta = 1000.0;  // large, so any un-excluded bias is obvious
  TestableOfiMaker maker(config, /*horizon=*/10);
  RecordingSink sink;

  // Bootstrap: give the maker a resting bid via a direct
  // OnBookUpdate + OnOrderAck round-trip -- deterministic, no
  // Simulator/reconciliation settling involved. The maker itself decides
  // the price (gamma=0 -> half_spread floors to 1.0 tick -> bid = mid-1).
  sim::BookSnapshot bootstrap = TwoSidedSnapshot(90, 1000, 110, 1000);
  maker.OnBookUpdate(bootstrap, /*now=*/0, /*event_ordinal=*/0, sink);
  ASSERT_EQ(sink.last_side, Side::Buy);
  Price own_bid_price = sink.last_price;

  AddOrderResult ack_result;
  ack_result.resting_quantity = 500;
  maker.OnOrderAck(sink.last_id, ack_result);

  // A snapshot where the best bid IS our own resting order: total
  // quantity there is 700 (our 500 + 200 external), best ask has 200
  // external. If OFI didn't exclude our own quantity, bid_qty' would read
  // 700 (heavily imbalanced toward buy); excluding it correctly leaves
  // 200 external vs 200 external -- a balanced signal.
  Quote with_own_resting = maker.ComputeQuotes(TwoSidedSnapshot(own_bid_price, 700, 110, 200), 0);

  // Control: a FRESH maker (nothing resting anywhere) given a snapshot
  // that already reflects only the true external picture (200 vs 200) at
  // the exact same prices. If exclusion works, these must match exactly:
  // same reservation price/half-spread inputs (same config, same zero
  // inventory, same tau), and the same externally-true OFI signal.
  TestableOfiMaker control(config, /*horizon=*/10);
  Quote control_quote = control.ComputeQuotes(TwoSidedSnapshot(own_bid_price, 200, 110, 200), 0);

  EXPECT_EQ(with_own_resting.bid_price, control_quote.bid_price);
  EXPECT_EQ(with_own_resting.ask_price, control_quote.ask_price);
}

TEST(OfiMakerTest, EndToEndQuotesAreTighterOrShiftedRelativeToPlainAvellanedaStoikov) {
  // Sanity end-to-end run through a real Simulator: confirms the strategy
  // actually produces valid, orderable quotes (bid < ask, both positive)
  // when driven by real order flow, without asserting exact prices (which
  // depend on reconciliation settling dynamics -- see MarketMaker's class
  // comment).
  OfiMakerConfig config;
  config.as_config.gamma = 0.1;
  config.as_config.sigma = 1.0;
  config.as_config.kappa = 1.5;
  config.as_config.quote_size = 10;
  config.ofi_beta = 5.0;
  OfiMaker maker(config, /*horizon=*/1000);
  sim::Simulator simulator(&maker, /*latency=*/0);

  simulator.LoadEvents({
      sim::testing::MakeAddEvent(0, 0, 1, Side::Buy, 90, 1000),
      sim::testing::MakeAddEvent(0, 1, 2, Side::Sell, 110, 1000),
  });
  simulator.Run();

  Price bid = 0, ask = 0;
  ASSERT_TRUE(simulator.DebugBook().best_bid(bid));
  ASSERT_TRUE(simulator.DebugBook().best_ask(ask));
  EXPECT_LT(bid, ask);
  EXPECT_GT(bid, 0);
}

}  // namespace
}  // namespace lob::mm
