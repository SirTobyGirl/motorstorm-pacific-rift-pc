#!/usr/bin/env python3
"""Why are some frames slow? Compare slow frames with fast ones in a PS3_FTRACE trace.

  work/tools/ftrace_slow.py <trace.txt> [slow_ms=20] [fast_ms=17.2]

Frames are cut at each guest flip request (as ps3recomp/tools/ftrace_report.py does). For every
frame it totals what the main thread (tid 1) and the JobBatchHandler spent in usleeps and
blocking syscalls (by guest call site), the RSX thread's drains and ACQUIRE waits, GPU waits,
presents and SPU busy time, then prints what is bigger in the slow frames than the fast ones,
how the slow frames are spaced, and a one-line-per-100-frames picture of the sequence.

A regular spacing is the clue: a fixed rhythm (every N frames, or every N ms) points at
something periodic rather than at the game's content. Record a trace with
FTRACE=<prefix> work/tools/bench.sh ..., which writes <prefix>_r<round>.txt.
It was used to find that the benchmark's own frame dumps (one every 10 frames) made every
10th frame slow at 60 fps; see docs/BASELINE.md.
"""
import sys, collections, statistics as st
path = sys.argv[1]
SLOW = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
FAST = float(sys.argv[3]) if len(sys.argv) > 3 else 17.2
(SYSCALL, USLEEP, SPU_WAIT, SPU_BUSY, SEMA, SEMA_GAVEUP, DRAIN, FLIP_FIFO, FLIP_REQ, PRESENT, GPU_ENCODE, GPU_WAIT, READBACK, GPU_EXEC, HLE) = range(1, 16)
names = {}; spans = []
with open(path) as f:
    for line in f:
        if line.startswith('#'): continue
        p = line.split()
        if p[0] == 'T': names[int(p[1])] = ' '.join(p[2:]); continue
        try: spans.append((int(p[0]), int(p[1]), int(p[2], 16), int(p[3], 16), float(p[4]), float(p[5])))
        except Exception: pass
flips = sorted(s[4] for s in spans if s[0] == FLIP_REQ)
frames = [(flips[i], flips[i+1]) for i in range(len(flips)-1)]
dur = [(b-a)/1000.0 for a, b in frames]
print(f"{len(frames)} frames; duration ms: median {st.median(dur):.1f}  p90 {sorted(dur)[int(.9*len(dur))]:.1f}  p99 {sorted(dur)[int(.99*len(dur))]:.1f}  max {max(dur):.1f}")
# index spans by time for speed
byk = collections.defaultdict(list)
for s in spans: byk[s[0]].append(s)
for k in byk: byk[k].sort(key=lambda s: s[4])
import bisect
def clip(lst, a, b):
    starts = [x[4] for x in lst]; i = bisect.bisect_left(starts, a - 200000)
    out = []
    while i < len(lst) and lst[i][4] < b:
        s = lst[i]; lo = max(s[4], a); hi = min(s[5], b)
        if hi > lo: out.append((s, hi - lo))
        i += 1
    return out
feat = collections.defaultdict(list)
for (a, b) in frames:
    f = collections.defaultdict(float)
    for s, d in clip(byk[USLEEP], a, b):
        if s[1] == 1: f['main usleep @%06X' % s[3]] += d / 1000
        elif s[1] == 12: f['JobBatchHandler usleep'] += d / 1000
    for s, d in clip(byk[SYSCALL], a, b):
        if s[1] == 1: f['main blocked syscall @%06X' % s[3]] += d / 1000
    for s, d in clip(byk[DRAIN], a, b): f['RSX drain'] += d / 1000
    for s, d in clip(byk[SEMA], a, b): f['RSX blocked on ACQUIRE'] += d / 1000
    for s, d in clip(byk[GPU_WAIT], a, b): f['RSX gpu wait kind %d' % s[3] if False else 'RSX gpu wait'] += d / 1000
    for s, d in clip(byk[SPU_BUSY], a, b): f['SPU busy (all lanes)'] += d / 1000
    for s, d in clip(byk[PRESENT], a, b): f['present'] += d / 1000
    feat['_dur'].append((b - a) / 1000.0)
    for k in list(f): pass
    for k, v in f.items(): feat[k].append(v)
    for k in set(feat) - set(f) - {'_dur'}: feat[k].append(0.0)
n = len(frames)
slow = [i for i in range(n) if feat['_dur'][i] >= SLOW]; fast = [i for i in range(n) if feat['_dur'][i] <= FAST]
print(f"fast frames (<={FAST} ms): {len(fast)}   slow frames (>={SLOW} ms): {len(slow)}")
rows = []
for k, v in feat.items():
    if k == '_dur' or len(v) != n: continue
    mf = st.mean(v[i] for i in fast) if fast else 0; ms_ = st.mean(v[i] for i in slow) if slow else 0
    rows.append((ms_ - mf, k, mf, ms_))
rows.sort(reverse=True)
print("\nwhat is bigger in slow frames (mean ms per frame):")
print(f"  {'':44s} {'fast':>7s} {'slow':>7s} {'diff':>7s}")
for d, k, mf, ms_ in rows[:8]: print(f"  {k:44s} {mf:7.2f} {ms_:7.2f} {d:+7.2f}")
print("\nwhat is smaller in slow frames:")
for d, k, mf, ms_ in rows[-4:][::-1]: print(f"  {k:44s} {mf:7.2f} {ms_:7.2f} {d:+7.2f}")
# pattern: positions of slow frames
pos = [i for i in slow]
gaps = [pos[i+1]-pos[i] for i in range(len(pos)-1)]
print(f"\nslow-frame positions: {len(pos)} of {n}; gaps between consecutive slow frames (frames): median {st.median(gaps) if gaps else 0}, histogram of small gaps: {dict(collections.Counter(g for g in gaps if g <= 12))}")
# sequence view of durations for a stretch
seq = ''.join('#' if d >= 20 else ('+' if d > 17.4 else '.') for d in feat['_dur'])
print("\nframe sequence ('.' normal, '+' a little long, '#' >= 20 ms), 100 per line:")
for i in range(0, len(seq), 100): print("  " + seq[i:i+100])
