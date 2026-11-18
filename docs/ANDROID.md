# Android (experiment)

A private Android build of the port, first run on an **AYN Thor**. It is an
experiment to see how the game uses a handheld-class chip, not a supported
platform. This fork is otherwise Linux-only.

> **The APK contains code translated from your own game.** Build it for your
> own device and never publish it or attach it to a release. The game's data is
> not in it; you copy that over yourself.

## Status: paused

Automated work on this stopped in the middle of a supervised session, because the handheld could
not be watched while the game ran it hot. Nothing is running or left on the handheld. The owner has
since played a race by hand (below).
What exists on this branch is complete and pushed; what is unfinished is listed
below under "To resume".

### What the experiment showed

1. **The port runs on a handheld.** Recompiled game code is architecture-independent
   C++; only three small, Android-guarded fixes to the toolkit were needed
   (a context switch for the fiber library, a spin-wait hint, thread affinity), plus
   an SDL launcher and a shared-library target. The game booted, played its intro
   movie and reached its first 3D scene, "Choose Your Driver", at a locked
   **30 fps** (648 draws, CPU about 300 to 430%).
2. **The first big cost was in shader code generation, not in the handheld's speed.**
   The translated shaders declared 96 registers per pixel and a mobile GPU compiler
   kept them in scratch memory: 250 to 290 ms per frame. Using only the registers a
   program needs took the intro from about 4 to about 16 fps. Something similar may
   help any GPU whose compiler is weaker than a desktop one (integrated GPUs, a Steam
   Deck); it has not been tried there.
3. **A CPU profile alone pointed the wrong way.** The sampling profile showed busy
   SPU threads and a "wasted" spinning core and suggested fixes that changed
   nothing. The frame tracer, the backend profiler, a small standalone Vulkan probe
   and A/B switches found the real cause. The order that worked: where does a frame
   wait (tracer), is the platform's queue itself slow (probe), what changes if one
   component is swapped for a trivial one (A/B), then read what that component emits.
4. **Handheld-specific traps** (all fixed or documented here): Android's libc has no
   `makecontext`; a Vulkan swapchain with `preTransform = currentTransform` shows
   sideways on a rotated screen; `adb push` cannot make nested folders under
   `Android/data`; a pad file made by `adb` cannot be emptied by the game; lifted
   chunks need about 7.5 GB each to compile.
5. **Heat is the limit to watch.** About 75 s of play brings the hottest zone to
   94 to 96 C, which is the GPU's first thermal mitigation point on this chip. Timings
   taken at that point are partly throttled.

### To resume

- The pad-file change (`work/port/main.cpp`: the app creates `pad.txt` itself,
  world-writable) compiles but has **not been run on the handheld**. Until it is, scripted
  input repeats one press for ever.
- **In-race rendering is untested** with the shader change, on Linux and on the handheld.
  Look at a race (and compare with `PS3RECOMP_FP_ARRAYS=1`) before relying on it.
- Next likely limits once the GPU side is fixed: the video decoder and the data
  decompressor, each running as translated SPU code on one thread (see Profile below).
- The audio-track stop spam (below) is uninvestigated.
- Use `source scripts/android_session.sh` for any session: it stops the game at 100 C
  and cools the handheld between runs. **Only run it while the handheld can be watched.**

### A race played by hand on the handheld

After the work above the owner played one Free-Play race on the handheld, with the fan at 100%.
Their report: 15 to 20 fps, briefly 30; after about 3 minutes the handheld's own display read 80 C
and they stopped. The game's frame log for that session (425 s, 9,052 frames) says:

| Phase | Time | Frame rate | Median frame |
|---|---|---|---|
| menus | 0 to 76 s | 25 to 32 fps, mostly the locked 30 | 33.3 to 33.4 ms |
| loading the race | 82 to 103 s | 12 to 13 fps, single frames up to 3.5 s | 34 to 52 ms |
| racing, first minute | 103 to 153 s | about 16 fps | 53 to 57 ms |
| racing, the rest | 153 to 425 s | 20 to 24 fps, peaks near 26 to 30 | 40 to 47 ms (90th percentile 55 to 70 ms) |

What this shows:
- **The frame rate did not fall as the handheld heated up.** The last five minutes were the steadiest, at
  about 22 fps. So thermal throttling is not what limits it; something in the race is.
- **Loading the race is its own problem**: 12 to 13 fps with multi-second single frames (pipelines and data
  being built for the first time).
