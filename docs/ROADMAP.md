# Roadmap

Two goals, in this order. The second builds on the first, because every
improvement in it (more pixels, more detail, more distance, more frames) costs
performance.

1. **Stable performance as the standard.** The default settings give a steady
   game, every time, without tuning.
2. **Enhanced frame rate, resolution and level of detail, including
   far-distance rendering.** Opt-in options on top of a stable base.

Handheld and Android support is a later goal and has its own plan:
[PLAN_HANDHELD_AND_ANDROID.md](PLAN_HANDHELD_AND_ANDROID.md).

**Testing limit:** everything here can only be tested on one desktop Linux PC
(Ryzen 7 5800X, Radeon RX 9070 XT, 31 GB RAM, Vulkan through RADV). Results on
other hardware are unknown. **[known]** marks facts read from the repository's
notes or code; **[assumed]** marks estimates that a step has to check.

## Rules

- **Defaults stay safe.** Every enhancement is an option, off by default, and a
  preset ("Standard", "Enhanced") picks a set of them.
- **Nothing of the game is changed or shipped.** The port reads the user's own
  files. Enhancements change what the *runtime* does (sampler settings,
  resolution, which passes run) or are small hooks in the lifted code, like the
  existing 60 fps timestep hook. Any file generated from the game stays on the
  user's machine.
- **Every change is measured** with the existing tools before and after. A
  change that does not help is dropped.
- **One supported game version** (BCES-00129 01.00): hooks patch fixed addresses.

## Goal 1: stable performance as the standard

### What "stable" means

Concrete targets for a Free-Play race at the default settings (30 fps, 720p).
They are proposals; step 1.1 replaces the guesses with measured numbers.

| Measure | Target |
|---|---|
| Frame rate | 30 fps held; 99th-percentile frame time at or under 40 ms |
| Hitches | no frame over 100 ms during a race (shader compiles, stalls) |
| First run | the first race on an empty shader cache is playable, not a slideshow |
| Soak | two full races, results, back to the menu, repeated, with no crash and no growth in memory |
| Menus and loading | no white screens, no stalls |

### Steps

**1.1 Baseline.** Record how the current build behaves at the defaults, using
the tools already in `work/tools/`: `bench.sh` (fps, pack fps, frame-interval
percentiles, CPU per thread), `fliplog.py` (a timestamp per presented frame),
`finish_race.sh` (the soak) and `tsample` (per-thread CPU). Do it twice: once
with a cold shader cache and once with a warm one, because the difference is
the first-run stutter. Commit the numbers (only numbers, no game data) to
`docs/BASELINE.md`.

**1.2 Fix what the baseline shows.** Known candidates **[known]**:

- the first race's loading screen **whites out over time** (`docs/NOTES.md`,
  *In-race rendering*, still open);
- 60 fps mode **dips in the crash camera's slow motion**, where SPU physics jobs
  fill all 6 job lanes for about 23 ms of a 44 ms frame;
- the race is a **latency chain**: about 20 ms on the main thread, then a wait
  for SPU jobs that run serially, and two or three surface read-backs a frame
  cost about 20% of the RSX thread in fence waits;
- on a slow CPU the intro video decodes at only 1.0 to 1.4 times real time.

Candidates **[assumed]**: shader and pipeline compile hitches on first use
(measure with the cold-cache run); frame pacing jitter in the vblank thread.

**1.3 Make the default the stable one.** If the baseline shows a setting that
is unstable on some machines (job-lane count, the SPURS deviations the port
already makes, thread priorities), pick the safe default and keep the fast one
as an option. Warm the shader and pipeline caches ahead of the race if the cold
run shows hitching.

**1.4 A regression check.** A script, `scripts/perf_baseline.sh`, that runs the
bench and prints one comparable table, so a later change that makes things
worse is seen. It needs the game, so it runs locally, not in CI.

**Goal 1 is done when** the targets above hold on the baseline PC, the numbers
are committed, and the check script exists.

## Goal 2: enhanced frame rate, resolution and detail

All of it is opt-in and runs on top of Goal 1's baseline. Ordered cheapest and
safest first.

### 2.1 Frame rate

- **60 fps already exists** (opt-in; the game's timestep is patched so the speed
  stays correct). Make it as steady as the 30 fps default: the crash-replay dips
  are the known gap.
- **Beyond 60** (90, 120, 144 Hz displays) is research. The simulation advances
  by a whole number of vblanks (the hook turns 2 into 1), so a 1/120 s step is
  not available that way; the options are a new timestep hook or presenting
  extra frames interpolated between simulated ones. Neither is cheap, and
  interpolation needs motion information the port does not have. Treat this as a
  spike with a go or no-go at the end.

