# What this fork adds

This fork of [millennium-h4x0r/motorstorm-pacific-rift-pc](https://github.com/millennium-h4x0r/motorstorm-pacific-rift-pc)
adds setup tooling around the port. **It does not change the port itself**: the
C++ sources, the lift and the ps3recomp patch are exactly the author's. Nothing
of the game is in any change, and `scripts/check_clean.sh` (the author's own
guard) passed when each commit was made.

The changes are a **stack of five layers**. Each layer builds on the one below
it, so you take them from the bottom up and stop where you like. They all start
from the author's `main` at `b89c73e`.

| Layer | Branch | Commit | What it adds | Needs |
|---|---|---|---|---|
| 1. Scripts | `stack/1-scripts` | `2f0ac1b` | `scripts/doctor.sh` (read-only check of the machine and checkout) and `scripts/prepare_disc.sh` (builds `disc/` from an ISO or a `PS3_GAME` folder, decrypts `EBOOT.BIN` through RPCS3, checks the checksums). README section for immutable distros (Bazzite, Fedora Atomic, SteamOS). | nothing |
| 2. Wizard | `stack/2-wizard` | `1092e79` | `scripts/setup_gui.py`: a local web page (Python standard library only, listens on 127.0.0.1 with a random token) that runs the scripts with buttons, a live log and a build progress bar. | layer 1 |
| 3. Executable | `stack/3-executable` | `d74be80` | `scripts/build_gui_exe.sh`: packs the wizard with PyInstaller into one file, `MotorStormPR-Setup`, that needs no Python and no checkout and unpacks into `~/MotorStormPR`. | layers 1-2 |
| 4. Updates | `stack/4-updates` | `31ad105`, `e9a3c99` | An *Updates* card: checks GitHub when you press the button and, if the repo is strictly ahead, replaces only the port's scripts and sources (backing up anything you changed). | layers 1-3 |
| 5. Release workflow | `stack/5-release-workflow` | `d30256b` | `.github/workflows/release-gui.yml` builds the executable on Ubuntu 22.04 and attaches it to a GitHub Release on a `v*` tag; `scripts/smoke_gui_exe.sh` tests it. | layers 1-4 |

`feature/gui-executable` is layers 1-5 plus this file.

**`linux-fork` is this fork's main line:** layers 1-5, then the Linux-only
declaration (below), then [the roadmap](ROADMAP.md) (current goals: stable
performance, then enhanced frame rate, resolution and detail) and the deferred
[handheld and Android plan](PLAN_HANDHELD_AND_ANDROID.md).
This fork is Linux-only because Linux on one desktop PC is all it can test.

## Taking a layer

Layers are branches, so to take everything up to layer N, merge that branch
(it is a fast-forward on the author's current `main`):

```sh
git remote add fork https://github.com/SirTobyGirl/motorstorm-pacific-rift-pc.git
git fetch fork
git merge fork/stack/2-wizard          # layers 1 and 2
```

Or pick the commits yourself, in order:

```sh
git cherry-pick 2f0ac1b                      # layer 1
git cherry-pick 2f0ac1b 1092e79              # layers 1-2
git cherry-pick 2f0ac1b 1092e79 d74be80      # layers 1-3
git cherry-pick 2f0ac1b 1092e79 d74be80 31ad105 e9a3c99          # layers 1-4
git cherry-pick 2f0ac1b 1092e79 d74be80 31ad105 e9a3c99 d30256b  # layers 1-5
```

These combinations were tried against the author's `main` and apply cleanly.
**Skipping a lower layer does not work**: a later layer alone (for example the
wizard without the scripts, or the workflow without the executable) conflicts in
`README.md`, and would not run anyway, because it calls what the lower layer
adds. The README edits are small and separate from the code, so if you only want
the code, resolve those conflicts by keeping your README.

## The Linux-only declaration

| Branch | Commits | What it does |
|---|---|---|
| `linux-fork` | `f8574bc`, `6d1f9fc` | Removes macOS and Metal from the **docs and scripts** (README, CREDITS, NOTES, the wizard, `build.sh` ...), on top of all five layers. The C++ and the toolkit patch are left alone, so the code stays unified. |
| `linux-only` | `2f4bd79` | The same removal made earlier on layers 1-2 only. It conflicts with layers 3 and up; `linux-fork` supersedes it and it is kept only for history. |

This goes against the author's cross-platform design, which is why it is not
part of the five layers: take layers 1-5 and skip it if you want macOS text
kept.

## Android experiment

| Branch | What it does |
|---|---|
| `android-experiment` | On top of `linux-fork`: builds a private arm64 APK, with three Android-guarded fixes in the toolkit patch, an Android entry point and the scripts to build, copy the game over and monitor it. First run on an AYN Thor boots the intro at about 4 fps. See [ANDROID.md](ANDROID.md). Experimental: the Linux build is unchanged. |

`macos-metal` is the author's own branch, copied when the fork was made; it is
not part of these changes.

## How much is tested

Tested on one machine: Bazzite (Fedora-based, Linux x86-64, an AMD GPU).

- **Layer 1:** both scripts run against a real ISO and an extracted folder, and
  produce the same `disc/` layout as the manual steps. The game then built and
  booted to its start screen.
- **Layers 2-3:** the web endpoints, the token, Host and Origin checks, input
  validation, and a real `setup.sh` run through the page and through the
  packaged executable.
- **Layer 4:** against real GitHub, from a workspace holding an older commit:
  found the newer one, applied it, backed up a local edit, then reported up to
  date; it refused when there was no check, an unknown version, an older
  branch, or a malformed repo name.
- **Layer 5:** a real Actions run on a test tag passed, and the Release asset
  was downloaded, checksummed and smoke-tested (the test Release was deleted).

**Not tested:** the page rendered in a browser; the wizard's firmware step; a
full lift and build driven from the wizard; macOS; any other Linux
distribution.

## Who wrote it

Written with Claude (Anthropic) working in the repository at the request of the
fork's owner (SirTobyGirl).
Every commit carries a `Co-Authored-By: Claude` line, and the work was tested
as described above rather than only read.
