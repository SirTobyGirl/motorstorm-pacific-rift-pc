# Towards an uncapped frame rate

The goal (ROADMAP, phase 2): a frame rate that is not tied to the PS3's 30 or 60 fps. This page records what was
measured before any work, and the plan. Machine: [TESTED_ON.md](TESTED_ON.md) (Ryzen 7 5800X, RX 9070 XT).

## How the cap works today

- A vblank clock thread (`vblank_clock` in `work/port/main.cpp`) ticks at 59.94 Hz. `PS3RECOMP_VBLANK_HZ` sets any
  other rate.
- The game flips every *n* vblanks (it asks for n = 2: 30 fps). 60 fps mode patches n to 1 (`port_timer_interval`,
  `work/port/port_patches.cpp`).
- The game's simulation advances by a fixed step per flip (1/60 s in 60 fps mode; `port_frame_dt` handles frames that
  miss it). So a faster clock alone makes the game run too fast, not smoother.

## Measured: the engine's ceiling (one round each, 720p, `PS3RECOMP_FPS=60 VK_ASYNC_RECORD=1`, race window)

| vblank clock | frame rate | median frame | 90th percentile | main thread CPU per frame |
|---|---|---|---|---|
| 59.94 Hz (the 60 fps mode) | 58.8 to 59.7 fps | 16.7 ms | 18.2 ms | 11.8 ms |
| 120 Hz | 55.6 fps | 17.7 ms | 20.8 ms | 12.1 ms |
| 240 Hz | 62.4 fps | 15.6 ms | 17.7 ms | 11.4 ms |

(The game ran faster than real time in the 120 and 240 Hz rows; only the frame rate is meaningful.) The engine does
not get past about 63 fps with the clock out of the way. Removing the cap alone would gain about 5%.

## What limits it

The **main game thread** (11 to 12 ms of CPU per frame, plus waiting on job threads). The notes in
[NOTES.md](NOTES.md) (*Performance*) list its parts: the job-counter spin loop (`func_0050C818`), waiting for a job
object's two locks (`func_0094CC80`) and the physics (Havok) tasks. The render-command thread is not the limit on
this PC (about 9 ms with async recording), and on the Android handheld it was ([ANDROID.md](ANDROID.md)).

## Measured: where the main thread's time goes (60 fps, 720p, async recording, 10 s race window)

`work/tools/prof_window.sh` (a perf profile plus a frame trace over the benchmark window). The main game thread is
the busiest thread (about one core). Its profile is **flat**: hundreds of lifted game functions each under 0.3% of all
samples. One stands out: `func_0050C818`, the job-counter spin, is about 12% of the main thread. The 60 fps frame
also needs about 11 ms of CPU on the render-command thread, 9 ms on the recorder thread and 7 to 8 ms on each of three
job threads.

## Tried: compiler-level speed-ups (no gain)

- **ThinLTO** (inlining across the 50 lifted chunks), three interleaved rounds each against the normal build, same
  settings: main thread 11.63 ms against 11.73 ms of CPU per frame, render-command thread 10.17 against 10.73, frame
  rate 58.5 against 58.0 (the frame rate is capped by the 60 Hz clock, so CPU per frame is the sensitive figure).
  Within noise for the main thread: not worth a longer link.
- **Profile-guided optimisation** is not possible with the compiler install here (no profiling runtime, and the profile
  tool needs libraries that are missing). It would also need a profile made from the game's own code, which must not
  be shared, so each user would have to make their own.
- The lifted code is already built `-O3 -march=x86-64-v3` (the CMake build type is Release).

So the main thread's cost is the game's own work plus its waiting, and the gain has to come from doing less of it.

## Measured: what the main thread waits for (trace of the same window, 578 frames)

Of a 17 ms frame the main thread runs about 9 ms and **waits about 6.2 ms**. 4.0 ms of that (3.1 times a frame) is
one place: the game's job-object barrier (`func_0094CC80`) taking two locks, whose lock function
(`func_004B63F0`) spins about 125 times and then calls `usleep(250 us)` and retries. The rest is small: a cellSync
mutex poll shared with the physics tasks (about 1.2 ms in the `func_00A499F8` area) and a few syscalls.

**It is real waiting, not oversleep.** `PS3_USLEEP_SPIN_MAX=300` (spin instead of sleeping, so the main thread wakes
the instant the lock frees) was tried against the default, two interleaved rounds each with the clock at 240 Hz:
60.2 against 60.5 fps (pack 63.3 against 62.7), while main-thread CPU rose from 11.7 to 15.0 ms because it burns the
wait. So the lock is held by job lanes that are still working; the barrier cannot finish sooner than they do.

**More lanes are not possible.** The job lanes are already widened from 5 to 6 (`SPURS_HOST_SPUS=6`, a deliberate
deviation); 6 is the most the game's job-list layout allows.

## Measured: the job lanes (same profile)

The translated job modules (`jobmod_*`) are about **28.7% of all CPU samples**; the SPU DMA emulation
(`memmove`, `mfc_do_transfer`, `mfc_submit`) another 7% or so. One function, in module `67757f5f2148` at SPU address
`0x4F80`, is **6.7% of all samples** on its own (about 1.1% on each of the six lanes): a large vector function that
saves 28 registers to the local store on entry and works with 1.0f constants and byte shuffles (probably an
animation or skinning job). So the frame's latency is the job lanes' work, and the work is real, which makes
**faster translated SPU code and cheaper DMA copies** the lever.

## Next experiments (not done)

- How the translated SPU code is built (`PORT_SPU_OPT` is `-O2`, `-march=x86-64-v3`): try `-O3`, and look at the
  helpers (`spu_rotqby`, the local-store accessors) for byte-shuffle and aliasing costs. Measure on the hot function
  above with a micro-benchmark before rebuilding the whole tree.
- The DMA path (`mfc_do_transfer` plus `memmove`): copy sizes and counts per frame, and whether copies that the
  host could avoid (the data is already in guest memory) are being made.

## Plan, in order

1. **Make a frame cheaper** (compiler options are exhausted, see above): trace the main thread's frame (`FTRACE=<prefix> work/tools/bench.sh`, and
   `work/tools/prof_window.sh` for a CPU profile of the same window) and reduce the job-counter spin and the lock
   waits. Target: main thread well under 8 ms, so the engine can exceed 100 fps.
2. **A faster game clock that keeps the speed right**: a rate setting above 60 (`PS3RECOMP_FPS=120`) that sets the
   vblank clock and the game's frame step (`f1` in `port_frame_dt`, and the frame timer's step) together. The
   60 fps work needed fixes for effects tuned at 30 Hz (camera shake, noise); expect more at higher rates, and
   physics tuned to 1/60 steps (riders were thrown off when steps alternated).
3. **Optionally**, render extra frames between simulation steps (interpolation): the way to a smooth high rate
   without changing the game's physics. A larger piece of work.
4. A **vsync-off / mailbox** present option once the above give frames to show (`VK_PRESENT_MODE_FIFO_KHR` is the
   only mode used today, `rsx_vulkan_backend.c`).
