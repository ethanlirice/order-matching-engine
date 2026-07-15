#include "lob/mm/market_maker.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "lob/sim/simulator.hpp"
#include "sim/support/sim_test_helpers.hpp"

namespace lob::mm {
namespace {

// Returns a scripted sequence of quotes, one per ComputeQuotes call
// (repeating the last one once exhausted) -- lets base-class reconciliation
// tests exercise exact scenarios without coupling to any concrete
// strategy's mid formula.
class ScriptedMaker : public MarketMaker {
 public:
  void QueueQuotes(std::vector<Quote> quotes) { quotes_ = std::move(quotes); }
  int compute_calls() const { return compute_calls_; }

 protected:
  Quote ComputeQuotes(const sim::BookSnapshot& /*snapshot*/, Timestamp /*now*/) override {
    ++compute_calls_;
    if (quotes_.empty()) {
      return Quote{};
    }
    if (next_ + 1 < quotes_.size()) {
      return quotes_[next_++];
    }
    return quotes_[next_];
  }

 private:
  std::vector<Quote> quotes_;
  std::size_t next_ = 0;
  int compute_calls_ = 0;
};

Quote TwoSidedQuote(Price bid_price, Quantity bid_quantity, Price ask_price,
                    Quantity ask_quantity) {
  Quote quote;
  quote.has_bid = true;
  quote.bid_price = bid_price;
  quote.bid_quantity = bid_quantity;
  quote.has_ask = true;
  quote.ask_price = ask_price;
  quote.ask_quantity = ask_quantity;
  return quote;
}

TEST(MarketMakerTest, SubmitsPostOnlyQuotesOnBothSidesAndTheyRest) {
  ScriptedMaker maker;
  sim::Simulator simulator(&maker, /*latency=*/0);
  maker.QueueQuotes({TwoSidedQuote(95, 10, 105, 10)});

  simulator.LoadEvents({sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 50, 1)});
  simulator.Run();

  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, 95).size(), 1u);
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, 105).size(), 1u);
  EXPECT_EQ(simulator.DebugBook().quantity_at(Side::Buy, 95), 10u);
  EXPECT_EQ(simulator.DebugBook().quantity_at(Side::Sell, 105), 10u);
}

TEST(MarketMakerTest, DoesNotIssueASecondActionWhileAnActionIsOutstanding) {
  ScriptedMaker maker;
  sim::Simulator simulator(&maker, /*latency=*/5);
  // A constant desired quote: this isolates the gate itself. At most one
  // Submit/Modify is ever in flight across BOTH sides at a time (see
  // MarketMaker's class comment) -- without that gate, a second
  // OnBookUpdate firing before the first submit's ack lands could issue a
  // duplicate/conflicting action before the base class knows whether the
  // first one rested, crossed, or was rejected.
  maker.QueueQuotes({TwoSidedQuote(90, 10, 110, 10)});

  // t=10: first OnBookUpdate submits bid@90 (only one side per round; ask
  // waits its turn), scheduled to land (and ack) at t=15 given latency=5.
  // t=12: second OnBookUpdate fires before that submit has landed --
  // action_pending_ is still true, so nothing new is issued at all.
  simulator.LoadEvents({
      sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 50, 1),
      sim::testing::MakeAddEvent(12, 1, 2, Side::Buy, 51, 1),
  });
  simulator.Run();

  EXPECT_GE(maker.compute_calls(), 2);
  // Exactly one order per side ever reaches the book.
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, 90).size(), 1u);
  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, 110).size(), 1u);
  EXPECT_EQ(simulator.DebugBook().order_count(), 4u);
}

TEST(MarketMakerTest, CancelsASideWhenTheStrategyStopsWantingAQuoteThere) {
  ScriptedMaker maker;
  sim::Simulator simulator(&maker, /*latency=*/0);
  Quote bid_only = TwoSidedQuote(95, 10, 105, 10);
  bid_only.has_ask = false;
  maker.QueueQuotes({TwoSidedQuote(95, 10, 105, 10), bid_only});

  simulator.LoadEvents({
      sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 50, 1),
      sim::testing::MakeAddEvent(20, 1, 2, Side::Buy, 51, 1),
  });
  simulator.Run();

  EXPECT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, 95).size(), 1u);
  EXPECT_TRUE(simulator.DebugBook().resting_order_ids(Side::Sell, 105).empty());
}

TEST(MarketMakerTest, AttributesFillsAndUpdatesInventoryAndCash) {
  ScriptedMaker maker;
  sim::Simulator simulator(&maker, /*latency=*/0);
  maker.QueueQuotes({TwoSidedQuote(95, 10, 105, 10)});

  simulator.LoadEvents({sim::testing::MakeAddEvent(10, 0, 1, Side::Buy, 50, 1)});
  simulator.Run();
  ASSERT_EQ(simulator.DebugBook().resting_order_ids(Side::Buy, 95).size(), 1u);
  ASSERT_EQ(simulator.DebugBook().resting_order_ids(Side::Sell, 105).size(), 1u);

  // A taker sell hits our resting bid@95 for qty=4 -- we buy.
  simulator.LoadEvents({sim::testing::MakeAddEvent(20, 1, 2, Side::Sell, 95, 4)});
  simulator.Run();

  EXPECT_EQ(maker.inventory(), 4);
  EXPECT_DOUBLE_EQ(maker.cash(), -4.0 * 95.0);
  ASSERT_EQ(maker.fills().size(), 1u);
  EXPECT_EQ(maker.fills()[0].side, Side::Buy);
  EXPECT_EQ(maker.fills()[0].price, 95);
  EXPECT_EQ(maker.fills()[0].quantity, 4u);
  EXPECT_EQ(maker.fills()[0].inventory_after, 4);

  // A taker buy hits our resting ask@105 for qty=3 -- we sell.
  simulator.LoadEvents({sim::testing::MakeAddEvent(30, 2, 3, Side::Buy, 105, 3)});
  simulator.Run();

  EXPECT_EQ(maker.inventory(), 1);
  EXPECT_DOUBLE_EQ(maker.cash(), -4.0 * 95.0 + 3.0 * 105.0);
  ASSERT_EQ(maker.fills().size(), 2u);
  EXPECT_EQ(maker.fills()[1].side, Side::Sell);
  EXPECT_EQ(maker.fills()[1].price, 105);
  EXPECT_EQ(maker.fills()[1].quantity, 3u);
  EXPECT_EQ(maker.fills()[1].inventory_after, 1);
}

}  // namespace
}  // namespace lob::mm
