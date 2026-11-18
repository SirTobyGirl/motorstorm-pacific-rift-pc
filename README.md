<h1 align="center">MotorStorm: Pacific Rift — native PC port (Linux · Vulkan)</h1>

<p align="center">
  <img src="https://img.shields.io/badge/status-playable-brightgreen" alt="Status: playable">
  <img src="https://img.shields.io/badge/platform-Linux%20x86--64-1793D1?logo=linux&logoColor=white" alt="Linux x86-64">
  <img src="https://img.shields.io/badge/GPU-Vulkan-AC162C?logo=vulkan&logoColor=white" alt="Vulkan">
  <img src="https://img.shields.io/badge/PS3-BCES00129-003791?logo=playstation&logoColor=white" alt="PS3 BCES00129">
  <img src="https://img.shields.io/badge/code-MIT-blue" alt="Code MIT">
</p>

<p align="center"><b>motorstorm-pacific-rift-pc</b> · native on Linux (Windows planned) · PS3 static recompilation · Vulkan · up to 4K · optional 60 fps</p>

<p align="center"><img src="docs/screenshots/cascade-falls-start.jpg" width="900" alt="GO at Cascade Falls: the player's monster truck behind a pack of bikes, ATVs and buggies on a jungle track, race HUD on"></p>

> **About this fork.** A Linux-only fork of
> [millennium-h4x0r/motorstorm-pacific-rift-pc](https://github.com/millennium-h4x0r/motorstorm-pacific-rift-pc).
> The port itself is the original author's; this fork adds setup tooling around it, measurements and notes.
> Start with [docs/FORK_CHANGES.md](docs/FORK_CHANGES.md) (what it adds, and how to take each piece),
> [docs/BASELINE.md](docs/BASELINE.md) (measured frame times: 29.9 fps at the defaults and 58.8 fps at 1440p and
> 60 fps, on the PC described in [docs/TESTED_ON.md](docs/TESTED_ON.md), which also lists what has not been
> tested), [docs/ANDROID.md](docs/ANDROID.md) (an experiment: a private APK that boots the game on an AYN Thor
> handheld; paused) and [docs/CONTINUE.md](docs/CONTINUE.md) (where the work stands and what comes next).

**MotorStorm: Pacific Rift** (PS3, BCES-00129, version 01.00) running natively
on Linux (x86-64), without an emulator. The
game's code, PowerPC (PPU) and SPU, is translated to C ahead of time by static
recompilation with [ps3recomp](https://github.com/sp00nznet/ps3recomp) and
compiled into a normal x86-64 program. The PS3 system is
reimplemented by the toolkit's runtime, and the GPU (RSX) runs the game's own
shaders translated to Vulkan.

<p align="center">
  <img src="docs/screenshots/rain-god-spires-crash.jpg" width="445" alt="A buggy flipping end over end in a crash at Rain God Spires, debris flying">
  <img src="docs/screenshots/cascade-falls-crash.jpg" width="445" alt="A monster truck crashing under a water tower at Cascade Falls, its fender torn off in mid-air">
</p>
<p align="center">
  <img src="docs/screenshots/caldera-ridge-big-rig.jpg" width="445" alt="A big rig tanker truck on the grid at Caldera Ridge during the countdown">
  <img src="docs/screenshots/kanaloa-race.jpg" width="445" alt="Racing a bike along the beach at Kanaloa Bay among palm trees">
</p>
<p align="center"><i>Cascade Falls, Rain God Spires, Caldera Ridge and Kanaloa Bay at 1440p in 60 fps mode (shown here scaled to 1280×720).</i></p>

> [!IMPORTANT]
> **No game files come with this project, and none may be added to it.** To
> play you must own the game and dump it from your own PS3. The build turns
> **your** executable into code on **your** machine; that code, like the game
> data, is the game's and cannot be distributed. This repository holds only
> the port's own code, scripts and a patch to the toolkit.

## Status

| Area | State |
|---|---|
| Lift (39,469 PPU functions; 9 SPU ELFs, the WWS policy module, 31 job modules) and link | ✅ |
| Boot, intro FMV (Bink on SPU), first-run driver select, menus | ✅ |
| Free-Play races: rendering, AI, HUD, Havok physics on SPU, race results | ✅ |
| Audio (MultiStream, 7.1 folded to the output device) | ✅ |
| Game speed: 30 fps, as the game ships | ✅ correct speed, holds 30 fps |
| 60 fps mode (opt-in, game timestep patched, correct speed) | ✅ holds 60 fps in races at 1440p; dips in the slow-motion crash replay |
| Render scale (720p / 1440p / 4K) and upscaling (AMD FSR 1) | ✅ |
| Start screen (Play, settings, saves, game files), in-game menu (F1), performance HUD (F10), settings file | ✅ |
| Pad (SDL2 game controller) | ✅ |
| Saves, other modes and tracks | 🔧 not checked end to end |
| Online play | ❌ not planned (the servers are gone) |
| Windows | 🔜 planned: not built or tested yet |

The status table and the 60 fps note above are the **original author's**, measured on a Ryzen 9 5950X with an
RTX 3090. **This fork's own measurements** are on a Ryzen 7 5800X with a Radeon RX 9070 XT (Mesa RADV): 29.9 fps
at the defaults and **58.8 fps at 1440p in 60 fps mode** (median frame 16.7 ms, 90th percentile 18.2 ms), in a
Free-Play race. See [docs/BASELINE.md](docs/BASELINE.md) for the method and its limits and
[docs/TESTED_ON.md](docs/TESTED_ON.md) for the machine and what has not been tested.

**Known issue:** in 60 fps mode the frame rate dips in the crash camera's slow-motion replay, where the game's SPU
physics jobs fill every job lane (see [docs/NOTES.md](docs/NOTES.md), *Performance*).

Technical notes: [docs/NOTES.md](docs/NOTES.md).

## Requirements

**Game files** (details: [docs/REQUIRED_FILES.md](docs/REQUIRED_FILES.md)):

- Your own dump of **MotorStorm: Pacific Rift, BCES-00129 (Europe), version
  01.00**: the whole `PS3_GAME/` folder.
- Its `EBOOT.BIN` decrypted to `EBOOT.elf`, for example with RPCS3
  (*Utilities → Decrypt PS3 Binaries*). `scripts/lift.sh` checks its SHA-256;
  other regions and versions are not supported (the port patches the game at
  fixed addresses).
- The PS3 firmware installed in RPCS3 (*File → Install Firmware*), which
  provides the `dev_flash` the game reads.

**PC (Linux):**

- Linux x86-64. The lifted code is built for `x86-64-v3` (AVX2/FMA) by
  default; set the CMake cache variables `PORT_PPU_ARCH` and `PORT_SPU_ARCH`
  empty for an older CPU.
- A Vulkan GPU and driver. Developed on NVIDIA by the original author; this fork was tested on AMD with Mesa RADV
  (Radeon RX 9070 XT). Other GPUs and drivers are untested ([docs/TESTED_ON.md](docs/TESTED_ON.md)).
- Plenty of RAM and time for the first build: the lift is tens of large C
  files, and one chunk can take several GB of RAM to compile. `scripts/build.sh` defaults to 14 jobs inside an
  80 GB memory scope; lower `JOBS` (and `PORT_BUILD_MEM`) on a smaller machine. On a 31 GB machine the Linux
  build ran with `JOBS=6 PORT_BUILD_MEM=26G`.
- Tools and libraries: `git`, `rsync`, CMake 3.20+, Ninja, Clang (clang-20 is
  preferred when installed), Python 3.10+, and the development packages of
  SDL2, the Vulkan headers and glslang. On Debian/Ubuntu, for example:

  ```sh
  sudo apt install git rsync cmake ninja-build clang python3 python3-venv \
      libsdl2-dev libvulkan-dev glslang-dev glslang-tools spirv-tools libpng-dev
  ```

  libpng is optional: it draws the start screen over the game's own
  artwork (a plain backdrop without it).

  `ffmpeg` is only needed by the test scripts in `work/tools/`.

## Setup

**The setup program is the only supported way to install.** It is one file, `MotorStormPR-Setup`: you run it,
it opens a page in your browser, and the page takes you from "I have the game" to "it is running", with buttons
and a live log.

1. **Download** `MotorStormPR-Setup` from this repository's
   [Releases](https://github.com/SirTobyGirl/motorstorm-pacific-rift-pc/releases) and make it executable
   (`chmod +x MotorStormPR-Setup`). *No release is published yet: until there is one, build it yourself with
   `scripts/build_gui_exe.sh` (see [docs/DEVELOPING.md](docs/DEVELOPING.md)).*
2. **Run it** (double-click, or from a terminal). It unpacks itself into `~/MotorStormPR`.
3. **Follow the page, top to bottom:** *Check this computer* (it lists anything missing and how to get it),
   *Prepare game files* (your own dump: an `.iso` or a `PS3_GAME` folder), *Install firmware* (through RPCS3),
   then *Do all steps*: it fetches the toolkit, lifts the game's code and builds it, which takes a long while and
   a lot of RAM. Then *Play*.

You supply the game, and the setup program supplies everything else. What it **cannot** do is install system
packages: the first card lists what is missing (a compiler, CMake, git ...) and you install it with your
distribution's package manager. The exception is **SDL2, glslang and the Vulkan headers**: if they are missing, a
*Build SDL2 and glslang here* button builds them from source into the setup folder (a few minutes, nothing
system-wide), and *Do all steps* does it for you. It also needs [RPCS3](https://rpcs3.net) (the page links to it) for
the firmware and to decrypt the game's executable. Nothing of the game or the firmware is included, and no decryption keys are included.

**Setup wizard.** `scripts/setup_gui.py` opens a page in your browser that
does all of this with buttons: it checks the computer, prepares the game from
an ISO or folder, installs the firmware through RPCS3, and runs the toolkit
fetch, the lift and the build with a live log and progress bar, then starts
the game. It drives the scripts above (so they stay the source of truth), uses
only Python's standard library, listens on `127.0.0.1` with a random access
token, and installs no system packages: it tells you what is missing. Linux
only for now. Its build settings (a local libraries folder, the clean build
environment, the job count) are kept in `user/setup_gui.json`.

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

**Updates.** The wizard's *Updates* card checks GitHub when you press *Check
for updates* (it never checks by itself) and offers newer commits of the repo
and branch the executable was built from (`UPDATE_REPO` / `UPDATE_REF` when
building, or the *Update source* fields in Build settings). It only offers a
change that is strictly newer than the files in the workspace, asks before
doing anything, and replaces just the port's scripts and sources: never the
game, saves, settings, lift or build. A file you had changed is copied to
`.update-backup/` first. It then tells you whether to lift or build again. A
git checkout is not touched (use `git pull`).

### Play

```sh
work/run.sh
```

The game opens on its start screen: **Play** (backs up your saves first, then
boots the game), **Settings**, **Saves**, **Game files** (where the game, the
firmware and the saves are, each checked), **Help** and **Quit**. A pad, the
keyboard or the mouse all work there. `work/run.sh --play` (or
`PS3_SKIP_START=1`, or the option under Settings > Gameplay) boots straight
into the game.

`work/run.sh` sets up the environment (`./dev_flash` when it exists, else the
Game files page's folder or RPCS3's copy; `gamedata/` for saves), runs `work/port/build/PacificRiftRecomp` on
`disc/PS3_GAME/USRDIR/EBOOT.elf` and logs to `work/runs/<time>.log`
(`work/runs/latest.log` points at the newest). The game opens a window;
`work/run.sh --offscreen` renders offscreen instead (the test scripts' `INST`
instances are offscreen by default). Arguments of the form `VAR=value` are
exported to the game.

Use an SDL2-supported game controller. The game ignores SIGINT (Ctrl+C); stop
it with SIGKILL, for example `pkill -KILL -x PacificRiftReco`.

**At 1440p and 60 fps.** Either set them on the start screen (Settings: the resolution on the Display page, the
frame rate on the Gameplay page, then Play), or in `user/settings.ini`:

```ini
[video]
scale = 2        ; 1 = 720p, 2 = 1440p, 3 = 4K
fps = 60         ; 30 (original) or 60
```

or for one run only, `work/run.sh PS3RECOMP_FPS=60 PS3RECOMP_RES_SCALE=2`. Both are read at start. **F10** shows the
frame-rate overlay (the resolution reads `(2x)`); **F11** or **Alt+Enter** toggles fullscreen. The game paces
itself at 59.94 Hz and presents with vsync, so a 60 Hz display is the cleanest match.

## Settings

### Start screen, menu and keys

| Key | Action |
|---|---|
| **F1**, the pad's **Home** button (PS, Xbox, Guide), or Select+Start held half a second | The settings in game, with **Quit game** (Escape or Circle/B closes them); the game sees a neutral pad while they are open |
| **F10** | Performance HUD: detailed, compact, off |
| **F11** or **Alt+Enter** | Full screen and back |

Both the start screen and the menu are made for a controller, with the PS3's
button icons shown on screen: D-pad or left stick to move, ✕ to select, ○ to
go back or close, L1/R1 to switch tabs, △ for *Quit game* in game, and the
PS / Home button to open or close the menu. Every connected pad works, and so
do the keyboard (arrows, Enter, Escape) and the mouse.

The settings, on the start screen and in game (F1):

| Page | What is there |
|---|---|
| Display | Full screen, picture shape (16:9 with black bars, or stretched), resolution (720p, 1440p, 4K), FSR 1 upscaling and sharpness |
| Gameplay | 30 or 60 fps, frame-time catch-up (60 fps), camera shake, the performance HUD, skipping the start screen |
| Controller | The controllers found, a live button test |
| Saves | The saves (title, date, size), back up now, backups kept, restore (start screen only), delete; Play backs up first unless turned off |
| Game files | The game folder (EBOOT.elf decrypted and the right version by its checksum, PARAM.SFO, `ms2.psarc` joined), `dev_flash` and the saves folder, each with a folder picker |
| Keys / Help | The keys; the version, the logs and settings folders |

Every change is saved to `user/settings.ini` at once. The upscaler and its
sharpness apply immediately. What is read at start (resolution, frame rate,
picture shape, camera shake, catch-up, folders) applies when Play is pressed on
the start screen, which starts the process again with it; in game the page
offers *Restart now*. Save backups go to `user/backups/`.

The menus use the game's own menu font once the game has unpacked it on its
first run (`disc/framdcn.ttf`), and a system sans (Inter, Noto Sans, DejaVu or
Liberation) for the text.

### user/settings.ini

Read by windowed runs only (`$PS3_USER_DIR/settings.ini` if that is set). An
environment variable that is already set wins over the file.

```ini
[video]
fullscreen = 0
scale = 1            ; 1 = 720p, 2 = 1440p, 3 = 4K render targets (restart)
upscaler = off       ; off, or fsr (AMD FSR 1), to the window's size
fsr_sharpness = 0.80 ; the upscaler's sharpening, 0..1, 1 = sharpest
stretch = 0          ; 0 = keep the 16:9 shape, 1 = fill the window (restart)
fps = 30             ; 30 (original) or 60 (restart)

[game]
camera_shake = 0     ; the game's own camera shake (restart)
catch_up = 0         ; 60 fps mode: a late frame makes up the lost time (restart)

[performance]
fps_counter = 1      ; HUD: 0 = off, 1 = detailed, 2 = compact

[launcher]
skip_start = 0       ; 1 = no start screen
backup_on_play = 1   ; back up the saves each time Play is pressed
keep_backups = 8     ; save backups kept (0 = all)

[paths]              ; empty = the default
game =               ; the folder holding PS3_GAME (default: disc/)
dev_flash =          ; default: ./dev_flash, else RPCS3's
saves =              ; default: gamedata/
```

### Environment variables

The ones a player may want (all `PS3RECOMP_*` switches are read at start). The
names with `VK` in them are the Vulkan backend's.

| Variable | Effect |
|---|---|
| `PS3RECOMP_VK_WINDOW=0` | Render offscreen, same as `--offscreen` (default: a window, which reads `user/settings.ini`) |
| `PS3RECOMP_VK_FULLSCREEN=1` | Start full screen |
| `PS3RECOMP_STRETCH=1` | Stretch the frame to the window (default: keep its shape, black bars) |
| `PS3RECOMP_RES_SCALE=1..4` | Render-target scale: 2 = 1440p, 3 = 4K |
| `PS3RECOMP_FSR=1`, `PS3RECOMP_FSR_SHARPNESS=0..1` | Upscaling to the window's size: AMD FSR 1 |
| `PS3RECOMP_FPS=60` | 60 fps mode at correct game speed while it holds 60 (needs the `patch_lift.py` hooks) |
| `PS3RECOMP_DT_MAX=2` | 60 fps mode: a late frame makes up the lost time (default 1, off: every frame is 1/60 s, so the game runs slightly slow below 60 fps; the catch-up makes the car lurch and riders fall) |
| `PS3RECOMP_CAMERA_SHAKE=1` | Keep the game's camera shake (default: off, a steady camera at both 30 and 60 fps) |
| `PS3RECOMP_SHAKE_60=1` | With camera shake on, 60 fps mode: step it every frame (stronger shake) |
| `PS3_DEV_FLASH=<dir>` | Firmware `dev_flash`, over the Game files page (default: `./dev_flash`, else the page's folder, else RPCS3's: `~/.config/rpcs3/dev_flash`) |
| `PS3_GAMEDATA_ROOT=<dir>` | Where the saves live (`<dir>/dev_hdd0/home/00000001/savedata`), over the Game files page (default `gamedata/`) |
| `PS3_HDD0_ROOT=<dir>` | The PS3 hard drive's game data (run.sh default: `hdd0/`) |
| `PS3_SKIP_START=1` | No start screen (as `work/run.sh --play`); `INST` test runs set it |
| `PS3_UI_PAGE=settings`, `saves`, `game` or `help` | Open the start screen on that page (with `PS3RECOMP_UI_SHOT`, for screenshots) |
| `PS3_UI_AUTOPLAY_MS=<ms>` | Press the start screen's Play by itself after that long |
| `PS3RECOMP_UI_FONT=<file.ttf>`, `PS3RECOMP_UI_TITLE_FONT=<file.ttf>` | The menus' text and title fonts |
| `PS3_USER_DIR=<dir>` | Where `settings.ini` and `shader_cache/` live (default `user/`) |
| `PS3RECOMP_SPIRV_CACHE=<dir>` or `0` | Translated-shader cache (default `user/shader_cache`) or none |
| `PS3RECOMP_FLIPLOG=<file>` | Log a timestamp per presented frame (`work/tools/fliplog.py` reads it) |
| `PS3RECOMP_UI_SHOT=<file.ppm>` | Write the window's frame, menu and HUD included, once (`PS3RECOMP_UI_SHOT_AFTER=<frames>`, default 120) |

The port also changes some toolkit defaults for speed (`work/port/main.cpp`).
Two of them deviate from the PS3 and are the first thing to turn off when
something looks wrong: `SPURS_HOST_SPUS=5 SPURS_MAXCONT_MIN=1` restores
hardware SPU job scheduling. The rest, and the diagnostic switches, are
described in [docs/NOTES.md](docs/NOTES.md).

## Repository layout

| Path | What |
|---|---|
| `work/port/` | The port: `main.cpp` (boot, frame clock, defaults), `port_patches.cpp` (hooks called from the lifted code), `settings.cpp`, `ui.cpp`, `spu_fastpaths.c`, `CMakeLists.txt` |
| `work/patches/ps3recomp.patch` | This port's changes to ps3recomp (regenerate with `scripts/make_patch.sh`) |
| `work/tools/` | Lift helpers, test drivers and profilers ([work/tools/README.md](work/tools/README.md)) |
| `work/run.sh` | Run the game, or isolated test instances (`INST=<name>`) |
| `scripts/` | `setup_gui.py`, `doctor.sh`, `prepare_disc.sh`, `setup.sh`, `lift.sh`, `build.sh`, `make_patch.sh`, `check_clean.sh`, `install-hooks.sh` |
| `docs/` | Technical notes, required files, lessons from the Apocalypse port |

Everything else (the dump, the lift, builds, logs, saves, the toolkit
checkout) is ignored: `.gitignore` is an allowlist.

## Contributing

This fork targets Linux, with Windows possible later. Keep the build and the
scripts free of platform assumptions that would make a Windows port harder
(hard-coded paths, Linux-only tools without a fallback), and say in the commit
what you built and ran it on.

Run `scripts/install-hooks.sh` once. It installs pre-commit and pre-push hooks
that run `scripts/check_clean.sh`, which refuses game file types, lifted code,
binaries, large files and anything that looks like a credential. Toolkit
changes are made in `ps3recomp/` and committed here as the regenerated patch
(`scripts/make_patch.sh`).

## Credits

ps3recomp by Ned Heller (sp00nznet) and contributors; RPCS3 for decrypting
the executable and installing the firmware; AMD FidelityFX Super Resolution 1;
Dear ImGui; SDL2; glslang and Vulkan. Details in
[CREDITS.md](CREDITS.md).

## License

MIT for the code in this repository; see [LICENSE](LICENSE). The toolkit
patch modifies ps3recomp, also MIT. MotorStorm: Pacific Rift and its content
belong to their owners (Evolution Studios / Sony Interactive Entertainment).
This project is not affiliated with or endorsed by them. It is a research and
preservation project.
