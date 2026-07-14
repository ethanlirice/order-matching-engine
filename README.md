# order-matching-engine

A low-latency limit-order-book (LOB) matching engine in C++, wrapped in an
event-driven simulator, used as a lab for inventory-aware market-making
strategies (Avellaneda-Stoikov, order-flow-imbalance).

**Status: M2 — more order types.** All five order types (Limit, Market,
IOC, FOK, Post-Only), price-time-priority matching, cancel, modify, and
trade events are implemented and covered by unit, invariant, determinism,
and fuzz tests. No performance work has started yet (M3).

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

## Fuzzing

The matching engine has a libFuzzer harness (`tests/fuzz/fuzz_matching.cpp`)
that decodes a mutated byte buffer into a sequence of add/cancel/modify
operations and checks every invariant after each one. It requires real LLVM
clang (libFuzzer's runtime isn't shipped by AppleClang on macOS); a short
smoke run is wired into `ctest` automatically whenever the compiler
qualifies. To fuzz for longer locally (e.g. via Homebrew's llvm on macOS):

```bash
cmake -S . -B build-fuzz \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang
cmake --build build-fuzz -j
./build-fuzz/tests/lob_fuzz_matching -max_total_time=60
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
