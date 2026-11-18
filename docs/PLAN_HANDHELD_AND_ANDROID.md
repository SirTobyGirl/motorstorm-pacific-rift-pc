# Plan: Steam Deck, Linux handhelds and Android

> **Status: deferred.** This is a future goal. The current goals are in
> [ROADMAP.md](ROADMAP.md): stable performance as the standard, then enhanced
> frame rate, resolution and detail. Much of what this plan needs (a baseline,
> a measured profile, the runtime options) is built there first.

This is a plan, not a result. Nothing here has been built or run on a handheld
or a phone yet, and this fork can only be tested on one desktop Linux PC today
(Ryzen 7 5800X, Radeon RX 9070 XT, 31 GB RAM). Statements are marked
**[known]** when they come from the repository's own notes or code, and
**[assumed]** when they are an estimate that the first phase must check.

## 1. Goal and ground rules

Run the port on weaker hardware, in two tracks:

- **Track A: Steam Deck and other x86-64 Linux handhelds.** Same code, same
  Vulkan backend. Near term.
- **Track B: Android (arm64).** A different platform, a new build, and a much
  weaker CPU budget. Long term, high risk, with explicit stop criteria.

Rules for both:

1. **Measure before cutting.** The first deliverable of each track is a
   profile, not a feature.
2. **One code base.** No second engine and no fork of the port's logic. Cuts
   are runtime options and presets (`user/settings.ini`), off by default, so the
   desktop build is unchanged.
3. **Nothing of the game is changed or shipped.** The port reads the user's own
   files. "Cutting the game down" therefore means changing what the *runtime*
   does with them (resolution, texture size, which effects run, thread layout),
   or small hooks in the lifted code like the existing 60 fps timestep hook. It
   never means editing or redistributing game data, and nothing generated from
   the game (lifted code, shader caches, an APK containing them) is shared.
4. **Only one game version is supported** (BCES-00129 01.00; the hooks patch
   fixed addresses), so a game-logic hook is cheap to write and fragile to keep.

## 2. What it costs today

**[known]**, from `docs/NOTES.md` and the code:

- The game is **CPU-bound**: PPU and SPU code run on the host CPU. Developed on
  a 16-core Ryzen 9 5950X with an RTX 3090 (the original author's machine; this fork's own
  figures are in BASELINE.md): 30 fps holds, and an opt-in 60 fps
  mode holds 60 at 1440p in a race, dipping in the crash replay.
- **Threads:** the game's 5-SPU SPURS instance is run on **6 host job lanes**
  (`SPURS_HOST_SPUS=6`, a deliberate deviation for speed), plus the PPU main
  thread, the RSX/FIFO thread, a vblank thread, audio (MultiStream) and the
  Bink video decoder. That is roughly 10 busy threads.
- **Race frame budget (older measurement, since improved):** about 20 ms on
  the main thread, then about 30 ms waiting for SPU jobs. In a race the SPURS
  job worker runs about 84% of wall time.
- **Draws:** 1000 to 2900 per race frame. Two or three surface read-backs per
  frame cost about 20% of the RSX thread in fence waits.
- **Intro video:** Bink decode runs on one thread at only 1.0 to 1.4 times real
  time, so a slower CPU makes the intro stutter and the game fall behind.
- **Crash replay:** SPU physics jobs fill all 6 lanes for about 23 ms of a 44 ms
  frame.
- **Guest memory:** a 4 GB *virtual* reservation, committed as used. The real
  resident size has not been measured.
- **arm64 already works:** the author ported the code to Apple Silicon (NEON
  forms of the vector instructions, checked bit for bit). On an M1 Max (8
  performance cores) 30 fps holds at 720p, and 60 fps mode reaches about 50.
  That branch is upstream's `macos-metal`; this fork does not use or test it.
- **Display and tuning knobs that already exist:** render scale 720p/1440p/4K,
  AMD FSR 1 upscaling, 30/60 fps, camera shake off, and many `VK_*`/`SPURS_*`
  environment switches.

**[assumed], to be measured first:** the resident memory of a race; how much
CPU headroom the 5800X really has at 30 fps; the GPU cost at 800p.

## 3. Track A: Steam Deck and x86-64 handhelds

