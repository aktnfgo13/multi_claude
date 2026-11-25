#!/bin/bash
# Hide the legacy Claude v0.1 plug-in ("MultiEffects", aumf Mfx1 Mfxp, com.multieffects.mfx) from Logic
# WITHOUT deleting it. Preferred first step is Logic's Plug-in Manager (see docs/BUILD_GUIDE.md §2-1);
# use this script only if you want the old bundles out of the scan path.
#
#   bash scripts/macos_legacy_au_disable.sh            # dry run: shows what would move
#   bash scripts/macos_legacy_au_disable.sh --apply    # moves the bundles to a dated backup folder
#   bash scripts/macos_legacy_au_disable.sh --restore DIR   # moves them back from that backup folder
#
# Never touches: "MFX Claude" (MfxL/McL1, current), "Multi Effects" (Codex, Mefx/Mfx6), Logic projects.
# Logic projects that used the legacy plug-in will show it as missing while it is moved; --restore fixes that.
set -u
PI=~/Library/Audio/Plug-Ins
LEGACY=("$PI/Components/MultiEffects.component" "$PI/VST3/MultiEffects.vst3")
EXPECT_BUNDLE="com.multieffects.mfx"

case "${1:-}" in
  --restore)
    SRC="${2:?backup folder required}"
    for f in "$SRC"/MultiEffects.component; do [ -e "$f" ] && mv -n "$f" "$PI/Components/" && echo "restored $f"; done
    for f in "$SRC"/MultiEffects.vst3; do [ -e "$f" ] && mv -n "$f" "$PI/VST3/" && echo "restored $f"; done
    killall -9 AudioComponentRegistrar 2>/dev/null; exit 0 ;;
esac

for f in "${LEGACY[@]}"; do
  [ -e "$f" ] || { echo "not present: $f"; continue; }
  id=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$f/Contents/Info.plist" 2>/dev/null)
  if [ "$id" != "$EXPECT_BUNDLE" ]; then echo "SKIP $f (bundle id '$id' is not the legacy Claude plug-in)"; continue; fi
  if [ "${1:-}" = "--apply" ]; then
    DEST="$PI/_disabled_legacy_claude/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$DEST" && mv -n "$f" "$DEST/" && echo "moved $f -> $DEST (restore: bash $0 --restore \"$DEST\")"
  else
    echo "would move: $f ($id)"
  fi
done
[ "${1:-}" = "--apply" ] && killall -9 AudioComponentRegistrar 2>/dev/null
echo "auval check:"; auval -a 2>/dev/null | grep -E "MfxL|Mfxp|Mefx" || true
