#!/usr/bin/env bash
# Put YOUR game into disc/ from an ISO or from a PS3_GAME folder: extract it
# when needed, decrypt EBOOT.BIN to EBOOT.elf with RPCS3, link the rest, and
# join a split ms2.psarc. Nothing here leaves your machine, and disc/ is
# git-ignored.
#
#   scripts/prepare_disc.sh <game.iso | PS3_GAME folder | folder holding PS3_GAME>
#
# Needs 7z (or bsdtar) for an ISO, and RPCS3 (native `rpcs3`, or the Flatpak
# net.rpcs3.RPCS3) to decrypt the executable. An ISO that is itself
# encrypted (a Redump image with a separate .key) must be decrypted first.
# Not handled here: the firmware (RPCS3: File > Install Firmware).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXPECT_SHA=bbb7fe2545e54847483439f59cda54e907db23a111d7afe9a1b655624886b287
EXPECT_PSARC=4811608561
IN="${1:-}"
usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ -n "$IN" ] || usage
[ -e "$IN" ] || { echo "prepare_disc: $IN does not exist" >&2; exit 2; }
IN="$(cd "$(dirname "$IN")" && pwd)/$(basename "$IN")"
WORK="${PREPARE_WORK:-$ROOT/disc.work}"     # extraction and decrypt scratch (git-ignored by the allowlist)
DISC="${DISC_DIR:-$ROOT/disc}"
die() { echo "prepare_disc: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -c1-64; }

# 1. Find PS3_GAME, extracting an ISO if that is what we were given.
if [ -f "$IN" ]; then
    case "${IN,,}" in *.iso) ;; *) die "$IN is a file but not an .iso" ;; esac
    mkdir -p "$WORK/extracted"
    STAMP="$WORK/extracted/.from"; ID="$IN $(stat -c %s "$IN") $(stat -c %Y "$IN")"
    if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$ID" ] && [ -f "$WORK/extracted/PS3_GAME/PARAM.SFO" ]; then
        echo "this ISO was already extracted to $WORK/extracted: reusing it (delete that folder to extract again)"
        SKIP_EXTRACT=1
    fi
    [ -n "${SKIP_EXTRACT:-}" ] || echo "extracting $(basename "$IN") to $WORK/extracted (about 13 GB)"
    if [ -n "${SKIP_EXTRACT:-}" ]; then :
    elif command -v 7z >/dev/null; then
        # 7z exits 2 on warnings (some images end with a header it dislikes);
        # the size checks below catch a real truncation.
        7z x -y -bso0 -bsp0 "$IN" -o"$WORK/extracted" >/dev/null || [ $? -eq 2 ] || die "7z failed"
    elif command -v bsdtar >/dev/null; then
        bsdtar -xf "$IN" -C "$WORK/extracted" || die "bsdtar failed"
    else die "need 7z or bsdtar to read an ISO"; fi
    [ -n "${SKIP_EXTRACT:-}" ] || echo "$ID" > "$STAMP"
    SRC="$WORK/extracted/PS3_GAME"
elif [ -d "$IN/PS3_GAME" ]; then SRC="$IN/PS3_GAME"
elif [ "$(basename "$IN")" = PS3_GAME ]; then SRC="$IN"
else die "no PS3_GAME folder in $IN"; fi
[ -f "$SRC/USRDIR/EBOOT.BIN" ] || [ -f "$SRC/USRDIR/EBOOT.elf" ] || die "no EBOOT in $SRC/USRDIR"
[ -f "$SRC/PARAM.SFO" ] || die "no PARAM.SFO in $SRC (not a PS3 game folder?)"
if [ -f "$SRC/USRDIR/EBOOT.BIN" ] && [ "$(head -c 3 "$SRC/USRDIR/EBOOT.BIN")" != SCE ]; then
    die "EBOOT.BIN is not an SCE file: is the dump encrypted? decrypt the ISO first"
fi

# 2. Decrypt the executable.
mkdir -p "$DISC/PS3_GAME/USRDIR"
ELF="$DISC/PS3_GAME/USRDIR/EBOOT.elf"
if [ -f "$SRC/USRDIR/EBOOT.elf" ] && [ "$(sha "$SRC/USRDIR/EBOOT.elf")" = "$EXPECT_SHA" ]; then
    cp "$SRC/USRDIR/EBOOT.elf" "$ELF"
else
    mkdir -p "$WORK/decrypt"
    cp "$SRC/USRDIR/EBOOT.BIN" "$WORK/decrypt/EBOOT.BIN"
    rm -f "$WORK/decrypt/EBOOT.elf"
    "$ROOT/scripts/find_rpcs3.sh" run --decrypt "$WORK/decrypt/EBOOT.BIN" >/dev/null 2>&1 \
        || die "RPCS3 not found or it failed (install it: https://rpcs3.net, or point to it: RPCS3_LOCATION=/path/to/rpcs3 or its data folder)"
    [ -f "$WORK/decrypt/EBOOT.elf" ] || die "RPCS3 did not produce EBOOT.elf"
    cp "$WORK/decrypt/EBOOT.elf" "$ELF"
fi
if [ "$(sha "$ELF")" = "$EXPECT_SHA" ]; then echo "EBOOT.elf: BCES-00129 01.00, checksum ok"
else die "EBOOT.elf has the wrong checksum ($(sha "$ELF")): this port needs BCES-00129 (Europe) 01.00"; fi

# 3. Link everything else; join a split ms2.psarc.
for f in "$SRC"/*; do [ "$(basename "$f")" = USRDIR ] || ln -sfn "$f" "$DISC/PS3_GAME/"; done
for f in "$SRC"/USRDIR/*; do
    case "$(basename "$f")" in EBOOT.*|ms2.psarc*) ;; *) ln -sfn "$f" "$DISC/PS3_GAME/USRDIR/" ;; esac
done
rm -f "$DISC/PS3_GAME/USRDIR/ms2.psarc"
if [ -f "$SRC/USRDIR/ms2.psarc" ]; then
    ln -sfn "$SRC/USRDIR/ms2.psarc" "$DISC/PS3_GAME/USRDIR/ms2.psarc"
elif ls "$SRC"/USRDIR/ms2.psarc.666* >/dev/null 2>&1; then
    echo "joining ms2.psarc pieces (about 4.8 GB)"
    cat "$SRC"/USRDIR/ms2.psarc.666* > "$DISC/PS3_GAME/USRDIR/ms2.psarc"
else die "no ms2.psarc in $SRC/USRDIR"; fi
sz=$(stat -L -c %s "$DISC/PS3_GAME/USRDIR/ms2.psarc")
[ "$sz" = "$EXPECT_PSARC" ] || die "ms2.psarc is $sz bytes, expected $EXPECT_PSARC (truncated copy or extraction?)"

echo "disc/ is ready. Next: scripts/doctor.sh, then scripts/setup.sh, lift.sh, build.sh."
if [ -f "$IN" ]; then
    echo "Keep $WORK/extracted: disc/ links into it. $WORK/decrypt can be deleted."
fi
