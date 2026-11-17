#!/usr/bin/env bash
# Check that this machine and this checkout are ready to lift, build and run
# the port, and say exactly what is missing. Read-only: it changes nothing.
#
#   scripts/doctor.sh
#
# Exit status: 0 when nothing is missing, 1 when something required is.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXPECT_SHA=bbb7fe2545e54847483439f59cda54e907db23a111d7afe9a1b655624886b287
EXPECT_PSARC=4811608561
fails=0; warns=0
ok()   { printf '  ok    %s\n' "$1"; }
warn() { printf '  warn  %s\n' "$1"; warns=$((warns + 1)); }
bad()  { printf '  MISSING  %s\n' "$1"; fails=$((fails + 1)); }

OS="$(uname -s)"
echo "Platform"
if [ "$OS" != Linux ]; then bad "unsupported system: $OS (Linux only for now)"; else
    ok "Linux $(uname -m)"
    [ "$(uname -m)" = x86_64 ] && ok "x86-64" || bad "the Linux build needs x86-64"
    if grep -qw avx2 /proc/cpuinfo 2>/dev/null; then ok "AVX2 (lift is built for x86-64-v3)"
    else warn "no AVX2: set PORT_PPU_ARCH and PORT_SPU_ARCH empty in CMake (README, Requirements)"; fi
    mem_gb=$(awk '/MemTotal/ {printf "%d", $2/1048576}' /proc/meminfo 2>/dev/null || echo 0)
    if [ "$mem_gb" -ge 30 ]; then ok "RAM ${mem_gb} GB"
    elif [ "$mem_gb" -ge 16 ]; then warn "RAM ${mem_gb} GB: build with JOBS=4 and a PORT_BUILD_MEM about 80% of it"
    else bad "RAM ${mem_gb} GB: the build needs about 16 GB at least"; fi
fi
free_gb=$(df -Pk "$ROOT" | awk 'NR==2 {printf "%d", $4/1048576}')
[ "$free_gb" -ge 20 ] && ok "free disk ${free_gb} GB" || warn "free disk ${free_gb} GB: about 20 GB are needed"

echo "Environment"
# A toolchain exported into the shell (CC, LDFLAGS, CMAKE_SYSROOT ...) is
# picked up by CMake and can break the build with link errors.
leak=""
for v in CC CXX LDFLAGS CMAKE_SYSROOT CMAKE_TOOLCHAIN_FILE; do
    [ -n "${!v:-}" ] && leak="$leak $v"
done
if [ -n "$leak" ]; then
    warn "these are set in your shell and CMake will use them:$leak"
    unset_args=""; for v in $leak; do unset_args="$unset_args -u $v"; done
    echo "          if the build fails to link or compile, retry with them unset: env$unset_args scripts/build.sh"
else ok "no compiler variables set in the shell"; fi

echo "Tools"
need() { command -v "$1" >/dev/null && ok "$1" || bad "$1 ($2)"; }
need git "git"
need cmake "CMake 3.20 or newer"
need ninja "Ninja"
need rsync "rsync"
if command -v clang-20 >/dev/null || command -v clang >/dev/null; then ok "clang"; else bad "clang"; fi
py=""
for c in "${PYTHON3:-}" /usr/bin/python3 python3.13 python3.12 python3.11 python3.10 python3; do
    [ -n "$c" ] && command -v "$c" >/dev/null || continue
    "$c" -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null && { py="$c"; break; }
