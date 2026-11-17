#!/usr/bin/env bash
# Record the running game's resource use on the phone, then summarise it.
#
#   scripts/android_monitor.sh [seconds=120] [outfile=.build-android/monitor.csv]
#
# Every 2 s it reads, over adb: the game's memory (RSS, peak) and thread count, the CPU
# time it used (so cores busy), every core's clock, the GPU's load and clock, the hottest
# thermal zone, and the battery level and temperature. In the middle of the run it also
# takes a per-thread CPU snapshot (top -H). At the end it pulls the game's frame log
# (fliplog.txt, from PS3RECOMP_FLIPLOG in the app's env.txt) and prints the frame-time
# percentiles. Start the game first (and play: a race is the interesting part).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SECS="${1:-120}"; OUT="${2:-$ROOT/.build-android/monitor.csv}"
PKG=org.motorstormpr.rift; DIR=/sdcard/Android/data/$PKG/files
command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
PID="$(adb shell pidof "$PKG" | tr -d '\r')"
[ -n "$PID" ] || { echo "$PKG is not running" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
echo "t,rss_mb,peak_mb,threads,cores_busy,cpu0,cpu1,cpu2,cpu3,cpu4,cpu5,cpu6,cpu7,gpu_busy_pct,gpu_mhz,hottest_c,hottest_zone,battery_pct,battery_c" > "$OUT"

# One adb round trip per sample: everything is read by a small shell script on the phone.
read -r -d '' SAMPLE <<EOS || true
p=/proc/$PID
echo "RSS \$(grep VmRSS \$p/status | tr -s '[:space:]' ' ' | cut -d' ' -f2) \$(grep VmHWM \$p/status | tr -s '[:space:]' ' ' | cut -d' ' -f2) \$(grep Threads \$p/status | tr -s '[:space:]' ' ' | cut -d' ' -f2)"
echo "JIF \$(cut -d' ' -f14,15 \$p/stat)"
echo "FRQ \$(for i in 0 1 2 3 4 5 6 7; do echo -n "\$(( \$(cat /sys/devices/system/cpu/cpu\$i/cpufreq/scaling_cur_freq) / 1000 )) "; done)"
echo "GPU \$(cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage 2>/dev/null | tr -d ' %') \$(( \$(cat /sys/class/kgsl/kgsl-3d0/gpuclk 2>/dev/null || echo 0) / 1000000 ))"
echo "THM \$(for z in /sys/class/thermal/thermal_zone*; do echo "\$(cat \$z/temp 2>/dev/null) \$(cat \$z/type 2>/dev/null)"; done | sort -nr | head -1)"
echo "BAT \$(dumpsys battery | grep -E '^  (level|temperature):' | tr -d ' ' | tr '\n' ' ')"
EOS

prev_j=0; prev_t=0; n=$((SECS / 2)); snap=$((n / 2))
echo "recording $SECS s for pid $PID (Ctrl+C stops early and still summarises)"
for i in $(seq 1 "$n"); do
    t0=$(date +%s.%N)
    out="$(adb shell "$SAMPLE" 2>/dev/null | tr -d '\r')" || true
    [ -n "$out" ] || { echo "the game stopped (no data): ending" >&2; break; }
    now=$(date +%s.%N)
    rss=$(echo "$out" | awk '/^RSS/{printf "%.0f", $2/1024}'); peak=$(echo "$out" | awk '/^RSS/{printf "%.0f", $3/1024}'); thr=$(echo "$out" | awk '/^RSS/{print $4}')
    jif=$(echo "$out" | awk '/^JIF/{print $2+$3}'); frq=$(echo "$out" | awk '/^FRQ/{$1=""; sub(/^ /,""); gsub(/ +/,","); sub(/,$/,""); print}')
    gpu=$(echo "$out" | awk '/^GPU/{print $2","$3}'); thm=$(echo "$out" | awk '/^THM/{printf "%.0f,%s", $2/1000, $3}')
    bat=$(echo "$out" | awk '/^BAT/{for(i=2;i<=NF;i++){split($i,a,":"); if(a[1]=="level")l=a[2]; if(a[1]=="temperature")t=a[2]/10}; printf "%s,%s", l, t}')
    cores=$(awk -v j="$jif" -v pj="$prev_j" -v n="$now" -v pn="$prev_t" 'BEGIN{ if (pn>0) printf "%.2f", (j-pj)/100/(n-pn); else printf "0" }')
    prev_j=$jif; prev_t=$now
    echo "$((i * 2)),$rss,$peak,$thr,$cores,$frq,$gpu,$thm,$bat" >> "$OUT"
    if [ "$i" = "$snap" ]; then
        adb shell "top -H -b -n 2 -d 1 -p $PID" 2>/dev/null | tr -d '\r' | awk '/PID +USER/{c++} c==2' | head -14 > "$OUT.threads"
    fi
    sleep "$(awk -v a="$t0" -v b="$(date +%s.%N)" 'BEGIN{d=2-(b-a); if(d<0.1)d=0.1; printf "%.2f", d}')"
done

echo; echo "== summary ($OUT) =="
python3 - "$OUT" <<'PY'
import csv, sys, statistics as st
rows = list(csv.DictReader(open(sys.argv[1])))
if len(rows) < 2: print("too few samples"); sys.exit()
f = lambda k: [float(r[k]) for r in rows[1:] if r[k] not in ("", "nan")]
def line(name, v, unit=""): print(f"  {name:<22} avg {st.mean(v):7.1f}   peak {max(v):7.1f} {unit}")
line("memory (RSS)", f("rss_mb"), "MB"); print(f"  {'peak memory (HWM)':<22} {max(f('peak_mb')):.0f} MB   threads {max(f('threads')):.0f}")
line("CPU cores busy", f("cores_busy"), "cores (of 8)")
for c in range(8):
    v = f(f"cpu{c}"); print(f"  cpu{c} clock             avg {st.mean(v):6.0f}   max {max(v):6.0f} MHz")
line("GPU load", f("gpu_busy_pct"), "%"); line("GPU clock", f("gpu_mhz"), "MHz")
t = f("hottest_c"); print(f"  hottest zone           start {t[0]:.0f}  end {t[-1]:.0f}  peak {max(t):.0f} C  ({rows[-1]['hottest_zone']})")
b = f("battery_pct"); bt = f("battery_c"); print(f"  battery                {b[0]:.0f}% -> {b[-1]:.0f}%   temp {bt[0]:.1f} -> {bt[-1]:.1f} C")
PY
if [ -s "$OUT.threads" ]; then echo; echo "== busiest threads (top -H) =="; cut -c1-110 "$OUT.threads"; fi
if adb shell "test -f $DIR/fliplog.txt" 2>/dev/null; then
    adb pull -q "$DIR/fliplog.txt" "$ROOT/.build-android/fliplog.txt" && { echo; echo "== frame times (fliplog) =="; python3 "$ROOT/work/tools/fliplog.py" "$ROOT/.build-android/fliplog.txt" 2>&1 | head -12; }
fi
