# CLAUDE.md — notes for coding agents working in this repository

**Read `AGENTS.md` first**, then `docs/AI_PROJECT_HANDOFF.md` (Quick Context) for the current state. Its mandatory requirements (R1–R6: modular architecture, JUCE/OS-independent core,
macOS/Windows common DSP and preset compatibility, ARM hardware portability, real-time safety and memory management,
preservation of existing models/presets) override anything below.

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

* Reference project (Codex, `~/multi_effects`): read-only. Never copy its code/presets into this tree; compare only
  through `apps/mfx_compare_drive` with `-DMFX_REFERENCE_DIR=...`. Never reuse its plug-in IDs.
* Plug-in identity: MFX Claude / manufacturer MfxL / subtype McL1 / com.mfxlab.mfxclaude. Do not change without a
  DECISION_LOG entry (hosts treat it as a different plug-in).
* Custom Drive has a `model` parameter (v0.4): 0 = v1 (v0.1, bit-exact), 1 = v2 (default), 2 = v2 + ADAA. Never change
  what an existing model sounds like; add a new model value instead (R6).
  After mapping changes also re-run `apps/mfx_compare_drive` and `tools/benchmark/preset_mapping_impact.py`.
* v0.6 types: `delay_tape_premium` (tape transport model), `delay_bbd_premium` (clocked BBD), `comp_fet` (feedback FET
  compressor). `delay` gained 5 appended stereo params whose defaults keep v0.5 output bit-identical — keep it so.
* v0.7 types: `gate_premium`, `eq_parametric_premium` (TPT SVF). `delay_bbd_premium` gained `model` (v1 = v0.6 bit-exact,
  legacy default) and `chips`. Host automation: all params (up to 24 per effect) since v0.8.
* v0.8 types: chorus/vibrato/phaser/univibe/flanger/tremolo/rotary `_premium` (src/dsp/modulation/PremiumMod.*).
  Host parameters: the 275-entry v0.2 layout is frozen (index = AU/VST3 ID); params 16..23 per slot are appended after it
  (version hint 2). Never insert host parameters before existing ones. Plug-in tests: `-DMFX_BUILD_PLUGIN_TESTS=ON`.
* v0.9 types: `reverb_spring_premium` (dispersive stretched all-pass loops), `reverb_hall_premium` (ER + 16-line FDN),
  `reverb_shimmer` (pitch-shifted FDN) in src/dsp/reverb/PremiumSpace.* — Freeze and wet-only Ducking. Legacy `reverb` and
  `reverb_plate_premium` are untouched (Plate v2 rejected, D-072). Tests: tests/audio/test_reverb_v09.cpp.
* Unverified items are written as NOT TESTED / BLOCKED. Never claim listening results; never call a GRU model "NAM".

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
* Decisions: docs/DECISION_LOG.md. Experiments: docs/EXPERIMENT_LOG.md. Status: docs/INTEGRATION_REPORT.md
  (v0.1: docs/COMPLETION_REPORT.md). Open problems: docs/KNOWN_ISSUES.md. Parameters: docs/PARAMETER_SPECIFICATION.md.
* Visualize: `src/engine/AnalysisTap.h` (RT, SPSC) -> `src/engine/AnalysisWorker.*` (non-RT) -> `src/ui/AnalysisView.*`.
