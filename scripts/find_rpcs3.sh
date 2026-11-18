#!/usr/bin/env bash
# Find an RPCS3 that is already on this machine, and its data folder.
#
#   scripts/find_rpcs3.sh                 print what was found, as KEY=value lines
#   scripts/find_rpcs3.sh run ARGS...     run that RPCS3 with ARGS (a Flatpak gets access to the folders ARGS name)
#
# Looks, in order, at: RPCS3_LOCATION (a program file, or a data folder, that you point it to),
# `rpcs3` on PATH, the Flatpak net.rpcs3.RPCS3, and rpcs3*.AppImage in ~/Applications, ~/.local/bin,
# ~/bin, ~/Downloads, ~/Desktop and ~. The data folder (the one holding config.yml and dev_flash) is
# RPCS3_DATA, a folder named by RPCS3_LOCATION, a portable one next to the program, ~/.config/rpcs3, or the
# Flatpak's. Output keys: RPCS3_KIND (native|appimage|flatpak|none), RPCS3_BIN, RPCS3_DATA, RPCS3_FLASH
# (the installed firmware, when it is complete).
set -u
loc="${RPCS3_LOCATION:-}"; loc="${loc/#\~/$HOME}"
kind=none; bin=""; data="${RPCS3_DATA:-}"
cfgdir="${XDG_CONFIG_HOME:-$HOME/.config}"
FPK="$HOME/.var/app/net.rpcs3.RPCS3/config/rpcs3"

is_data() { [ -d "$1" ] && { [ -f "$1/config.yml" ] || [ -d "$1/dev_flash" ]; }; }

if [ -n "$loc" ]; then
    if [ -f "$loc" ] && [ -x "$loc" ]; then bin="$loc"
    elif [ -d "$loc" ]; then
        if is_data "$loc"; then data="${data:-$loc}"
        elif is_data "$loc/rpcs3"; then data="${data:-$loc/rpcs3}"; fi
        for f in "$loc"/rpcs3 "$loc"/rpcs3*.AppImage "$loc"/rpcs3*.appimage; do [ -x "$f" ] && [ -f "$f" ] && { bin="$f"; break; }; done
    fi
fi
if [ -z "$bin" ] && command -v rpcs3 >/dev/null 2>&1; then bin="$(command -v rpcs3)"; fi
if [ -z "$bin" ] && command -v flatpak >/dev/null 2>&1 && flatpak info net.rpcs3.RPCS3 >/dev/null 2>&1; then kind=flatpak; fi
if [ -z "$bin" ] && [ "$kind" = none ]; then
    for d in "$HOME/Applications" "$HOME/.local/bin" "$HOME/bin" "$HOME/Downloads" "$HOME/Desktop" "$HOME"; do
        for f in "$d"/rpcs3*.AppImage "$d"/rpcs3*.appimage "$d"/RPCS3*.AppImage; do
            [ -f "$f" ] && [ -x "$f" ] && { bin="$f"; break 2; }
        done
    done
fi
if [ -n "$bin" ]; then
    case "$bin" in *.AppImage|*.appimage) kind=appimage ;; *) kind=native ;; esac
fi

if [ -z "$data" ] && [ -n "$bin" ] && is_data "$(dirname "$bin")"; then data="$(dirname "$bin")"; fi   # portable
if [ -z "$data" ]; then
    order=("$cfgdir/rpcs3" "$FPK"); [ "$kind" = flatpak ] && order=("$FPK" "$cfgdir/rpcs3")
    for d in "${order[@]}"; do is_data "$d" && { data="$d"; break; }; done
fi

flash=""
for d in "$data/dev_flash" "$cfgdir/rpcs3/dev_flash" "$FPK/dev_flash"; do
    [ -f "$d/sys/external/flashMP3.pic" ] && { flash="$d"; break; }
done

if [ "${1:-}" = run ]; then
    shift
    case "$kind" in
        none) echo "RPCS3 not found: install it (https://rpcs3.net or flatpak install flathub net.rpcs3.RPCS3), or point to it" >&2; exit 127 ;;
        flatpak)
            fs=(); for a in "$@"; do [ -e "$a" ] && { p="$(cd "$(dirname "$a")" && pwd)"; fs+=(--filesystem="$p"); }; done
            exec flatpak run "${fs[@]}" net.rpcs3.RPCS3 "$@" ;;
        appimage) APPIMAGE_EXTRACT_AND_RUN=1 exec "$bin" "$@" ;;
        *) exec "$bin" "$@" ;;
    esac
fi
echo "RPCS3_KIND=$kind"; echo "RPCS3_BIN=$bin"; echo "RPCS3_DATA=$data"; echo "RPCS3_FLASH=$flash"
