#!/usr/bin/env bash
# Copy YOUR game to the phone's app storage (the APK contains none of it).
#
#   scripts/android_push.sh [--flash DIR]
#
# Pushes everything under disc/ (following its links into your dump) to
# /sdcard/Android/data/org.motorstormpr.rift/files/disc, plus the PS3 firmware's
# dev_flash from --flash DIR (default: ./dev_flash, else RPCS3's copy) when there
# is one. Install the APK first. Over USB 2 the 13 GB take several minutes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "no device (adb devices; accept the prompt on the phone)" >&2; exit 1; }
PKG=org.motorstormpr.rift
adb shell pm path "$PKG" >/dev/null 2>&1 || { echo "$PKG is not installed: run scripts/build_android.sh --install first" >&2; exit 1; }
DEST="/sdcard/Android/data/$PKG/files"
FLASH=""
[ "${1:-}" = "--flash" ] && FLASH="${2:?--flash needs a folder}"
if [ -z "$FLASH" ]; then
    for d in "$ROOT/dev_flash" "$HOME/.config/rpcs3/dev_flash" "$HOME/.var/app/net.rpcs3.RPCS3/config/rpcs3/dev_flash"; do
        [ -f "$d/sys/external/flashMP3.pic" ] && { FLASH="$d"; break; }
    done
fi
[ -f "$ROOT/disc/PS3_GAME/USRDIR/EBOOT.elf" ] || { echo "no disc/PS3_GAME/USRDIR/EBOOT.elf (run scripts/prepare_disc.sh)" >&2; exit 1; }

# Android 11+ does not let `adb push` make sub-folders below Android/data ("secure_mkdirs failed"), but
# `adb shell mkdir -p` can. So make every folder first, then push each file into a folder that exists.
# (adb shell reads stdin, so inside a read loop it needs </dev/null or it eats the list.)
push_tree() {   # $1 = folder holding the tree, $2 = the tree's name, $3 = destination on the phone
    ( cd "$1"
      find -L "$2" -type d | while IFS= read -r d; do adb shell mkdir -p "'$3/$d'" </dev/null; done
      find -L "$2" -type f | while IFS= read -r f; do
          echo "  $f  ($(du -shL "$f" | cut -f1))"
          adb push -q "$(readlink -f "$f")" "$3/$f"
      done )
}
echo "pushing the game to $DEST/disc"
adb shell mkdir -p "'$DEST/disc'"
push_tree "$ROOT/disc" PS3_GAME "$DEST/disc"
if [ -n "$FLASH" ]; then
    echo "pushing the firmware files from $FLASH"
    push_tree "$(dirname "$FLASH")" "$(basename "$FLASH")" "$DEST"
else echo "no dev_flash found: fonts and the MP3 decoder will be missing"; fi
echo "done. Start the app on the phone; its log is $DEST/run.log (adb pull it)."