The Deck is x86-64 with AVX2 (4 Zen 2 cores, 8 threads, a 4 to 15 W budget,
RDNA 2 with 8 compute units, 16 GB shared memory, a 1280x800 screen). The
current Linux build should run on it unchanged, so the work is performance.

### A0. Make this PC behave like a Deck (do now; needs no device)

Write `scripts/deck_profile.sh` that starts the game under limits that
approximate the Deck, then record a race with the existing tools
(`work/tools/to_race.sh`, `bench.sh`, `fliplog.py`):

- **CPU:** pin to 4 cores and their SMT siblings (`taskset`, or
  `systemd-run --user -p AllowedCPUs=`), and cap the clock near 3.5 GHz.
  A Zen 3 core is faster per clock than Zen 2, so this is optimistic.
- **Memory:** `systemd-run --user --scope -p MemoryMax=12G` (the Deck's 16 GB is
  shared with the GPU and the system).
- **GPU:** a power or clock cap (LACT is installed), 1280x800 output. This is
  crude: an RX 9070 XT is far stronger than 8 RDNA 2 CUs, so treat the GPU
  numbers as a rough guide only. The CPU side is the one that matters here.
- **Record:** frame-time percentiles, per-thread CPU (`tsample`), resident
  memory, draws per frame.

**Exit:** a table of where a Deck-like budget fails (which thread, which
phase: menus, intro, race start, pack, crash replay). Everything below is
ordered by what this shows.

### A1. Run it on a real Deck

Needs a device. The user already runs Bazzite, which has a Steam Deck edition,
so the setup wizard (with the dependencies in a local prefix) should work as it
does here. Check: build, boot, race, controller (SDL maps the Deck's pad),
suspend and resume, Gamescope. Keep the build private: copy the user's own
build between their own machines, never share it.

### A2. A "Handheld" preset (no engine changes)

A named set of existing options, chosen on the start screen or in
`settings.ini`:

- render at 1280x800 (no scaling) or 960x600 with FSR to the panel;
- 30 fps, camera shake off;
- job lanes matched to cores (6 on 4 cores/8 threads may hurt; test 4, 5, 6),
  `SPURS_*` tuned, big threads kept off the efficiency-style scheduling, SMT
  considered;
- skip or shorten the intro video (the Bink decode is the first thing a slow
  CPU fails), by letting the user skip it.

### A3. Runtime cuts, cheapest first

Each is an opt-in option, with before/after frame times from A0.

| Cut | Layer | Saves | Risk |
|---|---|---|---|
| Lower render scale (below 720p) with FSR | existing | GPU, bandwidth | soft image |
| Skip or lower exposure/luminance read-backs | Vulkan backend | RSX thread stalls (the 20% fence waits) | exposure looks different |
| Skip costly post passes (motion blur) | Vulkan backend | GPU | missing effect, possible artefacts |
| Drop the top texture mips on upload | Vulkan backend | memory, bandwidth | softer textures; formats such as DXT need care |
| Smaller shadow render targets | Vulkan backend | GPU | blurry shadows; the game picks the sizes |
| Fewer draws (more batching or instancing) | Vulkan backend | RSX/CPU | needs profiling; may not be possible |
| Fewer opponents or less traffic, shorter draw distance, fewer particles | lift hook | SPU/PPU | the hardest and most fragile: game logic at fixed addresses |

### A4. Power and heat

Check at the Deck's 15 W and lower: sustained frame times (not just the first
minute), the fan, and the battery life of a race session.

### A5. Packaging for the Deck

Document adding the game as a non-Steam shortcut, a controller layout, and
launch options. Nothing is redistributed.

**Track A done when** a Free-Play race holds 30 fps (99th-percentile frame time
at or under 40 ms) at 800p at 15 W on a real Deck, using only options a player
can switch off. **Stretch:** 40 fps on the Deck's 40 Hz mode would need a
1/40 s simulation step, a new timestep hook like the 60 fps one; a separate item.

## 4. Track B: Android (arm64)

The honest starting point: a phone has a fraction of the CPU budget the game
needs, and the work is a new platform build on top of that. Treat it as
research with stop criteria.

