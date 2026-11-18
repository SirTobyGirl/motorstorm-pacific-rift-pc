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
- An Android experiment (branch `android-experiment`, **paused**): a private APK boots the game on an
  AYN Thor to its first 3D scene at 30 fps, and found a shader inefficiency (see below). Details in
  `docs/ANDROID.md` on that branch.

**Not done**
- Nothing yet improves what you *see* on Linux. No quality improvement has been started.
- The wizard and executable have never been viewed in a browser; the wizard's firmware step is unverified.
- Cold-versus-warm shader cache is not separated in the baseline; about 4% of 60 fps frames are still
  slow (clustered, cause not traced).
- On the Android branch: the pad-file fix has never run on the phone; a race on the phone was never reached.

## Next step: anisotropic filtering

The Vulkan renderer has **no anisotropic filtering** and ignores the texture **LOD bias** (a comment at
`rsx_vulkan_backend.c` says so). The D3D12 backend in the same toolkit already maps the game's own value.
This is the smallest change that visibly sharpens roads and ground at a distance.

1. In `ps3recomp/libs/video/rsx_vulkan_backend.c` the guest sampler is built at about line 1111
   (`VkSamplerCreateInfo sci`, `mipmapMode` from the RSX filter bits). Add `anisotropyEnable` and
   `maxAnisotropy`, taken from `TEXTURE_CONTROL0` bits [6:4] through the same table the D3D12 backend uses
   (`rsx_d3d12_backend.c` near line 2138: `{1, 2, 4, 6, 8, 10, 12, 16}`, applied only when both min and mag
   filters are linear). Enable the `samplerAnisotropy` device feature when creating the device.
2. Add a user override (an environment variable and a `settings.ini` key), up to 16x, off by default
   or matching the game's value, so the default look does not change unasked.
3. Then the LOD bias (`FILTER` bits [12:0]), as a separate step.
4. **Measure**: `BENCH_DUMP_EVERY=60 ROUNDS=3 work/tools/bench.sh PS3RECOMP_FPS=60 PS3RECOMP_RES_SCALE=2`
   with and without, plus before-and-after screenshots of the same race moment. Record the cost in
   BASELINE.md.
5. Toolkit edits are made in `ps3recomp/`, then `scripts/make_patch.sh` regenerates
   `work/patches/ps3recomp.patch`, which is what gets committed.

After that, in the roadmap's order: sharper upscaling and the resolution gaps; a shader and pipeline
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
- **Hardware:** do not run a phone or handheld hard for more than a short guarded test unless the owner
  can watch it (the Thor's GPU reaches its 95 C throttle point in about 75 s). `scripts/android_session.sh`
  has a hard temperature stop.
- Be plain about what was tested and what was not; say so in the commit message.

## Open threads

- An issue was opened on OddTrie/MotorstormPCificRift (#1) asking where that project's code lives; no
  reply yet. That is a different project from the one this fork follows.
- Whether to offer any of this to the original author (the scripts are the least contentious part) was
  discussed and deliberately left alone.
