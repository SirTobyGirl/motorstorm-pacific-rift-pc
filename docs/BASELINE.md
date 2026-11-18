# Baseline (first pass)

Frame times of a Free-Play race, measured with the port's own benchmark
(`work/tools/bench.sh`), on one machine. This is step 1.1 of [ROADMAP.md](ROADMAP.md): a
first pass, not a finished baseline (see "Limits"). Only numbers are recorded here, never
game data.

**Machine:** AMD Ryzen 7 5800X (8 cores), Radeon RX 9070 XT (RADV, Vulkan), 31 GB RAM,
Bazzite Linux. Built from the `android-experiment` branch, whose differences from
`linux-fork` on Linux are the shader change measured below and Android-only code.

## Results

Each row is the mean of 3 rounds with `BENCH_DUMP_EVERY=60`. A round boots a fresh instance,
drives it into a Free-Play race (Kanaloa Bay, bike), holds the throttle blind from GO and
measures a 10 s window starting 2 s after GO. "Pack" is the 3 s just after GO, with the field in
view and before the bike usually crashes (steadier for comparisons).

| Settings | fps | pack fps | median frame | 90th percentile | main thread | render thread |
|---|---|---|---|---|---|---|
| default: 30 fps, 720p | **29.9** | 29.7 | 33.4 ms | 33.4 ms | 12.7 ms | 17.9 ms |
| 60 fps mode, 1440p (`PS3RECOMP_FPS=60 PS3RECOMP_RES_SCALE=2`) | **58.8** | 59.4 | 16.7 ms | 18.2 ms | 11.8 ms | 10.9 ms |

(Main and render thread are CPU milliseconds per frame, from the benchmark's sampler.)

Reading it:
- **Both modes are steady.** At 30 fps the 90th-percentile frame equals the median (33.4 ms).
  At 60 fps and 1440p the average is 58.8 fps with a 90th percentile of 18.2 ms, barely over
  the 16.7 ms a 60 fps frame lasts.
- **A few slow frames remain at 60 fps**: about 4% of frames are 20 ms or longer (22, 25 and 21
  of about 590 in the three traced rounds), the worst 28 ms. They come in clusters, with no
  fixed rhythm, which looks like race events (collisions, camera changes) and not a cycle.
- Both threads use roughly 11 to 12 ms of CPU per frame in 60 fps mode, against a 16.7 ms
  budget, so there is some headroom on this CPU.

### Correction: an earlier version of this page overstated the judder

The first figures here (60 fps mode: 57.3 fps, **90th percentile 23.8 ms**, "about one frame in
ten is slower than a 60 fps frame"; 30 fps: 90th percentile 37.9 ms) came from the benchmark's
default settings, and they were partly **the benchmark's own overhead**. To read the screen, the
script has the game write a frame dump every 10 frames for the whole run. At 1440p that is a
whole 2560x1440 frame read back from the GPU and written to disk (about 11 MB) every 10th frame,
which shows up as one slow frame every 10 frames: a 169 ms rhythm at 60 fps. The frame tracer
showed it (below); spreading the dumps out (`BENCH_DUMP_EVERY=60`) removed the rhythm.

| 60 fps mode, 1440p | fps | median | 90th percentile |
|---|---|---|---|
| dumps every 10 frames (the old default) | 57.3 | 16.4 ms | 23.8 ms |
| dumps every 60 frames | **58.8** | 16.7 ms | **18.2 ms** |

Lesson: the figures a benchmark reports include whatever the benchmark does to the game. The
default of 10 is kept in `bench.sh` so old numbers stay comparable; use `BENCH_DUMP_EVERY=60` for
steady figures. The shader A/B below was run with the old default; both sides carried the same
overhead, so the comparison between them still holds.

### How it was traced

`FTRACE=/tmp/t ROUNDS=2 work/tools/bench.sh ...` records a 10 s trace per round. In a 60 fps
trace the slow frames (53 of 579 at 20 ms or more) differed from normal ones in one thing: the
game's main thread waited about 7.5 ms at one guest call site (`0x72E5C0`, the wait for the RSX
reference) against 0.06 ms in normal frames, and those waits recurred every 169 ms (10.2
frames) with a spread of only 160 to 188 ms. At 30 fps the same site waits every frame, which is
just frame pacing. A 10-frame rhythm matched the benchmark's `PS3RECOMP_VK_DUMP_EVERY=10`, and
raising it to 60 removed the rhythm and cut the 90th percentile from 24 to 18 ms.
`work/tools/ftrace_slow.py <trace>` runs this comparison.

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
BENCH_DUMP_EVERY=60 ROUNDS=3 INST=b30 work/tools/bench.sh                                          # default settings
BENCH_DUMP_EVERY=60 ROUNDS=3 INST=b60 work/tools/bench.sh PS3RECOMP_FPS=60 PS3RECOMP_RES_SCALE=2   # 60 fps, 1440p
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
- The remaining slow frames (about 4% at 60 fps) have not been traced to a cause; they only look like
  race events. A trace of a window that contains a collision against one that does not would show it.
- Memory use and thread CPU beyond the two figures above were not recorded.


## Cost of anisotropic filtering (2026-11-19)

`PS3RECOMP_ANISO=16` against off, 1440p, 60 fps mode, async recording on, three interleaved rounds each (the machine in
[TESTED_ON.md](TESTED_ON.md)): **58.5 fps** off, **57.1 fps** at 16x (rounds 57.5, 58.9, 59.1 against 56.3, 57.6, 57.4);
median frame 16.2 against 16.6 ms, 90th percentile 23.1 against 24.4 ms. About 2.4%, all on the GPU (the CPU-side
milliseconds per frame did not move). Far-ground sharpness (variance of the Laplacian, 90 captured frames of each):
far band +101%, mid band +31%, near band +1%.