### B0. Feasibility spikes (each independent)

1. **Does it need to be Android first?** Build and run the full port on an
   **arm64 Linux** machine with a Vulkan driver (a single-board computer or an
   arm64 laptop with Linux). It tests the arm64 code, a Vulkan driver that is
   not AMD or NVIDIA, and real arm64 performance, without the Android toolchain.
   Needs a device.
2. **Vulkan feature audit.** List the Vulkan extensions and features the
   backend uses (`ps3recomp/libs/video`) and compare them with what Adreno, Mali
   and Xclipse drivers expose (public device databases). A missing feature is a
   stop sign or a backend rewrite.
3. **Shader translation cost.** The RSX shaders are translated to SPIR-V with
   glslang at run time, which is heavy on a phone CPU. Estimate first-race hitching
   from the PC's shader-cache numbers. Caches are made on the device and never
   shared, because they derive from the game.
4. **Memory.** Take the resident size from A0. Android kills background apps
   aggressively; this probably limits the track to devices with 12 GB or more.
5. **Threads.** About 10 busy threads on a big.LITTLE chip with one prime core.
   Estimate from the arm64 Linux run whether the job lanes can be placed well.

**Stop criterion:** if spike 1 on arm64 hardware cannot reach about 20 fps at
540p in the menus-and-race path with the A3 cuts, Android is not worth building
on the current approach. Say so and stop.

### B1. An Android build (only if B0 passes)

- An NDK CMake toolchain for arm64-v8a, a thin SDL2 Android activity (SDL
  handles the Vulkan surface, input and audio) and a Gradle skeleton. No X11.
- The launcher (ImGui) already takes a gamepad; add touch handling.
- **The lift and compile happen on the user's PC**, because lifting needs the
  decrypted executable and about 16 GB of RAM. The result is a **private APK
  the user builds from their own dump**; it can never be published or attached
  to a release, because it contains the game's translated code. So the
  deliverable is a build tool (`scripts/build_android.sh`, later a wizard step),
  not an app.
- The 13 GB of game data stays on the phone's storage, read through Android's
  file access rules (scoped storage), and decryption happens on the PC.

### B2. Performance

Use the A3 options as the baseline, then add what only a phone needs: thermal
throttling (sustained, not peak, frame times), thread placement on the big
cores, and lower memory use. Flagship phone cores are slower than the Apple
cores the author measured, and they throttle, so expect more cuts than the Deck.

### B3. Input and UX

Touch overlay or a Bluetooth pad, immersive mode, suspend and resume, saves.

**Track B done when** a race runs at 20 to 30 fps on a named reference phone. If
it does not, the result is still useful: the arm64 and Vulkan findings feed
Track A.

## 5. Shared work

- **A preset system** in `settings.ini` and the launcher: Desktop, Handheld,
  Low. Options only, off by default.
- **A performance gate:** record frame-time percentiles per change with the
  existing tools, so a cut that does not help is dropped.
- **Documentation** of each cut: what it changes on screen, and how to turn it
  off.

## 6. Order of work

1. A0: the Deck-like profile on this PC (no device needed).
2. Read the A0 numbers, then pick the first two cuts from A3.
3. A1 and A2 when a Steam Deck is available.
4. B0 spikes 2 to 4 (desk research and the PC), then spike 1 when an arm64 Linux
   device is available.
5. Decide go or stop on Android from B0.

## 7. What is needed from the owner of this fork

- Access to a Steam Deck (or a Bazzite-Deck machine) for A1 to A4.
- For Android: which phone is the reference, and an arm64 Linux board or laptop
  for spike 1 if one can be had.
- A decision on how far to go with game-logic hooks (the last row of the A3
  table): they are the biggest saving and the most fragile.

## 8. Risks

- **The CPU is the limit.** If A0 shows the main thread or the SPU latency chain
  cannot fit a Deck-like budget, resolution cuts will not help, and the only
  fixes are game-logic hooks or lower job-lane counts.
- **Cuts change the picture.** Each one is an option for that reason.
- **Fragile hooks.** Hooks at fixed addresses survive only for this one
  executable.
- **Android may simply not be feasible** on most phones. The plan is built so
  that this is found out early and cheaply.
