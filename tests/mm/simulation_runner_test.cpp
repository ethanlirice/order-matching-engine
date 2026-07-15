#include "lob/mm/simulation_runner.hpp"

#include <gtest/gtest.h>

namespace lob::mm {
namespace {

SimulationConfig SmallConfig(StrategyKind kind) {
  SimulationConfig config;
  config.strategy_kind = kind;
  config.generator.seed = 42;
  config.generator.duration = 5000;
  config.generator.arrival_rate = 0.05;
  config.generator.base_price = 10000;
  config.generator.price_offset_ticks = 20;
  config.generator.aggressive_probability = 0.3;
  config.generator.cancel_probability = 0.2;
  config.half_spread = 5;
  config.quote_size = 10;
  config.max_inventory = 50;
  config.gamma = 0.1;
  config.sigma = 1.0;
  config.kappa = 1.5;
  config.ofi_beta = 1.0;
  config.latency = 5;
  config.markout_horizon = 100;
  config.sharpe_bucket_duration = 1000;
  return config;
}

class SimulationRunnerTest : public ::testing::TestWithParam<StrategyKind> {};

TEST_P(SimulationRunnerTest, ProducesInternallyConsistentResultsForEveryStrategyKind) {
  SimulationResult result = RunSimulation(SmallConfig(GetParam()));

  // fills and inventory_series are derived 1:1 from the same source, so
  // they must always have equal length.
  EXPECT_EQ(result.fills.size(), result.inventory_series.size());
  for (std::size_t i = 0; i < result.fills.size(); ++i) {
    EXPECT_EQ(result.inventory_series[i].timestamp, result.fills[i].timestamp);
    EXPECT_EQ(result.inventory_series[i].inventory, result.fills[i].inventory_after);
  }

  // A real, mostly-two-sided synthetic run should produce at least one
  // valid mid-price sample.
  EXPECT_FALSE(result.mid_price_series.empty());

  // metrics.fill_metrics is 1:1 with fills too.
  EXPECT_EQ(result.metrics.fill_metrics.size(), result.fills.size());

  // Fill rate is a pure function of fills.size() and session_end -- both
  // known here.
  EXPECT_DOUBLE_EQ(result.metrics.fill_rate, static_cast<double>(result.fills.size()) / 5000.0);
}

INSTANTIATE_TEST_SUITE_P(AllStrategyKinds, SimulationRunnerTest,
                         ::testing::Values(StrategyKind::Naive, StrategyKind::InventoryCapped,
                                           StrategyKind::AvellanedaStoikov, StrategyKind::Ofi));

TEST(SimulationRunnerTest, IsDeterministicGivenTheSameSeed) {
  SimulationConfig config = SmallConfig(StrategyKind::AvellanedaStoikov);
  SimulationResult a = RunSimulation(config);
  SimulationResult b = RunSimulation(config);

  ASSERT_EQ(a.fills.size(), b.fills.size());
  for (std::size_t i = 0; i < a.fills.size(); ++i) {
    EXPECT_EQ(a.fills[i].timestamp, b.fills[i].timestamp);
    EXPECT_EQ(a.fills[i].price, b.fills[i].price);
    EXPECT_EQ(a.fills[i].quantity, b.fills[i].quantity);
    EXPECT_EQ(a.fills[i].inventory_after, b.fills[i].inventory_after);
  }
  EXPECT_DOUBLE_EQ(a.metrics.pnl.total_pnl, b.metrics.pnl.total_pnl);
}

}  // namespace
}  // namespace lob::mm
