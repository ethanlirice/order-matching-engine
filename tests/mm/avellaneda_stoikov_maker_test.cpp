#include "lob/mm/avellaneda_stoikov_maker.hpp"

#include <gtest/gtest.h>

#include "lob/sim/simulator.hpp"
#include "sim/support/sim_test_helpers.hpp"

namespace lob::mm {
namespace {

// Exposes the protected ComputeQuotes directly so formula tests can check
// an exact hand-computed result without going through Simulator -- driving
// this through a real Simulator run would mix in the base class's
// reconciliation settling dynamics (see MarketMaker's class comment on
// self-reference), which is a separate concern from whether the AS formula
// itself is transcribed correctly.
class TestableAvellanedaStoikovMaker : public AvellanedaStoikovMaker {
 public:
  using AvellanedaStoikovMaker::AvellanedaStoikovMaker;
  using AvellanedaStoikovMaker::ComputeQuotes;
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

TEST(AvellanedaStoikovMakerTest, MatchesHandComputedReservationPriceAndHalfSpread) {
  AvellanedaStoikovConfig config;
  config.gamma = 0.1;
  config.sigma = 1.0;
  config.kappa = 1.5;
  config.quote_size = 10;
  TestableAvellanedaStoikovMaker maker(config, /*horizon=*/10);

  // mid = 100, tau = 10 - 0 = 10, variance_term = 0.1*1^2*10 = 1.0,
  // inventory = 0 so reservation_price = mid = 100.
  // adverse_selection_term = log1p(0.1/1.5)/0.1 ~= 0.6453852.
  // half_spread = 1.0/2 + 0.6453852 ~= 1.1453852 (above the 1-tick floor).
  // bid = trunc(100 - 1.1453852) = 98; ask = trunc(100 + 1.1453852) = 101.
  Quote quote = maker.ComputeQuotes(TwoSidedSnapshot(90, 1000, 110, 1000), /*now=*/0);

  EXPECT_TRUE(quote.has_bid);
  EXPECT_EQ(quote.bid_price, 98);
  EXPECT_EQ(quote.bid_quantity, 10u);
  EXPECT_TRUE(quote.has_ask);
  EXPECT_EQ(quote.ask_price, 101);
  EXPECT_EQ(quote.ask_quantity, 10u);
}

TEST(AvellanedaStoikovMakerTest, SpecialCasesGammaZeroWithoutDivisionByZero) {
  AvellanedaStoikovConfig config;
  config.gamma = 0.0;
  config.sigma = 1.0;
  config.kappa = 1.5;
  config.quote_size = 10;
  TestableAvellanedaStoikovMaker maker(config, /*horizon=*/10);

  // gamma == 0: variance_term = 0 (no inventory skew, no tau-driven
  // widening), adverse_selection_term takes the real gamma -> 0 limit,
  // 1/kappa = 0.6667, so half_spread = max(0.6667, 1.0) = 1.0 (the floor).
  Quote quote = maker.ComputeQuotes(TwoSidedSnapshot(90, 1000, 110, 1000), /*now=*/0);

  EXPECT_EQ(quote.bid_price, 99);
  EXPECT_EQ(quote.ask_price, 101);
}

TEST(AvellanedaStoikovMakerTest, FloorClampsHalfSpreadToAtLeastOneTick) {
  AvellanedaStoikovConfig config;
  config.gamma = 0.001;
  config.sigma = 0.001;
  config.kappa = 1e6;
  config.quote_size = 10;
  TestableAvellanedaStoikovMaker maker(config, /*horizon=*/1);

  // Both the variance term and the adverse-selection term are
  // near-zero here (tiny sigma/gamma, huge kappa) -- without the floor,
  // half_spread would collapse toward 0, and bid/ask could even round to
  // the same tick.
  Quote quote = maker.ComputeQuotes(TwoSidedSnapshot(90, 1000, 110, 1000), /*now=*/0);

  ASSERT_TRUE(quote.has_bid);
  ASSERT_TRUE(quote.has_ask);
  EXPECT_GE(quote.ask_price - quote.bid_price, 2);
}

TEST(AvellanedaStoikovMakerTest, SkewsQuotesDownwardAfterAcquiringPositiveInventory) {
  AvellanedaStoikovConfig config;
  config.gamma = 0.1;
  config.sigma = 1.0;
  config.kappa = 1.5;
  config.quote_size = 10;
  AvellanedaStoikovMaker maker(config, /*horizon=*/10);
  sim::Simulator simulator(&maker, /*latency=*/0);

  simulator.LoadEvents({
      sim::testing::MakeAddEvent(0, 0, 1, Side::Buy, 90, 1000),
      sim::testing::MakeAddEvent(0, 1, 2, Side::Sell, 110, 1000),
  });
  simulator.Run();

  Price pre_fill_ask = 0;
  ASSERT_TRUE(simulator.DebugBook().best_ask(pre_fill_ask));

  // An aggressive taker sell hits our resting bid -- we acquire positive
  // inventory (we bought).
  simulator.LoadEvents({sim::testing::MakeAddEvent(1, 2, 3, Side::Sell, 1, 5)});
  simulator.Run();
  ASSERT_GT(maker.inventory(), 0);

  Price post_fill_ask = 0;
  ASSERT_TRUE(simulator.DebugBook().best_ask(post_fill_ask));

  // Positive inventory skews the reservation price down (mid -
  // inventory*variance_term), pulling both quotes lower to encourage
  // selling and discourage further buying.
  EXPECT_LT(post_fill_ask, pre_fill_ask);
}

}  // namespace
}  // namespace lob::mm
