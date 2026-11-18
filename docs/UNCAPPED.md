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

## Plan, in order

1. **Make a frame cheaper**: trace the main thread's frame (`FTRACE=<prefix> work/tools/bench.sh`, and
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
