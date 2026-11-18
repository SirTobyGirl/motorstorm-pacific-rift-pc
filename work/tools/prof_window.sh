#!/usr/bin/env bash
# One unattended run that says where the time goes during the benchmark window:
# the standard race benchmark (bench.sh, one round) with a frame trace AND a perf CPU profile
# taken over the same 10 s window, then a report per thread and for the RSX (render-command) thread.
#
#   [INST=prof] work/tools/prof_window.sh [VAR=val ...]       e.g.  PS3RECOMP_FPS=60 PS3RECOMP_RES_SCALE=2
# Output: work/inst/<INST>/prof/{report.txt, perf.data, trace.txt, bench.txt}. Needs perf and the bench tools'
# own needs (ffmpeg); it uses user-space sampling only (perf_event_paranoid <= 2).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
I="${INST:-prof}"; P="$ROOT/work/inst/$I/prof"; TRIG="$P/window.trigger"
command -v perf >/dev/null || { echo "perf is needed" >&2; exit 1; }
mkdir -p "$P"; rm -f "$TRIG" "$P"/trace_r1.txt
ROUNDS=1 INST="$I" FTRACE="$P/trace" PROF_TRIGGER="$TRIG" BENCH_DUMP_EVERY="${BENCH_DUMP_EVERY:-60}" \
    "$ROOT/work/tools/bench.sh" "$@" > "$P/bench.txt" 2>&1 &
BENCH=$!
for _ in $(seq 1 900); do [ -e "$TRIG" ] && break; kill -0 $BENCH 2>/dev/null || break; sleep 0.5; done
if [ -e "$TRIG" ]; then
    PID="$(pgrep -x PacificRiftReco | tail -1)"
    echo "window open: sampling pid $PID for 9 s"
    perf record -q -F 1500 -g -p "$PID" -o "$P/perf.data" -- sleep 9 >/dev/null 2>&1
else echo "the benchmark never reached its window (see $P/bench.txt)" >&2; fi
wait $BENCH
{
  echo "== benchmark =="; cat "$P/bench.txt"
  echo; echo "== frame trace =="; python3 "$ROOT/ps3recomp/tools/ftrace_report.py" "$P/trace_r1.txt" --frames 0 2>/dev/null | sed -n 1,3p
  python3 "$ROOT/ps3recomp/tools/ftrace_report.py" "$P/trace_r1.txt" --frames 0 2>/dev/null | sed -n '/RSX \/ present/,/HLE import/p' | head -14
  if [ -s "$P/perf.data" ]; then
    echo; echo "== CPU by thread =="
    perf report -i "$P/perf.data" --no-children --sort tid --percent-limit 2 --stdio 2>/dev/null | grep -E "^ +[0-9.]+%" | head -8
    RSX="$(perf report -i "$P/perf.data" --no-children --sort tid,sym --stdio 2>/dev/null | grep -E "rsx_|vk_" | awk '{print $2}' | sort | uniq -c | sort -nr | head -1 | awk '{print $2}')"
    echo; echo "== RSX thread $RSX: top functions (percent of ALL samples) =="
    perf report -i "$P/perf.data" --no-children --tid "${RSX%%:*}" --sort sym,dso --percent-limit 0.4 --stdio -g none 2>/dev/null | grep -E "^ +[0-9.]+%" | head -22 | cut -c1-150
  fi
} > "$P/report.txt" 2>&1
cat "$P/report.txt"
