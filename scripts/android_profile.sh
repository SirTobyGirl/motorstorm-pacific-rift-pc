#!/usr/bin/env bash
# CPU-profile the running game on the handheld with simpleperf and print where the time goes.
#
#   scripts/android_profile.sh [seconds=12]
#
# Needs the app running (start it first and let it reach the part you care about), the app
# marked profileable (it is, in android/AndroidManifest.xml), and the UNSTRIPPED libmain.so
# from the last scripts/build_android.sh in .build-android/build (the one on the handheld is
# stripped). Prints: CPU by thread, the hottest thread's top functions and its call chain.
# The raw data stays in .build-android/profile/ for `simpleperf report` or the NDK's scripts.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SECS="${1:-12}"; PKG=org.motorstormpr.rift
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/android-toolchain/android-sdk}}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)}"
SP="$NDK/simpleperf/bin/linux/x86_64/simpleperf"
OUT="$ROOT/.build-android/profile"; LIB="$ROOT/.build-android/build/libmain.so"
command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
[ -x "$SP" ] || { echo "no simpleperf under $NDK (set ANDROID_NDK_HOME)" >&2; exit 1; }
[ -f "$LIB" ] || { echo "no unstripped $LIB: run scripts/build_android.sh" >&2; exit 1; }
adb shell pidof "$PKG" >/dev/null 2>&1 || { echo "$PKG is not running" >&2; exit 1; }
mkdir -p "$OUT"

echo "recording $SECS s ..."
adb shell "rm -f /data/local/tmp/perf_cpu.data; simpleperf record --app $PKG --duration $SECS -f 2000 --call-graph fp -e cpu-clock -o /data/local/tmp/perf_cpu.data >/dev/null 2>&1"
adb pull -q /data/local/tmp/perf_cpu.data "$OUT/cpu.data"; adb shell rm -f /data/local/tmp/perf_cpu.data

# Symbols: the handheld's libraries are stripped, so lay the unstripped ones out at the device's paths.
DEVLIB="$(adb shell pm path "$PKG" | tr -d '\r' | head -1 | sed 's#package:##; s#/base.apk##')/lib/arm64"
rm -rf "$OUT/symfs"; mkdir -p "$OUT/symfs$DEVLIB"
ln -s "$LIB" "$OUT/symfs$DEVLIB/libmain.so"
SDL="$(ls "${ANDROID_DEPS:-$HOME/.local/ps3deps-android}/lib/libSDL2.so" 2>/dev/null || true)"; [ -n "$SDL" ] && ln -s "$SDL" "$OUT/symfs$DEVLIB/libSDL2.so"
rep() { "$SP" report -i "$OUT/cpu.data" --symfs "$OUT/symfs" "$@" 2>&1 | grep -v "^simpleperf\|^Cmdline\|^Arch\|^Event" | sed -E 's#/data/app/[^ ]*/##g'; }

echo; echo "== CPU by thread (percent of all samples) =="
rep --sort tid,comm --percent-limit 1.5 | head -14
# The three busiest threads: their top functions, and the call chain of the hottest one.
TOP="$(rep --sort tid --percent-limit 1 | awk '/^[0-9.]+%/{print $2}' | head -3)"
for t in $TOP; do
    echo; echo "== thread $t: top functions =="
    rep --tids "$t" --sort symbol --percent-limit 3 | grep -E "^[0-9.]+%" | head -5 | cut -c1-170
done
HOT="$(echo "$TOP" | head -1)"
if [ -n "$HOT" ]; then
    echo; echo "== hottest thread $HOT: call chain =="
    rep --tids "$HOT" --sort symbol -g caller --percent-limit 10 --max-stack 10 | sed -n '/__start_thread/,$p' | head -16 | cut -c1-170
fi
# The render-command (RSX) thread, found by its functions, whatever its rank: it paces the frame.
RSX="$(rep --sort tid,symbol --percent-limit 0.02 | grep -E "rsx_|vk_" | awk '{print $2}' | sort | uniq -c | sort -nr | head -1 | awk '{print $2}')"
if [ -n "$RSX" ]; then
    echo; echo "== RSX (render-command) thread $RSX: top functions =="
    rep --tids "$RSX" --sort symbol --percent-limit 0.8 | grep -E "^[0-9.]+%" | head -16 | cut -c1-170
    echo; echo "== RSX thread $RSX: by library =="
    rep --tids "$RSX" --sort dso --percent-limit 1 | grep -E "^[0-9.]+%" | head -6 | cut -c1-170
fi
echo; echo "data: $OUT/cpu.data"
