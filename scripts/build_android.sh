#!/usr/bin/env bash
# Build a PRIVATE Android APK (arm64) of the port from YOUR lifted game code.
#
#   scripts/build_android.sh [--install]
#
# The APK contains code translated from your own copy of the game, so it is for
# your own device only: never publish it or attach it to a release. The game's
# data is not in it; copy that to the handheld with scripts/android_push.sh.
#
# Run scripts/setup.sh and scripts/lift.sh first (the lift is the same as for
# the Linux build). Needs an Android SDK with an NDK, build-tools and a
# platform, a JDK 17 or newer, CMake, Ninja, Python 3 and git.
#
# Environment (all optional):
#   ANDROID_SDK_ROOT / ANDROID_HOME   the SDK (default: ~/android-toolchain/android-sdk)
#   ANDROID_NDK_HOME                  the NDK (default: the newest under the SDK)
#   JAVA_HOME                         a JDK 17+ (default: ~/android-toolchain/jdk17, else the one on PATH)
#   ANDROID_DEPS                      where SDL2 and glslang for Android are built (default: ~/.local/ps3deps-android)
#   SDL2_SRC, GLSLANG_SRC             their source checkouts (default: cloned under .build-android/src)
#   JOBS, PORT_BUILD_MEM              parallel jobs (default 3: about 7.5 GB each) and memory cap (default 26G)
#   ANDROID_KEYSTORE                  the signing key (default: a debug key made in .build-android/)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
INSTALL=0; [ "${1:-}" = "--install" ] && INSTALL=1
B="$ROOT/.build-android"; mkdir -p "$B"

# ---- the toolchain ---------------------------------------------------------
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/android-toolchain/android-sdk}}"
[ -d "$SDK" ] || { echo "no Android SDK at $SDK (set ANDROID_SDK_ROOT)" >&2; exit 1; }
NDK="${ANDROID_NDK_HOME:-$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)}"
[ -f "$NDK/build/cmake/android.toolchain.cmake" ] || { echo "no NDK under $SDK/ndk (set ANDROID_NDK_HOME)" >&2; exit 1; }
BT="$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)"
JAR="$(ls -d "$SDK"/platforms/android-* 2>/dev/null | sort -V | tail -1)/android.jar"
[ -x "$BT/aapt2" ] && [ -f "$JAR" ] || { echo "need build-tools and a platform under $SDK" >&2; exit 1; }
if [ -z "${JAVA_HOME:-}" ] && [ -x "$HOME/android-toolchain/jdk17/bin/javac" ]; then export JAVA_HOME="$HOME/android-toolchain/jdk17"; fi
[ -n "${JAVA_HOME:-}" ] && export PATH="$JAVA_HOME/bin:$PATH"
command -v javac >/dev/null && command -v keytool >/dev/null || { echo "need a JDK (set JAVA_HOME)" >&2; exit 1; }
[ -f "$ROOT/work/src/recomp/ppu_recomp.h" ] || { echo "nothing lifted yet: run scripts/setup.sh and scripts/lift.sh" >&2; exit 1; }
echo "SDK $SDK | NDK $(basename "$NDK") | build-tools $(basename "$BT") | $(basename "$(dirname "$JAR")")"

# A foreign compiler or sysroot exported in the shell breaks cross builds.
unset CC CXX AR RANLIB LDFLAGS CMAKE_SYSROOT CMAKE_TOOLCHAIN_FILE LD_LIBRARY_PATH

