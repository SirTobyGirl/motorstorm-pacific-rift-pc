#!/usr/bin/env bash
# Watch a play session on the handheld: wait for the game to start, then record resource use and
# stop the game if the handheld gets too hot. For when someone is playing and cannot watch the
# thermals themselves.
#
#   [ANDROID_MARGIN_C=3 | ANDROID_STOP_C=<C>] scripts/android_watch.sh [seconds=420]
#
# Start this on the PC first, then start the game on the handheld. Records for up to <seconds> from the
# game's start with scripts/android_monitor.sh (memory, cores busy, clocks, GPU, temperatures, frame
# times), and every 3 s checks every thermal sensor against its own first throttle point (about 108 C for
# the CPU cores, 95 C for the GPU; see scripts/android_session.sh): within ANDROID_MARGIN_C (default 3) of it,
# it stops the game and says which sensor. A fixed limit for all sensors: ANDROID_STOP_C=<C>. Raise the
# margin for a more cautious run.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SECS="${1:-420}"
cd "$ROOT"
# shellcheck source=android_session.sh
source scripts/android_session.sh
command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
echo "stops the game within ${ANDROID_MARGIN_C:-3} C of any sensor's own throttle point (ANDROID_STOP_C=<C> for one fixed limit); waiting for the game to start on the handheld (Ctrl+C to cancel) ..."
until [ -n "$(adb shell pidof "$ANDROID_PKG" 2>/dev/null | tr -d '\r')" ]; do sleep 2; done
echo "game started: watching for up to $SECS s per launch (a relaunch is recorded too; Ctrl+C ends)"
# The guard runs for the whole session, across relaunches, and stops the game whenever it is too hot.
( end=$((SECONDS + 3600))
  while [ $SECONDS -lt "$end" ]; do
      if [ -n "$(adb shell pidof "$ANDROID_PKG" 2>/dev/null | tr -d '\r')" ]; then android_guard || true; fi
      sleep 3
  done ) &
GUARD=$!
trap 'kill "$GUARD" 2>/dev/null' EXIT
n=0
while :; do
    n=$((n + 1))
    scripts/android_monitor.sh "$SECS"
    [ -f .build-android/monitor.csv ] && mv .build-android/monitor.csv ".build-android/monitor_launch$n.csv"
    echo "launch $n recorded: .build-android/monitor_launch$n.csv"
    # wait up to 90 s for the game to be started again; otherwise the session is over
    waited=0
    until [ -n "$(adb shell pidof "$ANDROID_PKG" 2>/dev/null | tr -d '\r')" ]; do
        sleep 2; waited=$((waited + 2)); [ $waited -ge 90 ] && break 2
    done
done
echo "hottest zone now: $(android_hot) C"
