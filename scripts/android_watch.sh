#!/usr/bin/env bash
# Watch a play session on the handheld: wait for the game to start, then record resource use and
# stop the game if the handheld gets too hot. For when someone is playing and cannot watch the
# thermals themselves.
#
#   [ANDROID_STOP_C=97] scripts/android_watch.sh [seconds=420]
#
# Start this on the PC first, then start the game on the handheld. Records for up to <seconds> from the
# game's start with scripts/android_monitor.sh (memory, cores busy, clocks, GPU, temperatures, frame
# times), and every 3 s checks the hottest thermal zone: at or above ANDROID_STOP_C (default 97 C) it
# stops the game and says so. The GPU zones' first thermal mitigation is at 95 C and the CPU cores' at
# 108 C (see scripts/android_session.sh); 97 leaves room for a hot but normal race. Lower it for a
# more cautious run. It ends when the game does.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SECS="${1:-420}"
export ANDROID_STOP_C="${ANDROID_STOP_C:-97}"
cd "$ROOT"
# shellcheck source=android_session.sh
source scripts/android_session.sh
command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
echo "stop threshold ${ANDROID_STOP_C} C; waiting for the game to start on the handheld (Ctrl+C to cancel) ..."
until [ -n "$(adb shell pidof "$ANDROID_PKG" 2>/dev/null | tr -d '\r')" ]; do sleep 2; done
echo "game started: watching for up to $SECS s"
( end=$((SECONDS + SECS + 30))
  while [ $SECONDS -lt "$end" ]; do
      [ -n "$(adb shell pidof "$ANDROID_PKG" 2>/dev/null | tr -d '\r')" ] || break
      android_guard || break
      sleep 3
  done ) &
GUARD=$!
scripts/android_monitor.sh "$SECS"
kill "$GUARD" 2>/dev/null; wait "$GUARD" 2>/dev/null
echo "hottest zone now: $(android_hot) C"
