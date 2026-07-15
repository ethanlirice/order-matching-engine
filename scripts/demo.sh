#!/usr/bin/env bash
# End-to-end demo (PROJECT_SPEC.md M6): configure, build, test, benchmark,
# and reproduce the M5 market-making headline plots -- one command, meant
# to run well under 10 minutes on a clean checkout.
#
# Usage: ./scripts/demo.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }

start_time=$(date +%s)

step "Configuring (Release, tests + bindings on)"
# Pin Python3_EXECUTABLE to whatever 'python3' this script itself will
# later use to run analysis/generate_plots.py -- on a machine with more
# than one Python installed, CMake's own detection can otherwise pick a
# different one than what's first on PATH, and the compiled lob_bindings
# module is tied to a specific Python's ABI (see README's "multi-Python"
# note). Harmless when there's only one Python: this just pins it to
# itself.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$(command -v python3)"

step "Building"
cmake --build build -j

step "Running the test suite"
ctest --test-dir build --output-on-failure

step "Benchmark smoke (matching engine + SPSC ring buffer)"
./build/benchmarks/lob_bench --benchmark_min_time=0.05s

step "Reproducing the M5 headline plots (analysis/generate_plots.py)"
# This step is allowed to fail without taking the whole demo down with it:
# the build/test/benchmark steps above are the load-bearing part of "done
# when a stranger can clone, build, test, benchmark..."; this last step
# needs a Python environment CMake didn't necessarily control, and the
# most common failure here (see README's "multi-Python" note) is a fixable
# environment mismatch, not a bug.
plots_ok=0
if ! python3 -c "import numpy, pandas, matplotlib" >/dev/null 2>&1; then
  echo "numpy/pandas/matplotlib not found for this interpreter -- run:"
  echo "  pip install -r analysis/requirements.txt"
  echo "then: python3 analysis/generate_plots.py"
elif ! python3 analysis/generate_plots.py; then
  echo
  echo "The plot sweep failed to import lob_bindings. If you have more than"
  echo "one Python installed, CMake may have compiled the module against a"
  echo "different interpreter than the 'python3' on your PATH -- compare:"
  echo "  grep Python3_EXECUTABLE build/CMakeCache.txt"
  echo "  which python3"
  echo "and, if they differ, reconfigure with:"
  echo "  cmake -S . -B build -DPython3_EXECUTABLE=\$(which python3)"
  echo "then rerun this script."
else
  plots_ok=1
fi

elapsed=$(( $(date +%s) - start_time ))
if [[ "$plots_ok" -eq 1 ]]; then
  step "Done in ${elapsed}s. Plots written to analysis/output/ (gitignored -- see README for the numbers)."
else
  step "Build/test/benchmark done in ${elapsed}s; see above for the plot step's fix."
fi
