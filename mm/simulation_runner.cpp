#include "lob/mm/simulation_runner.hpp"

#include <memory>
#include <utility>

#include "lob/mm/avellaneda_stoikov_maker.hpp"
#include "lob/mm/inventory_capped_maker.hpp"
#include "lob/mm/naive_maker.hpp"
#include "lob/mm/ofi_maker.hpp"
#include "lob/sim/simulator.hpp"

namespace lob::mm {

namespace {

std::unique_ptr<MarketMaker> MakeStrategy(const SimulationConfig& config) {
  switch (config.strategy_kind) {
    case StrategyKind::Naive: {
      NaiveMakerConfig c;
      c.half_spread = config.half_spread;
      c.quote_size = config.quote_size;
      return std::make_unique<NaiveMaker>(c);
    }
    case StrategyKind::InventoryCapped: {
      InventoryCappedMakerConfig c;
      c.half_spread = config.half_spread;
      c.quote_size = config.quote_size;
      c.max_inventory = config.max_inventory;
      return std::make_unique<InventoryCappedMaker>(c);
    }
    case StrategyKind::AvellanedaStoikov: {
      AvellanedaStoikovConfig c;
      c.gamma = config.gamma;
      c.sigma = config.sigma;
      c.kappa = config.kappa;
      c.quote_size = config.quote_size;
      return std::make_unique<AvellanedaStoikovMaker>(c, config.generator.duration);
    }
    case StrategyKind::Ofi: {
      OfiMakerConfig c;
      c.as_config.gamma = config.gamma;
      c.as_config.sigma = config.sigma;
      c.as_config.kappa = config.kappa;
      c.as_config.quote_size = config.quote_size;
      c.ofi_beta = config.ofi_beta;
      return std::make_unique<OfiMaker>(c, config.generator.duration);
    }
  }
  // Unreachable: the switch above exhaustively handles every StrategyKind
  // enumerator (no default label, deliberately -- adding a new enumerator
  // without a case here triggers a real -Wswitch warning under -Werror).
  // This trailing return only exists to satisfy -Wreturn-type.
  return nullptr;
}

}  // namespace

SimulationResult RunSimulation(const SimulationConfig& config) {
  std::unique_ptr<MarketMaker> strategy = MakeStrategy(config);

  sim::SyntheticGenerator generator(config.generator);
  std::vector<sim::Event> events = generator.Generate();

  sim::Simulator simulator(strategy.get(), config.latency);
  simulator.LoadEvents(std::move(events));
  simulator.Run();

  SimulationResult result;
  result.fills = strategy->fills();

  result.inventory_series.reserve(result.fills.size());
  for (const Fill& fill : result.fills) {
    result.inventory_series.push_back(InventoryPoint{fill.timestamp, fill.inventory_after});
  }

  const std::vector<sim::MidPriceSample>& samples = simulator.market_data_log().samples();
  result.mid_price_series.reserve(samples.size());
  for (const sim::MidPriceSample& sample : samples) {
    if (sample.valid) {
      result.mid_price_series.push_back(MidPricePoint{sample.timestamp, sample.mid});
    }
  }

  MetricsConfig metrics_config;
  metrics_config.markout_horizon = config.markout_horizon;
  metrics_config.session_end = config.generator.duration;
  metrics_config.sharpe_bucket_duration = config.sharpe_bucket_duration;
  result.metrics = ComputeMetrics(*strategy, simulator.market_data_log(), metrics_config);

  return result;
}

}  // namespace lob::mm
