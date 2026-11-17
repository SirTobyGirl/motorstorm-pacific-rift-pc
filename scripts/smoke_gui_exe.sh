#!/usr/bin/env bash
# Smoke-test the packaged setup wizard: start it in a throwaway workspace,
# check that it unpacks, answers with the right token and refuses without it,
# and that no game-type file is in what it unpacked. Used by the release
# workflow and runnable by hand.
#
#   scripts/smoke_gui_exe.sh [dist/MotorStormPR-Setup]
set -euo pipefail
EXE="${1:-dist/MotorStormPR-Setup}"
[ -x "$EXE" ] || { echo "smoke: $EXE is not an executable file" >&2; exit 2; }
EXE="$(cd "$(dirname "$EXE")" && pwd)/$(basename "$EXE")"
T="$(mktemp -d)"
pid=""
cleanup() { [ -n "$pid" ] && { pkill -P "$pid" 2>/dev/null || true; kill "$pid" 2>/dev/null || true; }; rm -rf "$T"; }
trap cleanup EXIT

( cd "$T" && MSPR_HOME="$T/ws" "$EXE" --no-browser --port 0 >"$T/out" 2>&1 ) &
pid=$!
url=""
for _ in $(seq 1 40); do
    url="$(grep -o 'http://127\.0\.0\.1:[0-9]*/?t=[A-Za-z0-9_-]*' "$T/out" 2>/dev/null | head -1 || true)"
    [ -n "$url" ] && break
    kill -0 "$pid" 2>/dev/null || { echo "smoke: the wizard exited early:" >&2; cat "$T/out" >&2; exit 1; }
    sleep 0.5
done
[ -n "$url" ] || { echo "smoke: no address printed within 20 s:" >&2; cat "$T/out" >&2; exit 1; }
base="${url%%/?t=*}"; tok="${url##*t=}"

fail=0
check() { if [ "$2" = "$3" ]; then echo "  ok    $1"; else echo "  FAIL  $1 (got $2, wanted $3)"; fail=1; fi; }
check "page with token"       "$(curl -s -o /dev/null -w '%{http_code}' "$url")" 200
check "status with token"     "$(curl -s -o /dev/null -w '%{http_code}' -H "X-Token: $tok" "$base/api/status")" 200
check "status without token"  "$(curl -s -o /dev/null -w '%{http_code}' "$base/api/status")" 403
check "foreign Host header"   "$(curl -s -o /dev/null -w '%{http_code}' -H "X-Token: $tok" -H 'Host: evil.example' "$base/api/status")" 403
check "unknown step refused"  "$(curl -s -o /dev/null -w '%{http_code}' -H "X-Token: $tok" -X POST -d '{"step":"nope"}' "$base/api/run")" 400
check "workspace unpacked"    "$([ -x "$T/ws/scripts/doctor.sh" ] && echo yes || echo no)" yes
check "no game-type files"    "$(find "$T/ws" -type f \( -name '*.elf' -o -name '*.psarc*' -o -name '*.iso' -o -name '*.self' -o -name '*.sprx' -o -name '*.pkg' \) | wc -l | tr -d ' ')" 0
[ "$fail" = 0 ] && echo "smoke: all checks passed" || { echo "smoke: FAILED" >&2; exit 1; }
