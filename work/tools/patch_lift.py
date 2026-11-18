#!/usr/bin/env python3
"""Port hooks spliced into the lifted PPU code after scripts/lift.sh lifts it.

The lifted sources are generated from the user's executable and are never
committed, so changes to the game's own code are made here, as small
insertions that call a function defined in work/port. Each insertion is
matched against the exact text the lifter emits; if that text is not there
(a different lifter or executable), this script fails rather than guessing.
Running it again on an already patched tree changes nothing.

  work/tools/patch_lift.py [work/src/recomp]
"""
import glob, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'work', 'src', 'recomp')

# (function, text after which the hook goes, hook text)
PATCHES = [
    # 60 fps mode. func_00435D18 is the frame timer's SetInterval(timer, n):
    # it stores n vblanks per frame at timer+0x18 and recomputes the fixed
    # step (n * 1e6 / 60 us). The game asks for 2 (30 fps); the vblank gate's
    # threshold is copied from the same field each frame. port_timer_interval
    # turns 2 into 1 when PS3RECOMP_FPS=60, which halves the step and lets a
    # frame through on every vblank.
    ('func_00435D18',
     'void func_00435D18(ppu_context* ctx) {\n    PPU_MM_SNAP\n',
     '    { void port_timer_interval(ppu_context*); port_timer_interval(ctx); } /* patch_lift.py */\n'),
    # func_008CF1C0 takes the frame step in f1 and hands it to the game clock
    # (func_0031BA20) and the other per-frame updates. port_frame_dt (60 fps
    # mode) scales it by the vblanks the previous frame really took, so a
    # frame that misses 60 does not run the game in slow motion.
    ('func_008CF1C0',
     'void func_008CF1C0(ppu_context* ctx) {\n    PPU_MM_SNAP\n',
     '    { void port_frame_dt(ppu_context*); port_frame_dt(ctx); } /* patch_lift.py */\n'),
    # func_0037FB68 steps the chase camera's shake springs once per call,
    # with no frame time. port_shake_skip (60 fps mode) lets it run once per
    # two vblanks per camera, the rate the CHASE_SHAKE_* tuning assumes.
    ('func_0037FB68',
     'void func_0037FB68(ppu_context* ctx) {\n    PPU_MM_SNAP\n',
     '    { int port_shake_skip(ppu_context*); if (port_shake_skip(ctx)) return; } /* patch_lift.py */\n'),
    # func_0042E3F0 is evo::camerasystem::Shake's update: fresh random
    # offsets from the game RNG (func_00439820) on every call. In 60 fps mode
    # port_camshake_enter/port_camshake_rand make it replay its draws within
    # a 30 Hz tick, so the jolts change at the rate the game was made for.
    ('func_0042E3F0',
     'void func_0042E3F0(ppu_context* ctx) {\n    PPU_MM_SNAP\n',
     '    { void port_camshake_enter(ppu_context*); port_camshake_enter(ctx); } /* patch_lift.py */\n'),
    ('func_00439820',
     'void func_00439820(ppu_context* ctx) {\n    PPU_MM_SNAP\n',
     '    { int port_camshake_rand(ppu_context*); if (port_camshake_rand(ctx)) return; } /* patch_lift.py */\n'),
]


# Insertions inside a function's body, for text that also occurs in other functions (the lifter emits some functions
# twice, from an alternate entry): (function, anchor line, hook text). The anchor must occur exactly once inside the
# function, and the hook goes right after it. Viewport quality (see port_vq_float in work/port/port_patches.cpp):
# func_00360DD4 parses /scripts/viewportquality.xml; func_00360DD8 is its alternate entry and has the same code.
def _vq(fn):
    st = lambda reg, fp, off: ('{ float ftmp = (float)ctx->fpr[%d]; uint32_t tmp; memcpy(&tmp, &ftmp, 4); '
                               'vm_write32(ctx->gpr[%d] + 0x%X, tmp); }\n' % (fp, reg, off))
    call = lambda reg, off, kind: ('    { void port_vq_float(uint32_t, int); port_vq_float((uint32_t)(ctx->gpr[%d] + 0x%X), %d); } '
                                   '/* patch_lift.py */\n' % (reg, off, kind))
    return [(fn, st(11, 2, 0x0), call(11, 0x0, 0)),      # group LOD bias
            (fn, st(12, 4, 0xC), call(12, 0xC, 1)),      # group cull threshold
            (fn, st(29, 6, 0x10), call(29, 0x10, 2)),    # group shadow cull threshold
            (fn, st(28, 10, 0x3C), call(28, 0x3C, 3))]   # shadow fade distance modifier
FUNC_PATCHES = _vq('func_00360DD4') + _vq('func_00360DD8')


def _patch_in_function(files):
    for fn, anchor, hook in FUNC_PATCHES:
        head = 'void %s(ppu_context* ctx) {' % fn
        hits = [p for p in files if head in open(p, encoding='utf-8', errors='surrogateescape').read()]
        if len(hits) != 1:
            sys.exit(f'patch_lift: {fn}: expected its lifted entry once, found it in {len(hits)} files')
        p = hits[0]
        s = open(p, encoding='utf-8', errors='surrogateescape').read()
        i = s.index(head)
        j = s.index('\n}\n', i)
        body = s[i:j]
        if anchor + hook in body:
            print(f'patch_lift: {fn}: already patched ({os.path.basename(p)})')
            continue
        if body.count(anchor) != 1:
            sys.exit(f'patch_lift: {fn}: anchor appears {body.count(anchor)} times in the function ({anchor.strip()[:60]})')
        body = body.replace(anchor, anchor + hook)
        open(p, 'w', encoding='utf-8', errors='surrogateescape').write(s[:i] + body + s[j:])
        print(f'patch_lift: {fn}: patched ({os.path.basename(p)})')


def main():
    files = sorted(glob.glob(os.path.join(SRC, 'ppu_recomp_*.cpp')))
    if not files:
        sys.exit(f'patch_lift: no lifted sources in {SRC}')
    for fn, anchor, hook in PATCHES:
        hits = [p for p in files if anchor in open(p, encoding='utf-8', errors='surrogateescape').read()]
        if len(hits) != 1:
            sys.exit(f'patch_lift: {fn}: expected its lifted entry once, found it in {len(hits)} files')
        p = hits[0]
        s = open(p, encoding='utf-8', errors='surrogateescape').read()
        if anchor + hook in s:
            print(f'patch_lift: {fn}: already patched ({os.path.basename(p)})')
            continue
        if s.count(anchor) != 1:
            sys.exit(f'patch_lift: {fn}: entry appears {s.count(anchor)} times in {p}')
        open(p, 'w', encoding='utf-8', errors='surrogateescape').write(s.replace(anchor, anchor + hook))
        print(f'patch_lift: {fn}: patched ({os.path.basename(p)})')
    _patch_in_function(files)


if __name__ == '__main__':
    main()