- **Whether the race is limited by the GPU or the CPU is not yet known.** The idea of halving the render
  resolution only helps if it is the GPU. It is also not a setting yet: `PS3RECOMP_RES_SCALE` is an integer
  from 1 to 4 that multiplies the game's native 1280x720, and anything below 1 is clamped to 1. A half-size
  mode means teaching every scaling hook (target sizes, scissors, readbacks, uploads, shadow lookups) to
  divide with rounding. The next step is to record the backend profiler during a race
  (`VK_BE_PROFILE=1`: the share of each frame the render thread spends waiting for the GPU) with
  `scripts/android_watch.sh`, which also stops the game at a set temperature. If most of the frame is waiting
  on the GPU, half resolution is the right lever; if not, the work belongs on the CPU side (how many job
  threads it runs on this chip's mix of fast and slow cores, the video and decompression work).

**The device.** It reports itself as `AYN Thor` (build `Thor_V1.0.0.377_20260206`, platform `kalama`, SoC
QCS8550) and has two screens, one 1080x1920 (the game's) and one 1080x1240.

## What works (tested on one device)

Device: AYN Thor, Snapdragon 8 Gen 2 (QCS8550), Android 13, 11.5 GB RAM,
Adreno 740 (Vulkan 1.3). Built and run once; everything below was seen on it.

- The port compiles for arm64 with the NDK and links with no undefined symbols.
- The APK installs, SDL's Java launcher starts the native code, and the port
  creates a Vulkan device on the Adreno 740, translates shaders with glslang,
  shows its start screen, loads `EBOOT.elf` from the app's storage and runs the
  game's boot and intro (about 1300 frames over six minutes).
- No crash in a few minutes of running.

- After the intro logos it reaches the full MotorStorm: Pacific Rift title
  screen (about three minutes in at the current speed), upright and correctly
  proportioned, with the frame-rate overlay in the corner.

**Not seen yet:** a race, the menus past the title screen, sound, controller
input, or an acceptable frame rate. Nobody has played it on the device.

### "The logo appears and then nothing happens"

The game is not hung, it is slow. The startup screens run for a fixed number of
frames, so at about 4 fps instead of 30 each one lasts about seven times longer
in real time: the logo, then the title screen, then the menus take minutes. The
log and the frame counter keep advancing (660 frames in 150 s), and the process
stays alive.

## How it was measured (first numbers, intro and menus only)

| | |
|---|---|
| Memory | 446 MB resident, 448 MB peak, 63 threads |
| CPU | 1.6 of 8 cores busy on average; the busiest thread is at about 50% of one core; 546% of 800% of the whole handheld idle |
| Frame rate (before the shader fix) | **3.9 fps**, a steady 240 to 285 ms per frame |
| Frame rate (after the shader fix) | **15.7 fps**, median 33.8 ms, 3.4 cores busy |
| Heat | the hottest chip zone reached 95 C; battery 22 C |
| GPU | the load counter read 0% throughout (possibly not meaningful) |

The game is **not CPU-bound at this stage: it is waiting.** One hypothesis was
tested and rejected: the frame time does not change when the vblank semaphore
timeout (`GCM_SEMA_ACQUIRE_MS`) is 250 ms, 50 ms or off. Other candidates, not
yet tested: the slow intro video decode on one thread (the notes say it is only
1.0 to 1.4 times real time on a fast desktop CPU), thread wake-up latency across
the handheld's big and little cores, and slow scalar fall-backs in the arm64 SPU
helpers. A profile (`simpleperf`) is the next step.

## The actual cause: the translated shaders (found and fixed)

The game spent about 250 to 290 ms of every frame waiting for the **GPU**, for a
screen of only 22 draws. The fix takes the intro and title screens from about **4
to about 16 fps** (median frame 33.8 ms, the game's own 30 fps pacing) and gets
the game 82% further through its startup in the same time.

How it was found, in order (each step ruled something out):

1. The game's frame tracer (`PS3_FTRACE`, report: `ps3recomp/tools/ftrace_report.py`)
   showed every PPU thread blocked for the whole frame and the render thread in
   `gpu wait (present)` for **278 of 290 ms**; the `queue present` call itself took 0.2 ms.
2. The backend profiler (`VK_BE_PROFILE=1`): binding targets, pipelines, constants
   and textures cost about 0 ms; `present` was 99% of the wall time.
3. `android/tools/vkprobe.c` (a tiny Vulkan program run through `adb shell`)
   measured the handheld's queue: empty submit 0.43 ms, a 720p clear or blit 0.46 ms,
   a 16 MB copy 1.1 ms. The driver and queue are fast; the game's work is what is slow.
4. Running with no window (no swapchain, compositor or rotation) was no faster
   (3.3 fps), so the display path is not the cause.
5. `PS3RECOMP_VK_GUEST_PROGRAMS=0` (the plain built-in shaders instead of the
   game's translated ones) took it from 4 to **11 fps**. Turning off GPU vertex
   fetch changed nothing.
6. A shader dump (`RSX_ENGINE_SHADER_DUMP`) showed every translated pixel shader
   begins with `float4 r[48]; float4 h[48];` and a loop zeroing all 96 registers,
   while a program uses about **1.2 `r` and 3.2 `h`** of them on average. Desktop
   compilers drop what is unused; the Adreno's compiler keeps per-pixel arrays in
   slow scratch memory (1.5 KB zeroed per pixel, about 1.4 GB of traffic for one
   full-screen draw at 1280x720).

The fix (`ps3recomp/libs/video/rsx_fp_decompiler.c`, `fp_scalarize_registers`):
every access in the body is to a constant index, so each *used* register becomes a
plain variable (`r[3]` to `_r3`) and only those are declared. If anything indexes
the arrays by anything else, the shader is left as it was.
`PS3RECOMP_FP_ARRAYS=1` restores the old arrays.

**Checked:** all 17 pixel shaders from the intro compile with glslang before and
after. On Linux (same binary, new against old) the first 83 captured frames
(logos and start-up screens) are **byte-identical**; the intro video's frames
differ, but the old code differs from itself in the same way (135 of 226 identical
old against old, against 125 of 224 new against old, first difference at about the
same frame), so that is timing noise. **Not checked:** rendering inside a race, on
Linux or on the handheld. This change applies to every platform's shader output, so
a race should be looked at before relying on it.

### Things tried that changed nothing

Run-to-run noise on the handheld is about 15%, so only larger effects count.

| Change | Result |
|---|---|
| spin-waits off (`PS3_USLEEP_SPIN_MAX=0`) | no change |
| spin-waits up to 300 us | no change (more CPU, same fps) |
| clock poll 4 ms (`PS3RECOMP_CLOCK_POLL_US=4000`) | within noise |
| audio off (`SDL_AUDIODRIVER=dummy`) | no change |
| vblank semaphore timeout 250 / 50 ms / off | no change |
| SPU sources at `-O3`, `-march=armv8.2-a+fp16+dotprod`, `-mtune=cortex-a715`, no stack protector | no change |
| CPU vertex fetch (`PS3RECOMP_ENG_GPUFETCH=0`) | no change |

### Heat

With the game running, the hottest chip zone reaches 94 to 96 C within about 75
seconds (it was 64 C with no window). Every test here was therefore kept to about
75 seconds with a cool-down below 60 C before the next. Sustained play at those
temperatures will make the handheld throttle; check it before a long session.

## Profile (simpleperf, 12 s and 8 s samples on the Thor)

> **These profiles came first and, read alone, pointed the wrong way.** The SPU
> work below is real CPU work but it was not what set the frame rate: the frame was
> waiting on the GPU (section above). The "busy-polling thread" is almost certainly
> the game's main thread in its `usleep(1)` spin-wait while it waited for that GPU
> work. The candidate fixes listed at the end of this section turned out not to be
> the first thing to do. They may matter once the GPU side is fixed.

`scripts/android_profile.sh` records and symbolizes this. Two phases:

**Loading (the first minute or so).** One thread takes about half of all CPU
samples, about 89% of a core, and the rest of the handheld is idle. Its call chain
is `spu_pool_thread -> spu_run_lifted_job_abi -> ...` into one 38 KB lifted SPU
program (image `cbf1b1a1`), 98.9% of it in one leaf function. The mix is quadword
byte rotates and shifts, shuffles, loads and stores, with no single slow helper:
a bit-stream decoder. The notes say the game's file loader (FIOS) sends its
decompression requests to an SPU **zlib task**, and this task sits on that queue
(taskset `0x10483480`), so this is very likely the game's data being inflated by
interpreted SPU code on one core. (Inferred from the size, the instruction mix
and the queue; not confirmed against the program's source.)

**Title screen (about three minutes in), per 8 s:**

| Thread | Share of samples | What it is |
|---|---|---|
| 29130 | 30% | the **Bink video decoder** (SPU image `b34579b4`, the one the notes flag as the intro-video bottleneck), about 46% of a core |
| 26660 | 27% | **a busy-polling thread**: 55% in the kernel, 23% in `clock_gettime`, 5% in `sched_yield`; about half a core spent doing nothing useful (likely the frame clock, which the notes say polls every 1 ms) |
| 26725 | 13% | the audio mixer's MultiStream DSP plug-ins |

So the game is not slow because the handheld is overloaded: no thread is saturated
and most of the handheld is idle. It is slow because the work is serial and the lifted
SPU code is slow per instruction on one core, and (separately) a thread wastes a
core polling. Candidate fixes, in the order I would try them:

1. **Stop the clock thread busy-polling** (sleep to the next vblank with an
   absolute-time sleep). Small and safe; saves heat and battery.
2. **A native inflate for the SPU zlib task** (the port already has a mechanism for
   exact native replacements of lifted SPU functions, `spu_fastpaths.c`). The
   request format has to be understood first. This would also shorten loads on
   Linux.
3. **Make the lifted SPU code faster on arm64** (build flags for the handheld's cores,
   fewer round trips through the register file). Helps every SPU task, Bink included.

Not yet known: how much of the frame time each of these buys back.

## Also seen

While the game runs, Android's log shows an audio track being stopped about 45
times a second (about 2,600 a minute). The port itself opens one SDL audio device
once and only queues samples, so the cause is below it (SDL's Android audio
backend, or underruns at 4 fps). Not investigated; a second thing to check once
the frame rate is fixed.

## Building

Needs an Android SDK with an NDK (27 was used), build-tools and a platform, a
JDK 17+, and the lift from `scripts/lift.sh`:

```sh
scripts/build_android.sh --install      # build, package, sign, adb install
scripts/android_push.sh                 # copy the game and the firmware files to the handheld
```

The build needs about 22 GB of RAM at the default of 3 parallel jobs (each
lifted chunk takes about 7.5 GB to compile). Six jobs ran out of memory on a
31 GB machine.

## Running and measuring

The app reads an optional `env.txt` from its storage folder
(`/sdcard/Android/data/org.motorstormpr.rift/files/`), one `KEY=VALUE` per line:

```
PS3_SKIP_START=1                # boot at once instead of waiting on the start screen
PS3RECOMP_FLIPLOG=fliplog.txt   # a timestamp per presented frame
```

Its log is `run.log` in the same folder. To record resource use while you play:

```sh
scripts/android_monitor.sh 180          # samples every 2 s, then summarises
```

## Thor-specific notes

- **Orientation.** The picture first came out rotated 90 degrees. The screen's
  rotation was fine; the swapchain was created with
  `preTransform = currentTransform` (a 90 degree rotation on a landscape
  handheld), which tells Android the app has already rotated its output. The
  backend now declares the picture unrotated on Android and lets the compositor
  rotate it, and treats the resulting `VK_SUBOPTIMAL_KHR` as expected (otherwise
  it would rebuild the swapchain every frame). `currentExtent` already comes in
  landscape on this device; swapping it crushed the picture. The manifest and
  an SDL hint also fix the app to landscape: `sensorLandscape` lets the
  accelerometer pick, which is unreliable on a clamshell. Proper pre-rotation
  (drawing rotated) would save the compositor's rotation pass; not done.

- The Thor has two screens. `adb shell am start` opens apps on the second one
  (Display 4); use `--display 0` for the main screen.
- The screen sleeping pauses the app; wake it first (`input keyevent KEYCODE_WAKEUP`).
- Android 11+ stops `adb push` from creating nested folders under
  `Android/data`, so `android_push.sh` makes folders with `adb shell mkdir -p`
  first and pushes files into them.

## What changed in the code

- Toolkit (`ps3recomp.patch`): an aarch64 context switch for the fiber library
  (Android's libc has no `makecontext`/`swapcontext`), a spin-wait hint that is
  not x86-only, and `sched_setaffinity` in place of `pthread_setaffinity_np`.
  All three are guarded for Android; the Linux build is unchanged (rebuilt, and
  it still boots).
- `work/port/CMakeLists.txt`: on Android the port is a shared library
  (`libmain.so`) that SDL's launcher loads.
- `work/port/main.cpp`: an Android entry point that moves into the app's storage
  folder, keeps stdout in `run.log`, sets the environment `run.sh` would, and
  reads `env.txt`.
- `android/`: the manifest and a one-class launcher. `scripts/build_android.sh`,
  `android_push.sh` and `android_monitor.sh` do the rest.
