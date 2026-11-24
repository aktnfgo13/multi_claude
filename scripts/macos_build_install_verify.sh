#!/bin/bash
# macOS (Apple Silicon) build + install + AU validation for "MFX Claude".
#
#   cd ~/multi_effects_claude && bash scripts/macos_build_install_verify.sh
#
# Safety:
#  * Installs ONLY "MFX Claude.component" / "MFX Claude.vst3" (manufacturer MfxL, subtype McL1).
#    Other plug-ins (Codex "Multi Effects", the old Claude v0.1 "MultiEffects") are never touched.
#  * If an "MFX Claude" bundle is already installed it is MOVED to
#    ~/Library/Audio/Plug-Ins/_mfx_claude_backup/<timestamp>/ first (nothing is deleted).
#  * Log: tests/results/macos_verify_<timestamp>.txt  (send this file back for the report)
set -u
cd "$(dirname "$0")/.."
TS=$(date +%Y%m%d-%H%M%S)
LOG="tests/results/macos_verify_${TS}.txt"
mkdir -p tests/results
exec > >(tee "$LOG") 2>&1

echo "== MFX Claude macOS verify ${TS}"
sw_vers; uname -m; xcodebuild -version 2>/dev/null | head -1; cmake --version | head -1
echo "source id: $(cat SOURCE_ID 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo unknown)"

BUILD=build-mac
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMFX_BUILD_PLUGIN=ON -DMFX_WITH_NAM=ON \
      -DCMAKE_OSX_ARCHITECTURES=arm64 || { echo "RESULT configure: FAIL"; exit 1; }
cmake --build "$BUILD" || { echo "RESULT build: FAIL"; exit 1; }
echo "RESULT build: PASS"

echo "== core tests"
if "$BUILD/mfx_tests" > "tests/results/macos_cpp_tests_${TS}.txt" 2>&1; then echo "RESULT mfx_tests: PASS"; else echo "RESULT mfx_tests: FAIL (see macos_cpp_tests_${TS}.txt)"; fi
tail -3 "tests/results/macos_cpp_tests_${TS}.txt"
"$BUILD/mfx_render" --version

ART="$BUILD/src/plugin/MultiEffects_artefacts/Release"
AU_SRC="$ART/AU/MFX Claude.component"
VST_SRC="$ART/VST3/MFX Claude.vst3"
[ -d "$AU_SRC" ] || { echo "RESULT AU bundle: MISSING ($AU_SRC)"; exit 1; }
file "$AU_SRC/Contents/MacOS/"* | head -2

COMP=~/Library/Audio/Plug-Ins/Components; VST=~/Library/Audio/Plug-Ins/VST3
BK=~/Library/Audio/Plug-Ins/_mfx_claude_backup/$TS
mkdir -p "$COMP" "$VST"
for dst in "$COMP/MFX Claude.component" "$VST/MFX Claude.vst3"; do
  if [ -e "$dst" ]; then mkdir -p "$BK"; mv "$dst" "$BK/"; echo "backed up existing $(basename "$dst") -> $BK"; fi
done
cp -R "$AU_SRC" "$COMP/" && cp -R "$VST_SRC" "$VST/" && echo "installed MFX Claude AU + VST3"
echo "other plug-ins present (untouched):"; ls "$COMP" | grep -i -E "multi|mfx" || true

killall -9 AudioComponentRegistrar 2>/dev/null; sleep 2
echo "== auval list"; auval -a 2>/dev/null | grep -i -E "MfxL|Mefx|Mfxp" || echo "(none listed)"
echo "== auval -v aumf McL1 MfxL"
if auval -v aumf McL1 MfxL > "tests/results/auval_${TS}.txt" 2>&1; then echo "RESULT auval: PASS"; else echo "RESULT auval: FAIL (see auval_${TS}.txt)"; fi
grep -E "AU VALIDATION|PASSED|FAILED|ERROR|WARNING" "tests/results/auval_${TS}.txt" | tail -15

echo
echo "Next (manual, Logic Pro): see docs/BUILD_GUIDE.md §2 'Logic 체크리스트' and report each item."
echo "log written: $LOG"
