#!/usr/bin/env bash
# Build the libraries the port needs that many distributions do not package
# (SDL2, glslang, the Vulkan headers) into a local folder, from source.
#
#   scripts/build_libs.sh               installs into ./libs
#   LIBS_PREFIX=/some/dir scripts/build_libs.sh
#
# Needs git, CMake, a C and C++ compiler and (optionally) Ninja; it installs nothing
# system-wide. Anything already built is skipped, so it is safe to run again.
# The setup program points the build at ./libs automatically; by hand use
#   CMAKE_PREFIX_PATH=$PWD/libs scripts/build.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P="${LIBS_PREFIX:-$ROOT/libs}"
SRC="$P/src"; BLD="$P/build"
JOBS="${JOBS:-$(nproc)}"
SDL_TAG=release-2.32.x          # SDL 2.32 series
GLSLANG_TAG=15.1.0
VKH_TAG=v1.3.296
mkdir -p "$SRC" "$BLD"
gen=(); command -v ninja >/dev/null && gen=(-G Ninja)
for t in git cmake; do command -v $t >/dev/null || { echo "$t is needed" >&2; exit 1; }; done

fetch() {  # name url tag
    [ -d "$SRC/$1/.git" ] || git clone -q --depth 1 --branch "$3" "$2" "$SRC/$1"
}

if [ ! -f "$P/include/vulkan/vulkan.h" ]; then
    echo "== Vulkan headers"
    fetch Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers.git "$VKH_TAG"
    cmake -S "$SRC/Vulkan-Headers" -B "$BLD/vkh" "${gen[@]}" -DCMAKE_INSTALL_PREFIX="$P" >/dev/null
    cmake --install "$BLD/vkh" >/dev/null
fi
if [ ! -f "$P/lib/libSDL2.so" ] && [ ! -f "$P/lib64/libSDL2.so" ]; then
    echo "== SDL2 (a few minutes)"
    fetch SDL2 https://github.com/libsdl-org/SDL.git "$SDL_TAG"
    cmake -S "$SRC/SDL2" -B "$BLD/sdl2" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF >/dev/null
    cmake --build "$BLD/sdl2" -j"$JOBS" && cmake --install "$BLD/sdl2" >/dev/null
fi
if [ ! -f "$P/lib/libglslang.a" ] && [ ! -f "$P/lib64/libglslang.a" ]; then
    echo "== glslang (a few minutes)"
    fetch glslang https://github.com/KhronosGroup/glslang.git "$GLSLANG_TAG"
    cmake -S "$SRC/glslang" -B "$BLD/glslang" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
        -DENABLE_OPT=OFF -DBUILD_EXTERNAL=OFF -DENABLE_GLSLANG_BINARIES=OFF >/dev/null
    cmake --build "$BLD/glslang" -j"$JOBS" && cmake --install "$BLD/glslang" >/dev/null
fi
cfg="$(find "$P/include" -name SDL_config.h 2>/dev/null | head -1)"
if [ -n "$cfg" ] && ! grep -qE "SDL_VIDEO_DRIVER_(X11|WAYLAND) 1" "$cfg"; then
    echo "WARNING: this SDL2 was built without X11 or Wayland support, so it cannot open a window." >&2
    echo "Install your distribution's X11/Wayland development packages (libx11-dev libwayland-dev libxkbcommon-dev" >&2
    echo "libdecor-0-dev ... or the -devel equivalents), delete $P/build/sdl2 $P/lib*/libSDL2*, and run this again." >&2
fi
echo "libraries are in $P"
