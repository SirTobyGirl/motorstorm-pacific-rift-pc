# Developing, and setting up by hand

The supported way to install is the setup program (see the README). This page is for working on the port
itself, and for building the setup program. The setup program drives the same scripts, so they stay the
source of truth and are still in the repository.

## Building the setup program and releasing it

**As one executable** (no release is published yet; you build it). `scripts/build_gui_exe.sh` packs the wizard with
PyInstaller into a single file, `dist/MotorStormPR-Setup`, that needs no
Python and no checkout: double-click it (or run it in a terminal) and it opens
the wizard. It carries this repository's scripts and sources (tracked files
only; nothing of the game) and unpacks them on first run into `~/MotorStormPR`
(`--root DIR` or `MSPR_HOME` to choose another folder; a git checkout is left
alone). The game, the firmware, the lift and the build then live in that
folder. It still cannot install system packages (compiler, CMake, SDL2 ...):
the wizard lists what is missing. The binary needs a glibc as new as the one
it was built on, so build it on the oldest system you want to support (the
release workflow does this on Ubuntu 22.04).

**Releases** (none published yet). `.github/workflows/release-gui.yml` builds the executable on
Ubuntu 22.04 (glibc 2.35, so it runs on that or any newer system), smoke-tests
it (`scripts/smoke_gui_exe.sh`) and, when you push a tag such as `v1.0.0`,
attaches it and its SHA-256 to a GitHub Release. Run by hand from the Actions
tab it only keeps the build as a downloadable artifact. Set the repository
variable `UPDATE_REF` to choose the branch or tag the wizard's updates follow
(default: the repository's default branch).

## Quick start by hand

You need your own **decrypted** dump of the game, RPCS3 and the build tools from the README's *Requirements*:

```sh
scripts/doctor.sh                          # what is missing on this machine, and what is ready
scripts/prepare_disc.sh /path/to/game.iso  # or a PS3_GAME folder: extracts, decrypts EBOOT, makes disc/
# install the PS3 firmware in RPCS3 (File > Install Firmware), or put its dev_flash in ./dev_flash
scripts/build_libs.sh                       # only if SDL2, glslang or the Vulkan headers are missing -> ./libs
export CMAKE_PREFIX_PATH=$PWD/libs         # (when you built them)
scripts/setup.sh && scripts/lift.sh && scripts/build.sh
work/run.sh
```

A compiler or sysroot exported in your shell (`CC`, `CXX`, `LDFLAGS`, `CMAKE_SYSROOT`) can break the build:
`scripts/doctor.sh` warns about it. The steps in detail:

### 1. Get the code

```sh
git clone https://github.com/millennium-h4x0r/motorstorm-pacific-rift-pc.git motorstorm_pacific
cd motorstorm_pacific
scripts/install-hooks.sh    # optional: blocks commits of game files
```

### 2. Put the game in place

`disc/` is git-ignored. The port expects:

```
disc/PS3_GAME/            PARAM.SFO, ICON0.PNG, ... (links into your dump are fine)
disc/PS3_GAME/USRDIR/     the dump's USRDIR files, plus:
    EBOOT.elf             your decrypted executable
    ms2.psarc             one file; join a FAT32 dump's ms2.psarc.66600..66604 pieces in order
```

[docs/REQUIRED_FILES.md](docs/REQUIRED_FILES.md) has the exact commands
(symlinks for everything, a copy of `EBOOT.elf`, and the `cat` that joins
`ms2.psarc`), the expected checksum, and where the firmware's `dev_flash` is
looked for. Files prepared on another machine can simply be copied over.

### 3. Get and patch the toolkit

```sh
scripts/setup.sh
```

This clones ps3recomp into `ps3recomp/` (its own git repository, ignored by
this one), checks out the pinned commit (v0.18.0, `4e8f3ca8`), applies this
port's changes from `work/patches/ps3recomp.patch`, clones Dear ImGui v1.91.5
for the menu, and installs the toolkit's Python tools into `.venv/`. Set
`PS3RECOMP_DIR=<dir>` to keep the toolkit elsewhere (every script honours it).

### 4. Lift your executable

```sh
scripts/lift.sh                 # or: scripts/lift.sh path/to/EBOOT.elf
```

This translates the PPU code into `work/src/recomp/` and the SPU images into
`work/port/spu/` (`work/tools/lift_spu.sh`). It then runs
`work/tools/patch_lift.py`, which splices the port's hooks into the lifted
code: the 60 fps timestep, the frame-time compensation and the camera-shake
hooks. Both directories are git-ignored and must never be shared. After
updating this repository, run `scripts/lift.sh` again, or at least
`.venv/bin/python work/tools/patch_lift.py` (it is idempotent and fails
loudly if the lifted code is not what it expects).

### 5. Build

```sh
scripts/build.sh                # -> work/port/build/PacificRiftRecomp
```

This configures `work/port/CMakeLists.txt` with Ninja, a Release build and the
Vulkan backend, and builds it. It accepts `JOBS=<n>`, `PORT_BUILD_MEM=<size>`,
`BUILD_DIR=<dir>`, `SPU_DIR=<dir>`, `CC=`/`CXX=`. To run CMake yourself:

```sh
cmake -S work/port -B work/port/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPS3RECOMP_DIR="$PWD/ps3recomp" -DRECOMP_DIR="$PWD/work/src/recomp" \
    -DSPU_DIR="$PWD/work/port/spu" -DPS3RECOMP_RSX_VULKAN=ON \
    -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build work/port/build
```

### Immutable distros (Bazzite, Fedora Atomic, SteamOS)

The packages in *Requirements* cannot always be installed system-wide. Two
things that worked on Bazzite:

- **Build SDL2 and glslang into your home** and point CMake at them (SDL2
  2.32.x and glslang 15.x were used; SDL2 needs a clean toolchain, see below):

  ```sh
  cmake -S SDL -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=$HOME/.local/ps3deps -DSDL_TEST=OFF
  cmake --build build && cmake --install build      # likewise for glslang (-DENABLE_OPT=OFF -DBUILD_EXTERNAL=OFF)
  export CMAKE_PREFIX_PATH=$HOME/.local/ps3deps
  export PKG_CONFIG_PATH=$HOME/.local/ps3deps/lib64/pkgconfig
  export LD_LIBRARY_PATH=$HOME/.local/ps3deps/lib64        # when running
  ```

- **Build with a clean environment.** A compiler or sysroot exported in the
  shell (`CC`, `CXX`, `LDFLAGS`, `CMAKE_SYSROOT`, as some SDK or toolchain
  setups do) is picked up by CMake and can fail with link errors such as an
  undefined `__isoc23_strtol`. `scripts/doctor.sh` warns when they are set;
  build with `env -u CC -u CXX -u LDFLAGS -u CMAKE_SYSROOT scripts/build.sh`.

- **RPCS3 as a Flatpak** (`net.rpcs3.RPCS3`) keeps its firmware under
  `~/.var/app/net.rpcs3.RPCS3/config/rpcs3/dev_flash`. Run the game with
  `PS3_DEV_FLASH` set to that folder if `work/run.sh` does not find it.

