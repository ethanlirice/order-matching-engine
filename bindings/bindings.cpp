// Thin pybind11 wrapper: every field/function here is a direct passthrough
// to lob::mm::SimulationRunner's types. No simulation logic lives here --
// see mm/simulation_runner.cpp for that (and its own C++ tests).
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "lob/mm/simulation_runner.hpp"
#include "lob/sim/lobster_replay.hpp"

namespace py = pybind11;

PYBIND11_MODULE(lob_bindings, m) {
  m.doc() = "Bindings for the order-matching-engine's M5 market-making simulation runner";

  py::class_<lob::sim::SyntheticGeneratorConfig>(m, "SyntheticGeneratorConfig")
      .def(py::init<>())
      .def_readwrite("seed", &lob::sim::SyntheticGeneratorConfig::seed)
      .def_readwrite("duration", &lob::sim::SyntheticGeneratorConfig::duration)
      .def_readwrite("arrival_rate", &lob::sim::SyntheticGeneratorConfig::arrival_rate)
      .def_readwrite("base_price", &lob::sim::SyntheticGeneratorConfig::base_price)
      .def_readwrite("price_offset_ticks", &lob::sim::SyntheticGeneratorConfig::price_offset_ticks)
      .def_readwrite("min_quantity", &lob::sim::SyntheticGeneratorConfig::min_quantity)
      .def_readwrite("max_quantity", &lob::sim::SyntheticGeneratorConfig::max_quantity)
      .def_readwrite("aggressive_probability",
                     &lob::sim::SyntheticGeneratorConfig::aggressive_probability)
      .def_readwrite("cancel_probability", &lob::sim::SyntheticGeneratorConfig::cancel_probability);

  py::enum_<lob::Side>(m, "Side").value("Buy", lob::Side::Buy).value("Sell", lob::Side::Sell);

  py::enum_<lob::mm::StrategyKind>(m, "StrategyKind")
      .value("Naive", lob::mm::StrategyKind::Naive)
      .value("InventoryCapped", lob::mm::StrategyKind::InventoryCapped)
      .value("AvellanedaStoikov", lob::mm::StrategyKind::AvellanedaStoikov)
      .value("Ofi", lob::mm::StrategyKind::Ofi);

  py::class_<lob::mm::SimulationConfig>(m, "SimulationConfig")
      .def(py::init<>())
      .def_readwrite("generator", &lob::mm::SimulationConfig::generator)
      .def_readwrite("strategy_kind", &lob::mm::SimulationConfig::strategy_kind)
      .def_readwrite("half_spread", &lob::mm::SimulationConfig::half_spread)
      .def_readwrite("quote_size", &lob::mm::SimulationConfig::quote_size)
      .def_readwrite("max_inventory", &lob::mm::SimulationConfig::max_inventory)
      .def_readwrite("gamma", &lob::mm::SimulationConfig::gamma)
      .def_readwrite("sigma", &lob::mm::SimulationConfig::sigma)
      .def_readwrite("kappa", &lob::mm::SimulationConfig::kappa)
      .def_readwrite("ofi_beta", &lob::mm::SimulationConfig::ofi_beta)
      .def_readwrite("latency", &lob::mm::SimulationConfig::latency)
      .def_readwrite("markout_horizon", &lob::mm::SimulationConfig::markout_horizon)
      .def_readwrite("sharpe_bucket_duration", &lob::mm::SimulationConfig::sharpe_bucket_duration);

  py::class_<lob::mm::Fill>(m, "Fill")
      .def_readonly("timestamp", &lob::mm::Fill::timestamp)
      .def_readonly("event_ordinal", &lob::mm::Fill::event_ordinal)
      .def_readonly("side", &lob::mm::Fill::side)
      .def_readonly("price", &lob::mm::Fill::price)
      .def_readonly("quantity", &lob::mm::Fill::quantity)
      .def_readonly("inventory_after", &lob::mm::Fill::inventory_after);

  py::class_<lob::mm::MidPricePoint>(m, "MidPricePoint")
      .def_readonly("timestamp", &lob::mm::MidPricePoint::timestamp)
      .def_readonly("mid", &lob::mm::MidPricePoint::mid);

  py::class_<lob::mm::InventoryPoint>(m, "InventoryPoint")
      .def_readonly("timestamp", &lob::mm::InventoryPoint::timestamp)
      .def_readonly("inventory", &lob::mm::InventoryPoint::inventory);

  py::class_<lob::mm::FillMetrics>(m, "FillMetrics")
      .def_readonly("fill", &lob::mm::FillMetrics::fill)
      .def_readonly("pre_mid", &lob::mm::FillMetrics::pre_mid)
      .def_readonly("post_mid", &lob::mm::FillMetrics::post_mid)
      .def_readonly("effective_spread", &lob::mm::FillMetrics::effective_spread)
      .def_readonly("markout", &lob::mm::FillMetrics::markout)
      .def_readonly("pure_adverse_selection_cost",
                    &lob::mm::FillMetrics::pure_adverse_selection_cost);

  py::class_<lob::mm::PnlDecomposition>(m, "PnlDecomposition")
      .def_readonly("total_pnl", &lob::mm::PnlDecomposition::total_pnl)
      .def_readonly("spread_pnl", &lob::mm::PnlDecomposition::spread_pnl)
      .def_readonly("inventory_pnl", &lob::mm::PnlDecomposition::inventory_pnl);

  py::class_<lob::mm::MetricsSummary>(m, "MetricsSummary")
      .def_readonly("fill_metrics", &lob::mm::MetricsSummary::fill_metrics)
      .def_readonly("pnl", &lob::mm::MetricsSummary::pnl)
      .def_readonly("sharpe", &lob::mm::MetricsSummary::sharpe)
      .def_readonly("fill_rate", &lob::mm::MetricsSummary::fill_rate)
      .def_readonly("max_inventory", &lob::mm::MetricsSummary::max_inventory)
      .def_readonly("min_inventory", &lob::mm::MetricsSummary::min_inventory)
      .def_readonly("max_abs_inventory", &lob::mm::MetricsSummary::max_abs_inventory);

  py::class_<lob::mm::SimulationResult>(m, "SimulationResult")
      .def_readonly("fills", &lob::mm::SimulationResult::fills)
      .def_readonly("inventory_series", &lob::mm::SimulationResult::inventory_series)
      .def_readonly("mid_price_series", &lob::mm::SimulationResult::mid_price_series)
      .def_readonly("metrics", &lob::mm::SimulationResult::metrics);

  m.def("run_simulation", &lob::mm::RunSimulation, py::arg("config"),
        "Run a full synthetic-data market-making simulation; returns fills, inventory/mid-price "
        "series, and the metrics suite.");

  py::enum_<lob::sim::LobsterReplayEvent::Kind>(m, "LobsterEventKind")
      .value("Add", lob::sim::LobsterReplayEvent::Kind::Add)
      .value("Cancel", lob::sim::LobsterReplayEvent::Kind::Cancel)
      .value("Reduce", lob::sim::LobsterReplayEvent::Kind::Reduce);

  py::class_<lob::sim::LobsterReplayEvent>(m, "LobsterReplayEvent")
      .def(py::init<>())
      .def_readwrite("kind", &lob::sim::LobsterReplayEvent::kind)
      .def_readwrite("order_id", &lob::sim::LobsterReplayEvent::order_id)
      .def_readwrite("side", &lob::sim::LobsterReplayEvent::side)
      .def_readwrite("price", &lob::sim::LobsterReplayEvent::price)
      .def_readwrite("quantity", &lob::sim::LobsterReplayEvent::quantity);

  py::class_<lob::sim::BookLevelSnapshot>(m, "BookLevelSnapshot")
      .def_readonly("ask_price", &lob::sim::BookLevelSnapshot::ask_price)
      .def_readonly("ask_size", &lob::sim::BookLevelSnapshot::ask_size)
      .def_readonly("bid_price", &lob::sim::BookLevelSnapshot::bid_price)
      .def_readonly("bid_size", &lob::sim::BookLevelSnapshot::bid_size)
      .def_readonly("ask_empty", &lob::sim::BookLevelSnapshot::ask_empty)
      .def_readonly("bid_empty", &lob::sim::BookLevelSnapshot::bid_empty);

  py::class_<lob::sim::BookSnapshotRow>(m, "BookSnapshotRow")
      .def_readonly("levels", &lob::sim::BookSnapshotRow::levels);

  m.def("replay_lobster_events", &lob::sim::ReplayLobsterEvents, py::arg("events"),
        py::arg("num_levels"),
        "Replays a LOBSTER-derived event list (see analysis/lobster_loader.py) through a bare "
        "MatchingEngine; returns one top-num_levels BookSnapshotRow per input event.");
}
