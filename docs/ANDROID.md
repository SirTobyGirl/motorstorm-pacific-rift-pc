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

**Not seen yet:** a race, the menus past the intro, sound, controller input, or
an acceptable frame rate. Nobody has played it on the device.

## How it was measured (first numbers, intro and menus only)

| | |
|---|---|
| Memory | 446 MB resident, 448 MB peak, 63 threads |
| CPU | 1.6 of 8 cores busy on average; the busiest thread is at about 50% of one core; 546% of 800% of the whole phone idle |
| Frame rate | **3.9 fps**, a steady 240 to 285 ms per frame |
| Heat | the hottest chip zone reached 95 C; battery 22 C |
| GPU | the load counter read 0% throughout (possibly not meaningful) |

The game is **not CPU-bound at this stage: it is waiting.** One hypothesis was
tested and rejected: the frame time does not change when the vblank semaphore
timeout (`GCM_SEMA_ACQUIRE_MS`) is 250 ms, 50 ms or off. Other candidates, not
yet tested: the slow intro video decode on one thread (the notes say it is only
1.0 to 1.4 times real time on a fast desktop CPU), thread wake-up latency across
the phone's big and little cores, and slow scalar fall-backs in the arm64 SPU
helpers. A profile (`simpleperf`) is the next step.

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
