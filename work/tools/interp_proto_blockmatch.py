import cv2, numpy as np, glob, sys, time
files = sorted(glob.glob(sys.argv[1] + "/f*.ppm"), key=lambda p: int(''.join(c for c in p.split('/')[-1] if c.isdigit())))
imgs = [cv2.imread(f) for f in files]
H, W = imgs[0].shape[:2]
def luma(i): return cv2.cvtColor(i, cv2.COLOR_BGR2GRAY).astype(np.float32) / 255.0
def pyr(img, n=4):
    out = [img]
    for _ in range(n - 1): out.append(cv2.resize(out[-1], (max(1, out[-1].shape[1] // 2), max(1, out[-1].shape[0] // 2)), interpolation=cv2.INTER_AREA))
    return out
def sample(img, gx, gy): return cv2.remap(img, gx.astype(np.float32), gy.astype(np.float32), cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
def level_flow(a, b, d0, radius, penalty, win=5, subpix=False):
    h, w = a.shape
    gx, gy = np.meshgrid(np.arange(w, dtype=np.float32) + 0.0, np.arange(h, dtype=np.float32) + 0.0)
    best = np.full((h, w), 1e9, np.float32); bd = np.zeros((h, w, 2), np.float32)
    costs = {}
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            dX = d0[..., 0] + dx; dY = d0[..., 1] + dy
            pa = sample(a, gx - 0.5 * dX, gy - 0.5 * dY); pb = sample(b, gx + 0.5 * dX, gy + 0.5 * dY)
            c = cv2.boxFilter(np.abs(pa - pb), -1, (win, win), normalize=False) + penalty * (abs(dx) + abs(dy))
            costs[(dx, dy)] = c
            m = c < best; best = np.where(m, c, best)
            bd[..., 0] = np.where(m, dX, bd[..., 0]); bd[..., 1] = np.where(m, dY, bd[..., 1])
    if subpix:   # parabola on the 4 neighbours of the best (relative to per-pixel best: re-evaluate)
        for ax in (0, 1):
            e = np.zeros(2, np.float32); e[ax] = 1
            def cost_at(sgn):
                dX = bd[..., 0] + sgn * e[0]; dY = bd[..., 1] + sgn * e[1]
                pa = sample(a, gx - 0.5 * dX, gy - 0.5 * dY); pb = sample(b, gx + 0.5 * dX, gy + 0.5 * dY)
                return cv2.boxFilter(np.abs(pa - pb), -1, (win, win), normalize=False)
            cm, cp = cost_at(-1), cost_at(+1)
            # centre cost at bd
            pa = sample(a, gx - 0.5 * bd[..., 0], gy - 0.5 * bd[..., 1]); pb = sample(b, gx + 0.5 * bd[..., 0], gy + 0.5 * bd[..., 1])
            c0 = cv2.boxFilter(np.abs(pa - pb), -1, (win, win), normalize=False)
            den = cm - 2 * c0 + cp
            off = np.where(den > 1e-6, 0.5 * (cm - cp) / np.maximum(den, 1e-6), 0.0).clip(-0.5, 0.5)
            bd[..., ax] += off
    return bd
def estimate(A, B, levels=(3, 2, 1), radii=None, pens=None, win=5, subpix=False, final_lod0=False, median=False):
    pa, pb = pyr(luma(A)), pyr(luma(B))
    radii = radii or {3: 4, 2: 2, 1: 2, 0: 1}; pens = pens or {3: .03, 2: .03, 1: .03, 0: .03}
    lods = list(levels) + ([0] if final_lod0 else [])
    d = None
    for lod in lods:
        a, b = pa[lod], pb[lod]
        h, w = a.shape
        d0 = np.zeros((h, w, 2), np.float32) if d is None else np.round(cv2.resize(d, (w, h), interpolation=cv2.INTER_LINEAR) * 2.0)
        d = level_flow(a, b, d0, radii[lod], pens[lod], win, subpix and lod == lods[-1])
    if median:
        d = np.stack([cv2.medianBlur(d[..., k], 3) for k in range(2)], -1)
    return d, lods[-1]
def interpolate(A, B, d, lod, phase=0.5, thr=1.0):
    sc = 2 ** lod
    fx = cv2.resize(d[..., 0], (W, H), interpolation=cv2.INTER_LINEAR) * sc; fy = cv2.resize(d[..., 1], (W, H), interpolation=cv2.INTER_LINEAR) * sc
    gx, gy = np.meshgrid(np.arange(W, dtype=np.float32), np.arange(H, dtype=np.float32))
    a = sample(A, gx - phase * fx, gy - phase * fy); b = sample(B, gx + (1 - phase) * fx, gy + (1 - phase) * fy)
    return (a.astype(np.float32) * (1 - phase) + b.astype(np.float32) * phase)
def psnr(a, b):
    mse = np.mean((a.astype(np.float32) - b.astype(np.float32)) ** 2); return 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)
cfgs = {
  "gpu-now (lod3,2,1 r2 int)":      dict(),
  "+penalty .08":                   dict(pens={3: .08, 2: .08, 1: .08, 0: .08}),
  "+subpixel":                      dict(pens={3: .08, 2: .08, 1: .08, 0: .08}, subpix=True),
  "+lod0 refine":                   dict(pens={3: .08, 2: .08, 1: .08, 0: .08}, subpix=True, final_lod0=True),
  "+median":                        dict(pens={3: .08, 2: .08, 1: .08, 0: .08}, subpix=True, final_lod0=True, median=True),
}
which = sys.argv[3].split(",") if len(sys.argv) > 3 else list(cfgs)
idx = list(range(10, len(imgs) - 1, int(sys.argv[2]) if len(sys.argv) > 2 else 20))
for name in which:
    cfg = cfgs[name]; sc_blend, sc_flow, tt = [], [], 0
    for t in idx:
        A, C, B = imgs[t - 1], imgs[t], imgs[t + 1]
        t0 = time.time(); d, lod = estimate(A, B, **cfg); tt += time.time() - t0
        P = interpolate(A, B, d, lod).clip(0, 255).astype(np.uint8)
        sc_flow.append(psnr(P, C)); sc_blend.append(psnr(((A.astype(np.uint16) + B) // 2).astype(np.uint8), C))
    print(f"{name:30} warp {np.mean(sc_flow):5.2f}  blend {np.mean(sc_blend):5.2f}  gain {np.mean(sc_flow)-np.mean(sc_blend):+.2f} dB   ({tt/len(idx)*1000:.0f} ms/frame cpu)")

if len(sys.argv) > 4 and sys.argv[4] == "dis":
    dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    sc = {"dis-medium": [], "dis-fast": [], "dis-lowres(1/2)": []}
    dfast = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_FAST)
    for t in idx:
        A, C, B = imgs[t - 1], imgs[t], imgs[t + 1]
        ga, gb = (luma(A) * 255).astype(np.uint8), (luma(B) * 255).astype(np.uint8)
        f = dis.calc(ga, gb, None); P = interpolate(A, B, f, 0).clip(0, 255).astype(np.uint8); sc["dis-medium"].append(psnr(P, C))
        f = dfast.calc(ga, gb, None); P = interpolate(A, B, f, 0).clip(0, 255).astype(np.uint8); sc["dis-fast"].append(psnr(P, C))
        sa, sb = cv2.resize(ga, (W // 2, H // 2), interpolation=cv2.INTER_AREA), cv2.resize(gb, (W // 2, H // 2), interpolation=cv2.INTER_AREA)
        f = dis.calc(sa, sb, None); P = interpolate(A, B, f, 1).clip(0, 255).astype(np.uint8); sc["dis-lowres(1/2)"].append(psnr(P, C))
    for k, v in sc.items(): print(f"{k:30} warp {np.mean(v):5.2f}")
