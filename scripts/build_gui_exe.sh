#!/usr/bin/env bash
# Build the setup wizard (scripts/setup_gui.py) into ONE executable that
# needs no Python and no checkout: dist/MotorStormPR-Setup.
#
#   scripts/build_gui_exe.sh
#
# The executable carries this repository's scripts and sources (git-tracked
# files only: scripts/, work/, docs/*.md and the top-level files) and unpacks
# them into a workspace (default ~/MotorStormPR) on first run. Nothing from
# the game, nothing generated from it, and none of your own files are in it.
#
# It is built with PyInstaller into .build-gui/ (git-ignored). The binary
# needs a glibc at least as new as the one it was built on, so build it on
# the oldest system you want to support (or in a container of that system).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
B="$ROOT/.build-gui"
NAME=MotorStormPR-Setup

# 1. A Python 3.10+ with venv (the system one first: a foreign SDK's may lack
#    the shared library PyInstaller wants).
PY=""
for c in "${PYTHON3:-}" /usr/bin/python3 python3.13 python3.12 python3.11 python3.10 python3; do
    [ -n "$c" ] && command -v "$c" >/dev/null || continue
    if "$c" -c 'import sys, venv; sys.exit(sys.version_info < (3, 10))' 2>/dev/null; then PY="$c"; break; fi
done
[ -n "$PY" ] || { echo "need Python 3.10 or newer with venv (set PYTHON3=...)" >&2; exit 1; }
command -v git >/dev/null || { echo "need git" >&2; exit 1; }
git rev-parse --git-dir >/dev/null 2>&1 || { echo "run this inside the git checkout" >&2; exit 1; }

rm -rf "$B/payload" "$B/work" "$B/spec" "$B/dist"
mkdir -p "$B/payload"
if [ ! -x "$B/venv/bin/python" ]; then "$PY" -m venv "$B/venv"; fi
"$B/venv/bin/pip" install -q --disable-pip-version-check "pyinstaller>=6,<7"

# 2. The payload: tracked files only, from the working tree.
ver="$(git rev-parse --short HEAD)"
[ -z "$(git status --porcelain --untracked-files=no)" ] || ver="$ver-dirty"
git ls-files -z -- scripts work README.md LICENSE CREDITS.md version.txt .gitignore 'docs/*.md' \
    | while IFS= read -r -d '' f; do
        [ -f "$f" ] || continue
        mkdir -p "$B/payload/$(dirname "$f")"
        cp -p "$f" "$B/payload/$f"
    done
echo "$ver" > "$B/payload/.bundle_version"
echo "payload: $(find "$B/payload" -type f | wc -l) files, $(du -sh "$B/payload" | cut -f1), version $ver"

# 3. Refuse to build if anything that looks like the game got in.
if find "$B/payload" -type f \( -name '*.elf' -o -name '*.self' -o -name '*.sprx' -o -name '*.prx' -o -name '*.psarc*' \
        -o -name '*.iso' -o -name '*.bin' -o -name '*.pkg' -o -name '*.sfo' \) | grep -q .; then
    echo "build_gui_exe: a game-type file is in the payload; refusing to build" >&2; exit 1
fi

# 4. One file, console on (the wizard prints its address there).
"$B/venv/bin/pyinstaller" --noconfirm --clean --onefile --name "$NAME" \
    --distpath "$B/dist" --workpath "$B/work" --specpath "$B/spec" \
    --add-data "$B/payload:payload" \
    "$ROOT/scripts/setup_gui.py" >"$B/pyinstaller.log" 2>&1 \
    || { tail -20 "$B/pyinstaller.log" >&2; echo "PyInstaller failed (full log: $B/pyinstaller.log)" >&2; exit 1; }

mkdir -p "$ROOT/dist"
cp "$B/dist/$NAME" "$ROOT/dist/$NAME"
( cd "$ROOT/dist" && sha256sum "$NAME" > "$NAME.sha256" )
echo "built: dist/$NAME ($(du -h "$ROOT/dist/$NAME" | cut -f1)), version $ver"
echo "glibc here: $(ldd --version 2>/dev/null | head -1 | grep -o '[0-9]*\.[0-9]*$') (it runs on systems with the same or newer)"
