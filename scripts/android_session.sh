# Helpers for a SUPERVISED session with the game on an Android handheld. Source it; do not run it.
#
#   source scripts/android_session.sh
#   android_cool                         # wait until the handheld is below 60 C
#   android_env 'PS3_SKIP_START=1\nPAD_FILE=pad.txt\n'   # the app's env.txt
#   android_launch; sleep 60; android_guard && android_stat
#   android_press 0x0008 20              # Start (masks: work/tools/pad.sh); needs PAD_FILE=pad.txt in env.txt
#   android_shot name                    # screenshot of the game's screen to .build-android/shots/name.png
#
# Only use this while you can watch the handheld. The game runs the CPU and GPU hard: on an AYN Thor the
# hottest zone reached 94 to 96 C within about 75 s, and the GPU zones' first thermal mitigation is at
# 95 C (CPU cores 108 C, shutdown 125 C; `cat /sys/class/thermal/thermal_zone*/trip_point_*`). Every
# android_guard call stops the game if any zone is at or above ANDROID_STOP_C (default 100). Keep runs
# short, call the guard often, and cool down between them.
#   ANDROID_STOP_C       stop threshold, C (default 100)
#   ANDROID_DISPLAY_ID   SurfaceFlinger display id for screenshots (default: the first one it lists)
ANDROID_PKG=org.motorstormpr.rift
ANDROID_DIR=/sdcard/Android/data/$ANDROID_PKG/files
ANDROID_STOP_C="${ANDROID_STOP_C:-100}"
ANDROID_SHOTS="${ANDROID_SHOTS:-$PWD/.build-android/shots}"
mkdir -p "$ANDROID_SHOTS"

android_hot() { adb shell 'for z in /sys/class/thermal/thermal_zone*; do cat $z/temp 2>/dev/null; done' | tr -d '\r' | sort -nr | head -1 | awk '{printf "%.0f", $1/1000}'; }
android_guard() {
    local t; t="$(android_hot)"
    if [ "${t:-0}" -ge "$ANDROID_STOP_C" ]; then adb shell am force-stop "$ANDROID_PKG"; echo "!! $t C >= $ANDROID_STOP_C C: game stopped"; return 1; fi
    return 0
}
android_cool() {
    adb shell am force-stop "$ANDROID_PKG" >/dev/null 2>&1
    local i; for i in $(seq 1 30); do [ "$(android_hot)" -lt 60 ] && break; sleep 10; done
    echo "cooled to $(android_hot) C"
}
android_env() { printf '%b' "$1" > "${TMPDIR:-/tmp}/android_env.txt"; adb push -q "${TMPDIR:-/tmp}/android_env.txt" "$ANDROID_DIR/env.txt"; }
android_launch() { adb shell input keyevent KEYCODE_WAKEUP; adb shell am start --display 0 -n "$ANDROID_PKG/.MainActivity" >/dev/null 2>&1; }
# pad.txt must be made by the app (it is, when PAD_FILE is set) so the game can empty it after each press;
# a file made by `adb push` belongs to the shell user and one press then repeats for ever.
android_press() { adb shell "echo '$1 ${2:-20}' > $ANDROID_DIR/pad.txt"; }
android_shot() {
    local id="${ANDROID_DISPLAY_ID:-$(adb shell dumpsys SurfaceFlinger --display-id 2>/dev/null | tr -d '\r' | grep -o 'Display [0-9]*' | head -1 | cut -d' ' -f2)}"
    adb exec-out screencap ${id:+-d "$id"} -p > "$ANDROID_SHOTS/$1.png" 2>/dev/null; echo "$ANDROID_SHOTS/$1.png"
}
android_stat() {
    local p; p="$(adb shell pidof "$ANDROID_PKG" | tr -d '\r')"
    echo "t=$(android_hot)C alive=${p:+yes} log=$(adb shell "wc -l < $ANDROID_DIR/run.log" | tr -d '\r') frames=$(adb shell "wc -l < $ANDROID_DIR/fliplog.txt 2>/dev/null" | tr -d '\r')"
}
