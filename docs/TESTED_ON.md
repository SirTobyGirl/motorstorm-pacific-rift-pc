# What this has been tested on

Two machines, and only those. Anything not listed here has **not** been tried, and results on other
hardware are unknown. Game: MotorStorm: Pacific Rift, BCES-00129 (Europe) 01.00, from the owner's own dump.

## The PC (the Linux build, the setup tooling, the benchmarks)

| | |
|---|---|
| CPU | AMD Ryzen 7 5800X, 8 cores / 16 threads, 32 MB L3 |
| RAM | 32 GB (31 GiB usable) |
| GPU | AMD Radeon RX 9070 XT, Mesa 26.2.4 RADV, Vulkan 1.4.354 |
| OS | Bazzite (an immutable Fedora-based distribution), kernel 7.2.4-ogc3.1.fc44; the build and the lift run in a Fedora 44 Toolbox container |
| Desktop | KDE Plasma on Wayland |

Used for:
- Building, lifting and playing the game (30 fps at 720p, and 60 fps at 1440p).
- The frame-time benchmark ([BASELINE.md](BASELINE.md)): 29.9 fps at the defaults, 58.8 fps at 1440p and 60 fps.
- The setup scripts, the browser setup wizard's server (endpoints, token and Host checks, a real job), the
  packaged executable (built locally and by GitHub Actions on Ubuntu 22.04), the update check against GitHub,
  and the release workflow (one real run on a test tag).

## The Android handheld (the Android experiment)

| | |
|---|---|
| Device | AYN Thor. It reports `AYN Thor`, build `Thor_V1.0.0.377_20260206`, platform `kalama`, and has two screens (1080x1920 for the game, 1080x1240 second) |
| SoC | Qualcomm QCS8550 (Snapdragon 8 Gen 2): 1 core at 3.19 GHz, 4 at 2.8 GHz, 3 at 2.0 GHz |
| RAM | 11.5 GB (plus 4 GB swap) |
| GPU | Adreno 740, Vulkan 1.3.128 |
| OS | Android 13 (API 33), 4 KB pages |

Used for: building and installing a private arm64 APK; the game booting, playing its intro movie and
reaching its first 3D scene at a locked 30 fps; a race played by hand (16 to 24 fps); profiling that found a
GPU stall in the translated shaders. Findings: [ANDROID.md](ANDROID.md).

## What has not been tested

- **Other GPUs and drivers**: NVIDIA, Intel, Mesa versions other than the one above, the Steam Deck or other
  AMD integrated graphics. The Adreno shader fix is neutral on the PC's discrete AMD GPU; its effect elsewhere
  is unknown.
- **Other distributions and desktops**, X11, other CPUs, machines with less than 16 GB of RAM (each lifted
  chunk takes about 7.5 GB to compile).
- **Windows and macOS** (this fork is Linux-only by choice).
- **Other tracks and vehicles**: the benchmark plays one track (Kanaloa Bay) on one bike.
- **The setup wizard in a browser** (only its server was tested), and the wizard's firmware step.
- **Android**: any device other than the one above; a full race recorded with the profiler; sound; touch.
- **Rendering inside a race on the handheld** with the shader fix beyond what was seen by eye.

## How the figures were taken

The benchmark plays a Free-Play race by itself and times a 10 s window after GO, three rounds. It includes
its own overhead: its default frame dumps (one every 10 frames) made every 10th frame slow at 60 fps until
that was found and made configurable (`BENCH_DUMP_EVERY`). See BASELINE.md. Handheld figures come from the
game's own per-frame log and a sampling profiler over `adb`; temperatures are the handheld's thermal zones.
