# Continue where we left off

A handoff note: where this project stands, and what to do next. Written 2026-11-18. Read it first,
then [ROADMAP.md](ROADMAP.md) (the goals), [BASELINE.md](BASELINE.md) (the numbers) and
[FORK_CHANGES.md](FORK_CHANGES.md) (the branches).

## The goal

A native Linux build of MotorStorm: Pacific Rift (this is a fork of
millennium-h4x0r/motorstorm-pacific-rift-pc, kept Linux-only because Linux is all that can be tested).
In order: **stable performance as the standard**, then **better frame rate, resolution and detail,
including far-distance rendering**. Handhelds and Android are a later goal.

## Where it stands

**Done and working**
- The game builds and plays on Linux from the owner's own dump (BCES-00129 01.00). Settings are
  already at **1440p and 60 fps** (`user/settings.ini`: `scale = 2`, `fps = 60`); run `work/run.sh`.
- Setup tooling on this fork, tested: `scripts/doctor.sh`, `prepare_disc.sh`, a browser setup wizard
  (`scripts/setup_gui.py`), a packaged executable, update-from-GitHub, and a release workflow (run for
  real in GitHub Actions). See FORK_CHANGES.md for the five layers and how to take each.
- **Baseline measured** (docs/BASELINE.md): 29.9 fps at the defaults (steady, 90th-percentile frame equals
  the median); 58.8 fps at 1440p/60 fps (median 16.7 ms, 90th percentile 18.2 ms). The 60 fps "judder"
  first reported was the benchmark's own frame dumps; fixed with `BENCH_DUMP_EVERY=60`.
- An Android experiment (branch `android-experiment`, paused for automated runs): a private APK boots the game
  on an AYN Thor handheld (the device reports `AYN Thor` and has two screens; the owner calls it an Odin) to
  its first 3D scene at a locked 30 fps, and found a shader inefficiency. The owner then played a race by
  hand: about 16 fps for the first minute, then 20 to 24, with the fan at 100%; the frame rate did not fall as
  it heated up. Details in [ANDROID.md](ANDROID.md); the machines everything was tested on are in
  [TESTED_ON.md](TESTED_ON.md).

**Setup program (paused 2026-11-18, mid-way)**: the README now documents only the packaged setup program.
Added and pushed: a "Build SDL2 and glslang here" step (`scripts/build_libs.sh`, tested from scratch), RPCS3
detection with a "point me to it" field (`scripts/find_rpcs3.sh`), and a dump inspector that says what the game
files need before preparing (`scripts/inspect_disc.sh`) plus reuse of an earlier ISO extraction. Next, in order:
(1) open the page in a browser and click through every card (its script has never run: no JavaScript engine on
the machine), (2) a full clean run from the executable in an empty folder, (3) publish a first release (the owner
must say yes), (4) the decrypt step through the finder on a real dump.

**Not done**
- Nothing yet improves what you *see* on Linux. No quality improvement has been started.
- The wizard and executable have never been viewed in a browser; the wizard's firmware step is unverified.
- Cold-versus-warm shader cache is not separated in the baseline; about 4% of 60 fps frames are still
  slow (clustered, cause not traced).
- On the Android branch: the pad-file fix has never run on the handheld, and no race has been recorded with
  the profiler. That recording (`scripts/android_watch.sh`, with `VK_BE_PROFILE=1` in the app's `env.txt`) is
  the next Android step: it shows whether a race is limited by the GPU, which decides whether rendering at
  half resolution is the right lever. It is not a setting yet (`PS3RECOMP_RES_SCALE` clamps at 1).

## Also in flight: uncapped frame rate

The owner wants an uncapped frame rate. The first measurement (the engine tops out near 63 fps whatever the clock) and
the plan are in [UNCAPPED.md](UNCAPPED.md); the first real work is making the main thread's frame cheaper.

## Done: anisotropic filtering (2026-11-19)

Added to the Vulkan engine sampler (`vk_eng_sampler`): the game's own request (`TEXTURE_CONTROL0` bits [6:4], decoded in
`rsx_draw_engine.c` into the new `max_aniso` field) is honoured, and `PS3RECOMP_ANISO` / `aniso` in `settings.ini` / the
menu's *Texture filtering* sets a minimum of 2, 4, 8 or 16 for every mip-mapped, linearly filtered texture. The
`samplerAnisotropy` device feature is enabled when present. `PS3RECOMP_LOD_BIAS` adds a bias offset. Measured at 1440p
(captured race frames): far-ground sharpness +101%, mid ground +31%, near ground unchanged (+1%, the control); cost
about 2.4% (58.5 to 57.1 fps, three interleaved rounds, 60 fps mode), all GPU. Not done: applying the game's own LOD
bias, an LOD-bias menu entry, and checking what anisotropy the game itself requests (it may request none).

Far distance is built as an option ([RENDER_DISTANCE.md](RENDER_DISTANCE.md)): `detail = 1 or 2` / three environment variables adjust the game's
own LOD bias, cull thresholds and shadow distance through a lift hook; modest effect, and it holds 30 fps (not 60) with async recording. Next, in the roadmap's order: sharper upscaling and the resolution gaps; a shader and pipeline
pre-warm (the first race adds about 230 SPIR-V files; Vulkan pipelines are built on demand and the port
keeps no pipeline cache); the far-distance investigation (the game, not the renderer, decides the
distance, so it starts as a "where is it decided" spike).

## State of this machine and the repo

- Work is on branch **`linux-fork`** (the main line). `android-experiment` is separate and carries a
  shader change that has **not** been merged into `linux-fork`. It is neutral on this desktop GPU
  (checked in a race: 57.3 fps against 56.6) but changes shader output on every platform, so it waits.
- **The `ps3recomp/` checkout currently holds the Android branch's edits**, and the Linux binary in
  `work/port/build/` was built from them. It plays fine. For a build of exactly what `linux-fork` commits,
  use a fresh clone and `scripts/setup.sh` (resetting this checkout in place is untested).
- `~/.local/bin/ffmpeg` is a user-level copy (needed by the benchmark); the Android SDK/NDK in
  `~/android-toolchain` is the owner's own and was only used, not changed.

## Rules that apply

- **Never commit or publish the game, its keys, or anything generated from it** (lifted code, shader
  caches, the APK). The installer must stay hostable on GitHub: no decryption keys in it.
- Commits use the repo-local identity `SirTobyGirl@users.noreply.github.com`, never a personal email.
- Keep everything on the fork; do not open a pull request to the original author without being asked.
- Build in a clean environment: a compiler or sysroot exported in the shell (`CC`, `CXX`, `LDFLAGS`,
  `CMAKE_SYSROOT`, `LD_LIBRARY_PATH`) breaks the build. Each lifted chunk needs about 7.5 GB to compile, so
  use 3 parallel jobs on a 31 GB machine.
- Do not use `pkill -f` with a pattern that appears in the command itself: it kills the shell.
- Benchmarks include their own overhead: use `BENCH_DUMP_EVERY=60` for steady figures.
- **Hardware:** do not run a handheld or phone hard for more than a short guarded test unless the owner
  can watch it (the Thor's GPU reaches its 95 C throttle point in about 75 s). `scripts/android_session.sh`
  has a hard temperature stop.
- Be plain about what was tested and what was not; say so in the commit message.

## Open threads

- An issue was opened on OddTrie/MotorstormPCificRift (#1) asking where that project's code lives; no
  reply yet. That is a different project from the one this fork follows.
- Whether to offer any of this to the original author (the scripts are the least contentious part) was
  discussed and deliberately left alone.