# ---- SDL2 and glslang for Android ------------------------------------------
P="${ANDROID_DEPS:-$HOME/.local/ps3deps-android}"; mkdir -p "$P/build"
TC=(-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P")
SDL2_SRC="${SDL2_SRC:-$B/src/SDL2}"; GLSLANG_SRC="${GLSLANG_SRC:-$B/src/glslang}"
# SDL's Java sources (the launcher) are needed even when libSDL2.so is already built.
[ -d "$SDL2_SRC" ] || git clone -q --depth 1 --branch release-2.32.x https://github.com/libsdl-org/SDL.git "$SDL2_SRC"
if [ ! -f "$P/lib/libSDL2.so" ]; then
    echo "building SDL2 for Android"
    cmake -S "$SDL2_SRC" -B "$P/build/sdl2" -G Ninja "${TC[@]}" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF >/dev/null
    cmake --build "$P/build/sdl2" -j8 >/dev/null && cmake --install "$P/build/sdl2" >/dev/null
fi
if [ ! -f "$P/lib/libglslang.a" ]; then
    if [ ! -d "$GLSLANG_SRC" ]; then
        git clone -q --depth 1 --branch 15.1.0 https://github.com/KhronosGroup/glslang.git "$GLSLANG_SRC"
        ( cd "$GLSLANG_SRC" && python3 update_glslang_sources.py >/dev/null 2>&1 || true )
    fi
    echo "building glslang for Android"
    cmake -S "$GLSLANG_SRC" -B "$P/build/glslang" -G Ninja "${TC[@]}" -DENABLE_OPT=OFF -DBUILD_EXTERNAL=OFF \
        -DENABLE_GLSLANG_BINARIES=OFF -DBUILD_SHARED_LIBS=OFF >/dev/null
    cmake --build "$P/build/glslang" -j8 >/dev/null && cmake --install "$P/build/glslang" >/dev/null
fi
SDL_JAVA="$(dirname "$(find "$SDL2_SRC" -path '*org/libsdl/app/SDLActivity.java' 2>/dev/null | head -1)")"
[ -f "$SDL_JAVA/SDLActivity.java" ] || { echo "SDL's Java sources not found under $SDL2_SRC (set SDL2_SRC)" >&2; exit 1; }

# ---- the port (libmain.so) ---------------------------------------------------
cmake -S work/port -B "$B/build" -G Ninja "${TC[@]}" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_PREFIX_PATH="$P" -DCMAKE_FIND_ROOT_PATH="$P" \
    -DPS3RECOMP_DIR="$ROOT/ps3recomp" -DRECOMP_DIR="$ROOT/work/src/recomp" -DSPU_DIR="$ROOT/work/port/spu" \
    -DPS3RECOMP_RSX_VULKAN=ON -DPython3_EXECUTABLE="$ROOT/.venv/bin/python" >"$B/configure.log" 2>&1 \
    || { tail -20 "$B/configure.log" >&2; exit 1; }
# Each lifted chunk needs about 7.5 GB of RAM to compile, so 3 jobs peak near 22 GB.
JOBS="${JOBS:-3}"
if command -v systemd-run >/dev/null && systemd-run --user --scope -q true 2>/dev/null; then
    systemd-run --user --scope -q -p MemoryMax="${PORT_BUILD_MEM:-26G}" -p MemorySwapMax=0 nice -n 10 \
        ninja -C "$B/build" -j "$JOBS" -k 0
else
    nice -n 10 ninja -C "$B/build" -j "$JOBS" -k 0
fi
LIBMAIN="$B/build/libmain.so"
[ -f "$LIBMAIN" ] || { echo "libmain.so was not built (see the errors above)" >&2; exit 1; }

# ---- the APK ----------------------------------------------------------------
echo "packaging"
rm -rf "$B/apk"; mkdir -p "$B/apk/classes" "$B/apk/dex" "$B/apk/lib/arm64-v8a"
javac --release 8 -nowarn -classpath "$JAR" -d "$B/apk/classes" "$SDL_JAVA"/*.java \
    "$ROOT"/android/java/org/motorstormpr/rift/*.java 2>&1 | grep -v "^Note:\|^warning:" || true
"$BT/d8" --release --min-api 29 --lib "$JAR" --output "$B/apk/dex" $(find "$B/apk/classes" -name '*.class')
"$BT/aapt2" compile --dir "$ROOT/android/res" -o "$B/apk/res.zip"
"$BT/aapt2" link -o "$B/apk/base.apk" -I "$JAR" --manifest "$ROOT/android/AndroidManifest.xml" \
    --min-sdk-version 29 --target-sdk-version 33 "$B/apk/res.zip"
STRIP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
cp "$LIBMAIN" "$B/apk/lib/arm64-v8a/libmain.so"; cp "$P/lib/libSDL2.so" "$B/apk/lib/arm64-v8a/libSDL2.so"
"$STRIP" --strip-unneeded "$B/apk/lib/arm64-v8a/"*.so
python3 - "$B/apk" <<'PY'
import sys, zipfile, os
d = sys.argv[1]
with zipfile.ZipFile(os.path.join(d, "base.apk")) as src, zipfile.ZipFile(os.path.join(d, "unsigned.apk"), "w") as out:
    for i in src.infolist():
        out.writestr(i, src.read(i.filename), compress_type=i.compress_type)
    out.write(os.path.join(d, "dex", "classes.dex"), "classes.dex", zipfile.ZIP_DEFLATED)
    for so in sorted(os.listdir(os.path.join(d, "lib", "arm64-v8a"))):
        out.write(os.path.join(d, "lib", "arm64-v8a", so), "lib/arm64-v8a/" + so, zipfile.ZIP_STORED)   # stored: page-aligned below
PY
KS="${ANDROID_KEYSTORE:-$B/debug.keystore}"
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias androiddebugkey \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
"$BT/zipalign" -f -p 4 "$B/apk/unsigned.apk" "$B/apk/aligned.apk"
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out "$B/MotorStormPR-private.apk" "$B/apk/aligned.apk"
"$BT/apksigner" verify "$B/MotorStormPR-private.apk" && echo "built: $B/MotorStormPR-private.apk ($(du -h "$B/MotorStormPR-private.apk" | cut -f1))"
echo "PRIVATE: contains code translated from your game. Do not share it."

if [ "$INSTALL" = 1 ]; then
    command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
    adb install -r -d "$B/MotorStormPR-private.apk"
fi
