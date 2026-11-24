#!/usr/bin/env bash
# Runs every automated check and writes results to tests/results/.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD"
mkdir -p tests/results
"$BUILD/mfx_tests" --json tests/results/cpp_tests.json
python3 tests/parity/run_parity.py --build "$BUILD"
"$BUILD/mfx_modeling_study" audio/output/modeling_study
if command -v ngspice >/dev/null; then python3 research/spice/run_spice.py validate; else echo "ngspice not found: SPICE validation skipped"; fi
python3 tools/benchmark/run_benchmarks.py run --build "$BUILD"
echo "All checks finished. See tests/results/."