### 2.2 Resolution

- **Already there [known]:** render scale 720p, 1440p and 4K, and AMD FSR 1
  upscaling to the window. Checked in the notes on one track at scales 1 to 3.
- **Gaps [known]:** guest-memory round trips (a read-back followed by an
  upload) come back at the game's own resolution and are scaled up with nearest
  filtering, and a shadow lookup with unnormalised coordinates would divide by
  the scaled size (not seen in this game). Fix and test at 1440p and 4K.
- **Better upscaling:** FSR 1 is spatial only. A sharper spatial option
  (a Lanczos or CAS pass, integer scaling) is cheap to try. Temporal upscalers
  (FSR 2 or 3, DLSS) need motion vectors and depth the PS3 game does not
  provide, so they are not planned.

### 2.3 Detail in the renderer (Vulkan backend)

These need no game-logic changes.

- **Anisotropic filtering [known gap].** The Vulkan sampler code does not use
  anisotropy at all. The D3D12 backend in the same toolkit already sets it from
  the game's own texture register (`TEXTURE_CONTROL0`), so the mapping is known.
  Add the game's value, plus a user override up to 16x. The largest visible
  improvement for the least work: ground and road textures at a distance.
- **Texture LOD bias [known gap].** The comment in the Vulkan backend says the
  bias is not applied. Apply it, and offer a negative bias for sharper textures.
- **Anti-aliasing [known].** Every pipeline is single-sample today. Options, from
  cheapest: a post-process pass (FXAA or SMAA) on the final image. True MSAA is
  probably much harder **[assumed]**: the port reads some surfaces back into the
  game's memory, which multisampled surfaces complicate.
- **Shadows.** The game chooses the shadow map sizes; scaling them up may be
  possible in the same place the render scale is applied. **[assumed]**: needs
  checking.
- **Texture filtering quality and mip behaviour** at high render scales.

Each is measured for cost (GPU time and frame time) and shown with before and
after screenshots in the notes.

### 2.4 Far-distance rendering and level of detail

The hardest item, and the one with the least known. In a static recompilation
the **game itself decides** how far it draws and which level of detail it uses:
the port only draws what the game asks for. So pushing the distance out is not a
renderer setting. The path:

1. **Find where it is decided [assumed].** Candidates: numeric tables in the
   game's data files (`ms2.psarc` holds configuration files, for example the
   camera configs the 60 fps work already touches), and constants and distance
   comparisons in the lifted PPU or SPU code. Start by listing the archive's
   configuration files and searching the lifted code for culling and LOD
   comparisons. This is a spike with a clear question: *where is the view or LOD
   distance decided, and can a value be overridden?*
2. **If it is in data,** add an override layer read at load time (a small
   key-and-value file in `user/`, applied as the game reads its own file), never
   a modified or shared copy of a game file.
3. **If it is in code,** add a hook like the existing timestep hook, scaled by an
   option.
4. **Pay for it.** More distance means more draws (already 1000 to 2900 a frame)
   and more SPU work, and it can show pop-in or break assumptions in the game's
   streaming. It only makes sense on top of Goal 1's headroom, and it may not be
   possible without side effects. Define the stop condition up front: if the
   distance is baked into streamed data that cannot be overridden at run time,
   stop and document it.

### Presets

When 2.1 to 2.3 have landed: **Standard** (the stable default) and
**Enhanced** (anisotropic filtering, a higher render scale, FSR or a sharper
upscaler, 60 fps where the machine holds it). Far-distance options are added to
Enhanced only if 2.4 succeeds.

## Order of work

1. **1.1 Baseline** (cold and warm cache).
2. **1.2 and 1.3**, guided by the baseline.
3. **1.4** the regression check.
4. **2.3 anisotropic filtering and LOD bias**: small, visible, measurable. A good
   first enhancement.
5. **2.2 resolution gaps** and a sharper upscaler.
6. **2.1** steady 60 fps, then the beyond-60 spike.
7. **2.4** the far-distance spike, any time after step 2 as a read-only
   investigation.

## Risks

- **The CPU is the limit.** More detail and distance cost CPU as well as GPU; the
  baseline decides how much room there is.
- **Changes alter the picture.** Hence options and a Standard preset that stays
  as the game looks today.
- **Fixed-address hooks** work for this one executable only.
- **One test machine.** A setting that is fine here may not be on weaker or
  different hardware; presets and notes say what was tested.
