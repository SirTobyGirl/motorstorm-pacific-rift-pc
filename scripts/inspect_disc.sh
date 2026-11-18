#!/usr/bin/env bash
# Look at a game dump WITHOUT extracting it, and say what scripts/prepare_disc.sh will need to do.
#
#   scripts/inspect_disc.sh <game.iso | PS3_GAME folder | folder holding PS3_GAME>
#
# Prints KEY=value lines:
#   KIND      iso | folder | unknown
#   EBOOT     elf_ok (decrypted and the right version) | elf_wrong | bin_sce (needs RPCS3 to decrypt) |
#             bin_plain (not a protected file: the dump is probably still encrypted) | none
#   NEED_RPCS3  yes | no   (for the executable; the firmware needs it separately)
#   PSARC     whole | split | missing
#   NOTE      a plain-language line
# For an ISO it reads only the few files it needs (the executable, a few bytes of it, a listing).
set -uo pipefail
EXPECT_SHA=bbb7fe2545e54847483439f59cda54e907db23a111d7afe9a1b655624886b287
IN="${1:-}"
out() { echo "$1=$2"; }
[ -n "$IN" ] && [ -e "$IN" ] || { out KIND unknown; out NOTE "that path does not exist"; exit 0; }
sha() { sha256sum | cut -c1-64; }
G=""
if [ -f "$IN" ]; then
    case "${IN,,}" in *.iso) ;; *) out KIND unknown; out NOTE "that is a file but not an .iso"; exit 0 ;; esac
    out KIND iso
    if command -v 7z >/dev/null; then
        rd() { 7z x -so "$IN" "$1" 2>/dev/null; }
        ls_() { 7z l -ba "$IN" 2>/dev/null | awk '{print $NF}'; }
    elif command -v bsdtar >/dev/null; then
        rd() { bsdtar -xOf "$IN" "$1" 2>/dev/null; }
        ls_() { bsdtar -tf "$IN" 2>/dev/null; }
    else out NOTE "7z or bsdtar is needed to read an ISO"; exit 0; fi
    names="$(ls_)"
    has() { grep -qx "PS3_GAME/USRDIR/$1" <<<"$names"; }
    if [ -z "$names" ] || ! grep -q "^PS3_GAME/" <<<"$names"; then
        out EBOOT none; out NOTE "no PS3_GAME inside: is this a PS3 game ISO, and is it decrypted?"; exit 0; fi
    if has EBOOT.elf && [ "$(rd PS3_GAME/USRDIR/EBOOT.elf | sha)" = "$EXPECT_SHA" ]; then e=elf_ok
    elif has EBOOT.elf; then e=elf_wrong
    elif has EBOOT.BIN; then [ "$(rd PS3_GAME/USRDIR/EBOOT.BIN | head -c3)" = SCE ] && e=bin_sce || e=bin_plain
    else e=none; fi
    if has ms2.psarc; then ps=whole; elif grep -q "^PS3_GAME/USRDIR/ms2.psarc.666" <<<"$names"; then ps=split; else ps=missing; fi
else
    out KIND folder
    if [ -d "$IN/PS3_GAME" ]; then G="$IN/PS3_GAME"; elif [ "$(basename "$IN")" = PS3_GAME ]; then G="$IN"
    else out EBOOT none; out NOTE "no PS3_GAME folder in there"; exit 0; fi
    U="$G/USRDIR"
    if [ -f "$U/EBOOT.elf" ] && [ "$(sha < "$U/EBOOT.elf")" = "$EXPECT_SHA" ]; then e=elf_ok
    elif [ -f "$U/EBOOT.elf" ]; then e=elf_wrong
    elif [ -f "$U/EBOOT.BIN" ]; then [ "$(head -c3 "$U/EBOOT.BIN")" = SCE ] && e=bin_sce || e=bin_plain
    else e=none; fi
    if [ -f "$U/ms2.psarc" ]; then ps=whole; elif ls "$U"/ms2.psarc.666* >/dev/null 2>&1; then ps=split; else ps=missing; fi
fi
out EBOOT "$e"; out PSARC "$ps"
case "$e" in
    elf_ok)   out NEED_RPCS3 no;  note="The executable is already decrypted and is the right version: RPCS3 is not needed for the game." ;;
    bin_sce)  out NEED_RPCS3 yes; note="The executable is the protected EBOOT.BIN: RPCS3 will decrypt it." ;;
    elf_wrong) out NEED_RPCS3 no; note="There is a decrypted executable, but it is not BCES-00129 version 01.00 (other region or version): this port cannot use it." ;;
    bin_plain) out NEED_RPCS3 no; note="EBOOT.BIN is not a protected executable: the dump is probably still encrypted at disc level (decrypt it with its key first)." ;;
    *)        out NEED_RPCS3 no;  note="No executable found." ;;
esac
[ "$ps" = split ] && note="$note ms2.psarc is in pieces and will be joined (about 4.8 GB)."
[ "$ps" = missing ] && note="$note ms2.psarc is missing."
out NOTE "$note"
