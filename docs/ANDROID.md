# Android (experiment)

A private Android build of the port, first run on an **AYN Thor**. It is an
experiment to see how the game uses a phone-class chip, not a supported
platform. This fork is otherwise Linux-only.

> **The APK contains code translated from your own game.** Build it for your
> own device and never publish it or attach it to a release. The game's data is
> not in it; you copy that over yourself.

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
| CPU | 1.6 of 8 cores busy on average; the busiest thread is at about 50% of one core; 546% of 800% of the whole phone idle |
| Frame rate (before the shader fix) | **3.9 fps**, a steady 240 to 285 ms per frame |
| Frame rate (after the shader fix) | **15.7 fps**, median 33.8 ms, 3.4 cores busy |
| Heat | the hottest chip zone reached 95 C; battery 22 C |
| GPU | the load counter read 0% throughout (possibly not meaningful) |

The game is **not CPU-bound at this stage: it is waiting.** One hypothesis was
tested and rejected: the frame time does not change when the vblank semaphore
timeout (`GCM_SEMA_ACQUIRE_MS`) is 250 ms, 50 ms or off. Other candidates, not
yet tested: the slow intro video decode on one thread (the notes say it is only
1.0 to 1.4 times real time on a fast desktop CPU), thread wake-up latency across
the phone's big and little cores, and slow scalar fall-backs in the arm64 SPU
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
   measured the phone's queue: empty submit 0.43 ms, a 720p clear or blit 0.46 ms,
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
Linux or on the phone. This change applies to every platform's shader output, so
a race should be looked at before relying on it.

### Things tried that changed nothing

Run-to-run noise on the phone is about 15%, so only larger effects count.

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
temperatures will make the phone throttle; check it before a long session.

## Profile (simpleperf, 12 s and 8 s samples on the Thor)

> **These profiles came first and, read alone, pointed the wrong way.** The SPU
> work below is real CPU work but it was not what set the frame rate: the frame was
> waiting on the GPU (section above). The "busy-polling thread" is almost certainly
> the game's main thread in its `usleep(1)` spin-wait while it waited for that GPU
> work. The candidate fixes listed at the end of this section turned out not to be
> the first thing to do. They may matter once the GPU side is fixed.

`scripts/android_profile.sh` records and symbolizes this. Two phases:

**Loading (the first minute or so).** One thread takes about half of all CPU
samples, about 89% of a core, and the rest of the phone is idle. Its call chain
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

So the game is not slow because the phone is overloaded: no thread is saturated
and most of the phone is idle. It is slow because the work is serial and the lifted
SPU code is slow per instruction on one core, and (separately) a thread wastes a
core polling. Candidate fixes, in the order I would try them:

1. **Stop the clock thread busy-polling** (sleep to the next vblank with an
   absolute-time sleep). Small and safe; saves heat and battery.
2. **A native inflate for the SPU zlib task** (the port already has a mechanism for
   exact native replacements of lifted SPU functions, `spu_fastpaths.c`). The
   request format has to be understood first. This would also shorten loads on
   Linux.
3. **Make the lifted SPU code faster on arm64** (build flags for the phone's cores,
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
scripts/android_push.sh                 # copy the game and the firmware files to the phone
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
