#pragma once

#include <cstdint>
#include <vector>

#include "lob/mm/market_maker.hpp"
#include "lob/mm/metrics.hpp"
#include "lob/sim/synthetic_generator.hpp"

namespace lob::mm {

enum class StrategyKind {
  Naive,
  InventoryCapped,
  AvellanedaStoikov,
  Ofi,
};

// One flat config surface covering every strategy kind's parameters --
// fields irrelevant to the selected kind are simply ignored. Kept flat
// (not a tagged union) since this is exposed directly to Python via
// pybind11, where a single struct with defaults is far more natural than
// a discriminated union.
struct SimulationConfig {
  sim::SyntheticGeneratorConfig generator;
  StrategyKind strategy_kind = StrategyKind::Naive;

  Price half_spread = 5;         // Naive, InventoryCapped
  Quantity quote_size = 10;      // all kinds
  Inventory max_inventory = 50;  // InventoryCapped

  double gamma = 0.1;     // AvellanedaStoikov, Ofi
  double sigma = 1.0;     // AvellanedaStoikov, Ofi
  double kappa = 1.5;     // AvellanedaStoikov, Ofi
  double ofi_beta = 1.0;  // Ofi

  Timestamp latency = 0;
  Timestamp markout_horizon = 100;
  Timestamp sharpe_bucket_duration = 1000;
};

struct MidPricePoint {
  Timestamp timestamp = 0;
  double mid = 0.0;
};

struct InventoryPoint {
  Timestamp timestamp = 0;
  Inventory inventory = 0;
};

struct SimulationResult {
  std::vector<Fill> fills;
  std::vector<InventoryPoint> inventory_series;
  std::vector<MidPricePoint> mid_price_series;
  MetricsSummary metrics;
};

// Generates synthetic order flow, runs it through a Simulator with the
// configured strategy attached, and computes the full metrics suite. The
// one entry point Python drives sweeps/plotting through (PROJECT_SPEC.md
// §11) -- every strategy stays C++-only, no Python-strategy trampoline.
SimulationResult RunSimulation(const SimulationConfig& config);

}  // namespace lob::mm
