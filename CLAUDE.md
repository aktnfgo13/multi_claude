# CLAUDE.md — notes for coding agents working in this repository

## Golden rules
* `mfx_core` (src/core, src/dsp, src/engine, src/presets, src/platform, src/neural) must never include JUCE or OS headers
  (except platform/ and the denormal helper). The plug-in is a thin wrapper.
* Audio thread (`Effect::process`, `Engine::process`): no allocation, locks, file I/O, logging. `tests/performance`
  counts allocations — keep it at zero.
* Every DSP block exposes physical/musical parameters through `ParameterSet`; ids are stable (presets depend on them).
  Never rename an id; add new parameters at the end of the list only if presets must stay compatible.
* Do not mark a feature done without a test. Do not create "measurement" data — SPICE and synthetic data must be
  labelled (`data_kind`).
* If you change `customDriveMapping`, the Biquad formulas or the oversampler, update `research/common/mfx_dsp.py`
  and run `python3 tests/parity/run_parity.py`.

## Build & test
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/mfx_tests                 # all C++ tests   (--group unit|audio|integration|performance, --filter name)
python3 tests/parity/run_parity.py
python3 research/spice/run_spice.py validate      # needs ngspice
scripts/run_all_checks.sh         # everything above + benchmark
```
Plug-in: `-DMFX_BUILD_PLUGIN=ON [-DMFX_JUCE_DIR=...] [-DMFX_WITH_NAM=ON -DMFX_NAM_DIR=... -DMFX_EIGEN_DIR=...]`.

## Where things are
* Add an effect: implement `Effect` (or `ControlRateEffect` / `OversampledDrive`), register it in
  `src/engine/EffectFactory.cpp`, add it to CMake, the generic tests pick it up automatically
  (finite/bounded, block-size invariance, parameter description).
* Decisions: docs/DECISION_LOG.md. Experiments: docs/EXPERIMENT_LOG.md. Status: docs/COMPLETION_REPORT.md.
