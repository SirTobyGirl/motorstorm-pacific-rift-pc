# Render distance and level of detail: where the game decides (investigation, 2026-11-19)

The roadmap's step 2.4 asked one question: *where is the view or level-of-detail distance decided, and can a value be
overridden?* This is the read-only answer. Nothing was changed in the game or the port yet. No game data is copied
here: only the names of settings and what they do, because the data is the owner's own dump.

Tool: `work/tools/psarc.py` lists and extracts files from your own `ms2.psarc` (5,004 files).

## Where it is decided: data, in one file

`/scripts/viewportquality.xml` inside `ms2.psarc` (about 5 KB) is the game's own quality table. It has three tiers,
chosen by the number of viewports (1, 2 or 4 players), and per tier:

- **General**: motion blur on or off, a *z-pass cull threshold*, a *field-of-view scale*.
- **Variable viewport scaling**: the game has its own **dynamic resolution** scaler (a minimum scale, and frame-time
  figures that drive it). Whether it is active in the port is **not known** (see the open questions).
- **Particles**: LOD scale range and emission rates.
- **Shadows**: a *shadow fade distance modifier*, the *number of shadow splits* (cascades) and a *shadow map scale*.
- **Per object group** (`object`, `dynamic_objects`, `world`, `vehicle`, `player`): a **`lodBias`**, a
  **`minimumLod`**, a **`lodOverride`**, a **`cullThreshold`** (apparently the on-screen size below which an object is
  not drawn) and a **`shadowCullThreshold`**.

So the distance at which things are simplified or dropped is a handful of numbers per group, not baked into the code.

## How the game reads it, and how a value could be overridden

- The parser is `func_00360DD4` (with an alternate entry `func_00360DD8`). It reads each attribute by name and stores
  it as a float in a small structure per group (the LOD bias at offset +0, the cull threshold at +0xC, the shadow cull
  threshold at +0x10). The setting names are in one string table in the executable; each has one table slot, reached
  through the usual TOC register.
- Other functions use the names too (`func_006ED210`, `func_006EE154`, `func_006EE8FC`, `func_006EF080`,
  `func_00A8D178`, `func_00AD8DF8`): probably a debug or tuning screen and the code that applies the values.
- **A loose file on disk will not override it.** A file-system trace of a whole race shows the game opens
  `ms2.psarc` once and reads its data through the archive layer; it only probes the disk for a few files (such as the
  race definition) and falls back to the archive. The quality file is not probed.
- **The way to change a value is the one the 60 fps work already uses**: a small hook spliced into the lifted code by
  `work/tools/patch_lift.py`, after those float stores, that adds an offset to the LOD bias or scales the cull
  threshold, driven by options. It needs a re-lift and a rebuild (about 30 to 40 minutes), and it must be an option
  that is off by default.

## What it probably buys, and what it costs

- A **lower LOD bias for the `world` group** should keep finer geometry out to a greater distance; a **smaller cull
  threshold** should keep smaller, farther objects (rocks, plants, props) in the picture; a longer **shadow fade
  distance** should keep shadows farther. These are guesses from the names: nothing has been changed yet to see.
- **Cost**: more objects and more detail mean more draws (already 1,000 to 2,900 a frame) and more job-thread and main-
  thread work, and the main thread is already the limit in 60 fps mode ([UNCAPPED.md](UNCAPPED.md)). In **30 fps mode the
  main thread uses about 12 ms of a 33 ms frame**, so there is headroom there: far detail at 30 fps is the likelier fit.
- **Pop-in**: pushing detail out can expose objects appearing at a distance that the original tuning hid.

## Open questions

1. **Is the distance also limited by baked visibility?** Every track has a `.spvs` file (tens of KB). The name looks
   like a *static potentially visible set*: precomputed "what can be seen from here". If objects are culled by it, a
   cull-threshold change would not bring them back, and the data would have to be regenerated, which is probably not
   possible. Test it: change the cull threshold and see whether distant objects appear.
2. **Per-model LOD distances** may live in the model resource packs (`.rpk`), outside this file.
3. **Is the game's dynamic resolution scaler running in the port?** It would lower the game's own render size under
   load. A short test: log the viewport size it asks for per frame.

## Built and measured (2026-11-19): `detail` / `PS3RECOMP_LOD_OFFSET`, `_CULL_SCALE`, `_SHADOW_DIST`

`work/tools/patch_lift.py` splices `port_vq_float` (`work/port/port_patches.cpp`) after the four stores in both copies of the
parser (a new per-function patch mode, since the stores' text also occurs in the alternate entry). It adjusts each value
just parsed, for every group and every viewport tier; all three options default to no change. `settings.ini` has
`detail = 0, 1 or 2` (the menu's *Distance detail*): enhanced is LOD offset -1, cull x0.5, shadows x1.5; maximum is -2,
x0.3, x2. Either also turns on asynchronous recording. Applying the hook to an already lifted tree needs only
`python3 work/tools/patch_lift.py` and a rebuild (no re-lift): about 5 minutes.

**What it shows** (still moment at the start of a race, 1440p, each setting alone against the default):
- **Shadow distance x2.5**: the largest change; shadows reach much farther (the sand around the bike, the slope).
- **Cull threshold x0.1**: smaller objects stay in the picture farther out: more foliage detail across the mid-distance
  and the horizon. So the baked visibility sets (open question 1) do **not** stop those small objects: it works.
- **LOD offset -3**: subtle: finer plants at mid-distance.
Judged from difference images (the race timer differs because the clock moved) and by eye; no measurable change in the
large geometry. The effect is modest, not a different horizon.

**Cost** (30 fps mode, 1440p, one round each; render-thread CPU per frame):

| Setting | fps | render thread | main thread |
|---|---|---|---|
| default | 29.9 | 17.3 ms | 11.8 ms |
| LOD offset -2 | 29.9 | 17.0 ms | 11.1 ms |
| shadow x2 | 29.9 | 23.1 ms | 12.2 ms |
| cull x0.3 | 29.8 | 28.2 ms | 13.7 ms |
| all three (maximum) | **24.2** | 40.2 ms | 16.0 ms |
| all three, async recording | **29.9** (90th percentile 33.4 ms) | 13.5 ms + 12.7 ms recorder | 14.1 ms |
| enhanced-like (-2, x0.5, x2), async | 29.8 (90th percentile 33.4 ms) | 12.6 ms + 11.8 ms recorder | 13.3 ms |

The detail settings roughly double or triple the draw calls per frame (about 3,500 to 10,600 with the strongest); at 30 fps
the PC has the headroom once asynchronous recording splits the render thread, at 60 fps it does not (the main thread is
already the limit, [UNCAPPED.md](UNCAPPED.md)). The LOD offset alone is free.

## Still open

- **Per-model LOD distances** in the model packs (`.rpk`) may limit large geometry regardless of this file.
- **The game's dynamic resolution scaler** (`ViewportScaling` in the same file): is it running in the port? Not checked.
- **Pop-in** was not looked for (needs motion): watch for objects appearing at a distance with the detail settings on.
- Group-specific tuning (for example only the `world` group) is possible, since the structure address identifies the group;
  not done: the options apply to every group.
