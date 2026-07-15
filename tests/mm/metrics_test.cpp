#include "lob/mm/metrics.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "lob/mm/market_maker.hpp"
#include "lob/sim/market_data_log.hpp"

namespace lob::mm {
namespace {

// A fixed quote, just enough to bootstrap resting bid/ask order ids via
// the real OnBookUpdate/OnOrderAck path -- fills are then injected
// directly via OnTrade with hand-picked prices/quantities/timestamps, so
// the whole scenario is exactly hand-computable rather than dependent on
// Simulator's reconciliation settling dynamics (see MarketMaker's class
// comment).
class FixedQuoteMaker : public MarketMaker {
 protected:
  Quote ComputeQuotes(const sim::BookSnapshot&, Timestamp) override {
    Quote quote;
    quote.has_bid = true;
    quote.bid_price = 100;
    quote.bid_quantity = 10;
    quote.has_ask = true;
    quote.ask_price = 102;
    quote.ask_quantity = 10;
    return quote;
  }
};

class RecordingSink : public sim::OrderIntentSink {
 public:
  OrderId Submit(NewOrderCommand command) override {
    last_side = command.side;
    last_id = next_id_++;
    return last_id;
  }
  void Cancel(OrderId id) override { last_id = id; }
  void Modify(OrderId id, Price, Quantity) override { last_id = id; }

  Side last_side = Side::Buy;
  OrderId last_id = 0;