done
[ -n "$py" ] && ok "Python 3.10+ ($py)" || bad "Python 3.10 or newer"
if [ "$OS" = Linux ]; then
    pc_ok() { pkg-config --exists "$1" 2>/dev/null; }
    # A local install is found through CMAKE_PREFIX_PATH (see README, Requirements).
    in_prefix() {
        [ -n "${CMAKE_PREFIX_PATH:-}" ] || return 1
        local pfx p; IFS=: read -ra pfx <<< "$CMAKE_PREFIX_PATH"
        for p in "${pfx[@]}"; do find "$p" -name "$1" 2>/dev/null | grep -q . && return 0; done
        return 1
    }
    if pc_ok sdl2 || in_prefix 'sdl2-config.cmake'; then ok "SDL2 development files"
    else bad "SDL2 development files (libsdl2-dev / SDL2-devel, or set CMAKE_PREFIX_PATH)"; fi
    if pc_ok vulkan || [ -f /usr/include/vulkan/vulkan.h ]; then ok "Vulkan headers"
    else bad "Vulkan headers (libvulkan-dev / vulkan-headers)"; fi
    if command -v glslangValidator >/dev/null || in_prefix 'glslang-config.cmake' \
       || [ -f /usr/lib64/cmake/glslang/glslang-config.cmake ] || [ -f /usr/lib/cmake/glslang/glslang-config.cmake ]; then
        ok "glslang"
    else bad "glslang (glslang-dev / glslang-devel, or set CMAKE_PREFIX_PATH)"; fi
    [ -f /usr/include/png.h ] && ok "libpng (optional, start-screen backdrop)" || warn "libpng headers not found (optional)"
    command -v vulkaninfo >/dev/null && vulkaninfo --summary 2>/dev/null | grep -qi "deviceName" \
        && ok "a Vulkan device is present" || warn "could not confirm a Vulkan device (vulkaninfo missing or none found)"
fi

echo "Game files (disc/)"
USR="$ROOT/disc/PS3_GAME/USRDIR"
ELF="$USR/EBOOT.elf"
if [ -f "$ELF" ]; then
    got=$(sha256sum "$ELF" | cut -c1-64)
    [ "$got" = "$EXPECT_SHA" ] && ok "EBOOT.elf is BCES-00129 01.00" \
        || bad "EBOOT.elf has the wrong checksum (other region or version, or not decrypted): $got"
else bad "disc/PS3_GAME/USRDIR/EBOOT.elf (run scripts/prepare_disc.sh)"; fi
if [ -e "$USR/ms2.psarc" ]; then
    sz=$(stat -L -c %s "$USR/ms2.psarc")
    [ "$sz" = "$EXPECT_PSARC" ] && ok "ms2.psarc has the expected size" \
        || bad "ms2.psarc is $sz bytes, expected $EXPECT_PSARC (incomplete, or split pieces not joined)"
else bad "disc/PS3_GAME/USRDIR/ms2.psarc (run scripts/prepare_disc.sh)"; fi
[ -e "$ROOT/disc/PS3_GAME/PARAM.SFO" ] && ok "PARAM.SFO" || bad "disc/PS3_GAME/PARAM.SFO"

echo "Firmware (dev_flash)"
FLASH=""
for d in "${PS3_DEV_FLASH:-}" "$ROOT/dev_flash" "$HOME/.config/rpcs3/dev_flash" \
         "$HOME/.var/app/net.rpcs3.RPCS3/config/rpcs3/dev_flash"; do
    [ -n "$d" ] && [ -f "$d/sys/external/flashMP3.pic" ] && { FLASH="$d"; break; }
done
if [ -n "$FLASH" ]; then
    ok "dev_flash found: $FLASH"
    case "$FLASH" in
        "$HOME/.config/rpcs3/dev_flash"|"$ROOT/dev_flash") ;;
        *) warn "not in a default place: run the game with PS3_DEV_FLASH=\"$FLASH\"" ;;
    esac
else bad "dev_flash (install the PS3 firmware in RPCS3, File > Install Firmware)"; fi

echo "Built"
[ -d "$ROOT/ps3recomp/.git" ] && ok "toolkit (scripts/setup.sh done)" || warn "toolkit not fetched yet: scripts/setup.sh"
[ -f "$ROOT/work/src/recomp/ppu_recomp.h" ] && ok "game lifted (scripts/lift.sh done)" || warn "not lifted yet: scripts/lift.sh"
[ -x "$ROOT/work/port/build/PacificRiftRecomp" ] && ok "port built" || warn "not built yet: scripts/build.sh"

echo
if [ "$fails" -gt 0 ]; then echo "$fails required item(s) missing, $warns warning(s)."; exit 1; fi
echo "Ready ($warns warning(s))."
