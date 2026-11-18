# Baseline (first pass)

Frame times of a Free-Play race, measured with the port's own benchmark
(`work/tools/bench.sh`), on one machine. This is step 1.1 of [ROADMAP.md](ROADMAP.md): a
first pass, not a finished baseline (see "Limits"). Only numbers are recorded here, never
game data.

**Machine:** AMD Ryzen 7 5800X (8 cores), Radeon RX 9070 XT (RADV, Vulkan), 31 GB RAM,
Bazzite Linux. Built from the `android-experiment` branch, whose differences from
`linux-fork` on Linux are the shader change measured below and Android-only code.

## Results

Each row is the mean of 3 rounds. A round boots a fresh instance, drives it into a
Free-Play race (Kanaloa Bay, bike), holds the throttle blind from GO and measures a
10 s window starting 2 s after GO. "Pack" is the 3 s just after GO, with the field in
view and before the bike usually crashes (steadier for comparisons).

| Settings | fps | pack fps | median frame | 90th percentile | main thread | render thread |
|---|---|---|---|---|---|---|
| default: 30 fps, 720p | **29.9** | 29.7 | 33.4 ms | 37.9 ms | 12.1 ms | 16.8 ms |
| 60 fps mode, 1440p (`PS3RECOMP_FPS=60 PS3RECOMP_RES_SCALE=2`) | **57.3** | 56.8 | 16.4 ms | 23.8 ms | 11.7 ms | 10.6 ms |

(Main and render thread are CPU milliseconds per frame, from the benchmark's sampler.)

Reading it:
- **30 fps is steady**: median exactly 33.4 ms, 90th percentile 37.9 ms.
- **60 fps mode at 1440p holds 57 on average** with a median frame of 16.4 ms (60 fps
  pacing), but the 90th percentile is 23.8 ms: about one frame in ten takes longer than
  a 60 fps frame. That judder, not the average, is what the first roadmap goal is about.
- Both threads use roughly 11 to 12 ms of CPU per frame in 60 fps mode, against a 16.7 ms
  budget, so there is some headroom on this CPU.

## The shader change, checked in a race

The Android work added a change that stops each translated pixel shader declaring 96
registers (`fp_scalarize_registers`; `PS3RECOMP_FP_ARRAYS=1` restores the old arrays). Same
binary, 60 fps / 1440p, 3 rounds each:

| | fps | pack fps | median | 90th percentile |
|---|---|---|---|---|
| new (registers as plain variables) | 57.3 | 56.8 | 16.4 ms | 23.8 ms |
| old (`PS3RECOMP_FP_ARRAYS=1`) | 56.6 | 56.8 | 16.5 ms | 24.2 ms |

**No difference on this desktop GPU**, as expected: desktop compilers already drop unused
registers. Race frames from the two runs show the same scene type, lighting, foliage and
HUD; they are different moments of the race, so this is a visual check, not a pixel
comparison. (On the earlier intro-screen frames, new and old were byte-identical.) The
change matters on mobile GPUs; see [ANDROID.md](ANDROID.md) on the `android-experiment`
branch.

## How to repeat

`ffmpeg` is needed to read the screen. Without a system one, a user-level copy:

```sh
python3 -m venv ~/.local/share/ffmpeg-venv && ~/.local/share/ffmpeg-venv/bin/pip install imageio-ffmpeg
ln -sfn "$(~/.local/share/ffmpeg-venv/bin/python -c 'import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())')" ~/.local/bin/ffmpeg
```

Then, with the dependencies on the path as for a normal build:

```sh
ROUNDS=3 INST=b30 work/tools/bench.sh                                          # default settings
ROUNDS=3 INST=b60 work/tools/bench.sh PS3RECOMP_FPS=60 PS3RECOMP_RES_SCALE=2   # 60 fps, 1440p
```

Each prints one line per round and a mean. The benchmark takes about 5 minutes a round and
uses the machine heavily. A trailing `ffmpeg ... Invalid data` message is a screenshot
conversion catching a half-written frame and is harmless.

## Limits

- **One machine, one track, one vehicle, 3 rounds each.** Round-to-round spread was about
  2 to 3 fps in 60 fps mode (56.0, 58.8 and 57.2).
- **The window is short (10 s) and the bike is driven blind**, so where it crashes moves
  a round by a few fps; "pack" is the steadier figure.
- **Cold against warm shader cache is not separated yet.** Round 1 of each new instance
  starts with an empty cache and was the lowest in both 60 fps runs (56.0 and 55.3, against
  58.8 and 58.3 for round 2), which fits a first-run cost, but that is three samples.
- The known 60 fps dips in the crash replay (notes) are not part of this window.
- Memory use and thread CPU beyond the two figures above were not recorded.
