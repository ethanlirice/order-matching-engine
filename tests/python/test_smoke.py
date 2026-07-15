"""Smoke test for the lob_bindings pybind11 module (PROJECT_SPEC.md §11's
"import + trivial RunSimulation call" requirement). Deliberately no
pytest dependency -- run directly with the module's build directory on
PYTHONPATH (CMake/CTest sets this up automatically; see tests/CMakeLists.txt).
"""

import sys

import lob_bindings as lob


def test_import():
    assert lob is not None
    assert hasattr(lob, "run_simulation")


def test_trivial_run_simulation_produces_sane_nonempty_results():
    config = lob.SimulationConfig()
    config.generator.seed = 1
    config.generator.duration = 5000
    config.generator.arrival_rate = 0.05
    config.generator.base_price = 10000
    config.generator.price_offset_ticks = 20
    config.generator.aggressive_probability = 0.3
    config.generator.cancel_probability = 0.2
    config.strategy_kind = lob.StrategyKind.Naive
    config.half_spread = 5
    config.quote_size = 10
    config.latency = 5
    config.markout_horizon = 100
    config.sharpe_bucket_duration = 1000

    result = lob.run_simulation(config)

    assert isinstance(result.fills, list)
    assert isinstance(result.mid_price_series, list)
    assert len(result.mid_price_series) > 0
    assert isinstance(result.metrics.fill_rate, float)
    assert result.metrics.fill_rate >= 0.0
    assert len(result.fills) == len(result.inventory_series)
    assert len(result.fills) == len(result.metrics.fill_metrics)


def test_every_strategy_kind_runs():
    for kind in (
        lob.StrategyKind.Naive,
        lob.StrategyKind.InventoryCapped,
        lob.StrategyKind.AvellanedaStoikov,
        lob.StrategyKind.Ofi,
    ):
        config = lob.SimulationConfig()
        config.generator.seed = 7
        config.generator.duration = 3000
        config.generator.arrival_rate = 0.05
        config.strategy_kind = kind
        result = lob.run_simulation(config)
        assert isinstance(result.fills, list)


def main():
    test_import()
    test_trivial_run_simulation_produces_sane_nonempty_results()
    test_every_strategy_kind_runs()
    print("OK")


if __name__ == "__main__":
    main()
    sys.exit(0)