 private:
  OrderId next_id_ = 1;
};

TradeEvent MakeTrade(Timestamp t, Side side, Price price, Quantity size, OrderId maker_order_id) {
  TradeEvent trade;
  trade.timestamp = t;
  trade.aggressor_side = (side == Side::Buy) ? Side::Sell : Side::Buy;
  trade.price = price;
  trade.size = size;
  trade.maker_order_id = maker_order_id;
  trade.taker_order_id = 999;
  return trade;
}

TEST(MetricsTest, MatchesHandComputedValuesForAThreeFillScenario) {
  FixedQuoteMaker maker;
  RecordingSink sink;

  sim::BookSnapshot snapshot;
  snapshot.has_bid = true;
  snapshot.best_bid = 100;
  snapshot.has_ask = true;
  snapshot.best_ask = 102;

  // Bootstrap: establish real resting bid/ask order ids (one Submit per
  // OnBookUpdate call, per MarketMaker's global single-action gate).
  maker.OnBookUpdate(snapshot, /*now=*/0, /*event_ordinal=*/0, sink);
  ASSERT_EQ(sink.last_side, Side::Buy);
  OrderId bid_id = sink.last_id;
  AddOrderResult bid_ack;
  bid_ack.resting_quantity = 10;
  maker.OnOrderAck(bid_id, bid_ack);

  maker.OnBookUpdate(snapshot, /*now=*/0, /*event_ordinal=*/0, sink);
  ASSERT_EQ(sink.last_side, Side::Sell);
  OrderId ask_id = sink.last_id;
  AddOrderResult ask_ack;
  ask_ack.resting_quantity = 10;
  maker.OnOrderAck(ask_id, ask_ack);

  // Hand-built mid-price series (see the derivation in the session
  // summary; reproduced in comments below each fill).
  sim::MarketDataLog log;
  log.Record(0, 0, true, 100, true, 102);   // mid=101
  log.Record(10, 1, true, 99, true, 103);   // mid=101 (fill 1's own impact)
  log.Record(20, 2, true, 100, true, 104);  // mid=102
  log.Record(30, 3, true, 101, true, 105);  // mid=103 (fill 2's own impact)
  log.Record(40, 4, true, 101, true, 105);  // mid=103
  log.Record(50, 5, true, 102, true, 106);  // mid=104 (fill 3's own impact)
  log.Record(60, 6, true, 102, true, 106);  // mid=104

  // Fill 1: buy 5 @ 101. pre_mid (strictly before (10,1)) = sample@(0,0) =
  // 101. post_mid (markout horizon 10 -> at-or-before (20, max)) =
  // sample@(20,2) = 102.
  maker.OnTrade(MakeTrade(10, Side::Buy, 101, 5, bid_id), 10, 1, sink);
  // Fill 2: sell 3 @ 103. pre_mid (strictly before (30,3)) = sample@(20,2)
  // = 102. post_mid (at-or-before (40, max)) = sample@(40,4) = 103.
  maker.OnTrade(MakeTrade(30, Side::Sell, 103, 3, ask_id), 30, 3, sink);
  // Fill 3: sell 4 @ 104. pre_mid (strictly before (50,5)) = sample@(40,4)
  // = 103. post_mid (at-or-before (60, max)) = sample@(60,6) = 104.
  maker.OnTrade(MakeTrade(50, Side::Sell, 104, 4, ask_id), 50, 5, sink);

  ASSERT_EQ(maker.fills().size(), 3u);
  ASSERT_EQ(maker.inventory(), -2);  // +5 -3 -4
  ASSERT_DOUBLE_EQ(maker.cash(), -5.0 * 101.0 + 3.0 * 103.0 + 4.0 * 104.0);

  MetricsConfig config;
  config.markout_horizon = 10;
  config.session_end = 60;
  config.sharpe_bucket_duration = 20;

  MetricsSummary summary = ComputeMetrics(maker, log, config);

  ASSERT_EQ(summary.fill_metrics.size(), 3u);

  // Fill 1: side_sign=+1, price=101, pre_mid=101, post_mid=102.
  const FillMetrics& f1 = summary.fill_metrics[0];
  ASSERT_TRUE(f1.pre_mid.has_value());
  ASSERT_TRUE(f1.post_mid.has_value());
  EXPECT_DOUBLE_EQ(*f1.pre_mid, 101.0);
  EXPECT_DOUBLE_EQ(*f1.post_mid, 102.0);
  EXPECT_DOUBLE_EQ(*f1.effective_spread, 0.0);              // 2*1*(101-101)
  EXPECT_DOUBLE_EQ(*f1.markout, 1.0);                       // 1*(102-101)
  EXPECT_DOUBLE_EQ(*f1.pure_adverse_selection_cost, -1.0);  // 1*(101-102)

  // Fill 2: side_sign=-1, price=103, pre_mid=102, post_mid=103.
  const FillMetrics& f2 = summary.fill_metrics[1];
  EXPECT_DOUBLE_EQ(*f2.pre_mid, 102.0);
  EXPECT_DOUBLE_EQ(*f2.post_mid, 103.0);
  EXPECT_DOUBLE_EQ(*f2.effective_spread, 2.0);             // 2*(-1)*(102-103)
  EXPECT_DOUBLE_EQ(*f2.markout, 0.0);                      // -1*(103-103)
  EXPECT_DOUBLE_EQ(*f2.pure_adverse_selection_cost, 1.0);  // -1*(102-103)

  // Fill 3: side_sign=-1, price=104, pre_mid=103, post_mid=104.
  const FillMetrics& f3 = summary.fill_metrics[2];
  EXPECT_DOUBLE_EQ(*f3.pre_mid, 103.0);
  EXPECT_DOUBLE_EQ(*f3.post_mid, 104.0);
  EXPECT_DOUBLE_EQ(*f3.effective_spread, 2.0);  // 2*(-1)*(103-104)
  EXPECT_DOUBLE_EQ(*f3.markout, 0.0);
  EXPECT_DOUBLE_EQ(*f3.pure_adverse_selection_cost, 1.0);

  double final_mid = 104.0;
  double expected_total_pnl = maker.cash() + static_cast<double>(maker.inventory()) * final_mid;
  double expected_spread_pnl = (0.0 / 2.0) * 5.0 + (2.0 / 2.0) * 3.0 + (2.0 / 2.0) * 4.0;
  EXPECT_DOUBLE_EQ(summary.pnl.total_pnl, expected_total_pnl);
  EXPECT_DOUBLE_EQ(summary.pnl.spread_pnl, expected_spread_pnl);
  EXPECT_DOUBLE_EQ(summary.pnl.inventory_pnl, expected_total_pnl - expected_spread_pnl);

  // Fill rate: 3 fills / 60 virtual-time units.
  EXPECT_DOUBLE_EQ(summary.fill_rate, 3.0 / 60.0);

  // Inventory extremes: running inventory_after sequence is 0 (initial),
  // 5, 2, -2.
  EXPECT_EQ(summary.max_inventory, 5);
  EXPECT_EQ(summary.min_inventory, -2);
  EXPECT_EQ(summary.max_abs_inventory, 5);

  // Sharpe: mark-to-market at t=0,20,40,60 is 0, 5, 10, 12 (hand-derived
  // in the session summary) -> deltas [5,5,2], mean=4, sample_std=sqrt(3).
  EXPECT_NEAR(summary.sharpe, 4.0 / std::sqrt(3.0), 1e-9);
}

TEST(MetricsTest, ReportsZeroSharpeAndZeroPnlWhenNothingEverFilled) {
  FixedQuoteMaker maker;
  sim::MarketDataLog log;
  log.Record(0, 0, true, 100, true, 102);

  MetricsConfig config;
  config.markout_horizon = 10;
  config.session_end = 60;
  config.sharpe_bucket_duration = 20;

  MetricsSummary summary = ComputeMetrics(maker, log, config);

  EXPECT_TRUE(summary.fill_metrics.empty());
  EXPECT_DOUBLE_EQ(summary.pnl.total_pnl, 0.0);
  EXPECT_DOUBLE_EQ(summary.pnl.spread_pnl, 0.0);
  EXPECT_DOUBLE_EQ(summary.pnl.inventory_pnl, 0.0);
  EXPECT_DOUBLE_EQ(summary.fill_rate, 0.0);
  EXPECT_EQ(summary.max_inventory, 0);
  EXPECT_EQ(summary.min_inventory, 0);
  EXPECT_EQ(summary.max_abs_inventory, 0);
  // Zero variance (all mark-to-market deltas are 0) -- must report 0, not
  // NaN.
  EXPECT_DOUBLE_EQ(summary.sharpe, 0.0);
}

}  // namespace
}  // namespace lob::mm
