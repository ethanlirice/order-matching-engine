# order-matching-engine

A low-latency limit-order-book (LOB) matching engine in C++, wrapped in an
event-driven simulator, used as a lab for inventory-aware market-making
strategies (Avellaneda-Stoikov, order-flow-imbalance).

**Status: M0 — scaffold.** Build system, CI, lint config, and interface-only
header stubs are in place. No matching logic exists yet — that lands in M1.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Sanitized build (ASan + UBSan)

```bash
cmake -S . -B build -DLOB_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Architecture

Four layers, each independently testable:

```
L4  Research / analytics   (Python, pybind11)
L3  Simulation / event loop (C++)
L2  Strategy interface      (C++)
L1  Matching engine / order book (C++)  <- the core
```

Architecture diagram, benchmark table, and result plots land in M6.
