import cv2, numpy as np, glob, sys, time
exec(open(__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), 'interp_proto_blockmatch.py')).read().split("cfgs = {")[0].replace("files = sorted(glob.glob(sys.argv[1]", "files = sorted(glob.glob(sys.argv[1]"))
def lk_level(a, b, d, iters, win, lam, med, clampv=2.0):
    h, w = a.shape
    gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    for _ in range(iters):
        pa = sample(a, gx - 0.5 * d[..., 0], gy - 0.5 * d[..., 1]); pb = sample(b, gx + 0.5 * d[..., 0], gy + 0.5 * d[..., 1])
        # gradients of the average of the two warped images (central differences)
        m = 0.5 * (pa + pb)
        Ix = (np.roll(m, -1, 1) - np.roll(m, 1, 1)) * 0.5; Iy = (np.roll(m, -1, 0) - np.roll(m, 1, 0)) * 0.5
        It = pb - pa           # brightness change across the full displacement d: It ~ -(Ix,Iy).delta
        k = (win, win)
        Sxx = cv2.boxFilter(Ix * Ix, -1, k) + lam; Syy = cv2.boxFilter(Iy * Iy, -1, k) + lam; Sxy = cv2.boxFilter(Ix * Iy, -1, k)
        bx = cv2.boxFilter(Ix * It, -1, k); by = cv2.boxFilter(Iy * It, -1, k)
        det = Sxx * Syy - Sxy * Sxy
        dx = -(Syy * bx - Sxy * by) / det; dy = -(Sxx * by - Sxy * bx) / det
        d = d + np.stack([dx, dy], -1).clip(-clampv, clampv)
        if med: d = np.stack([cv2.medianBlur(d[..., c], 3) for c in range(2)], -1)
    return d
def lk_flow(A, B, levels=5, iters=4, win=9, lam=1e-3, med=True, upto=0):
    pa, pb = pyr(luma(A), levels), pyr(luma(B), levels)
    d = None
    for lod in range(levels - 1, upto - 1, -1):
        a, b = pa[lod], pb[lod]; h, w = a.shape
        d = np.zeros((h, w, 2), np.float32) if d is None else cv2.resize(d, (w, h), interpolation=cv2.INTER_LINEAR) * 2.0
        d = lk_level(a, b, d, iters, win, lam, med)
    return d, upto
idx = list(range(10, len(imgs) - 1, 20))
variants = {
  "lk L5 i4 w9 med":          dict(),
  "lk L5 i4 w9 nomed":        dict(med=False),
  "lk L5 i6 w7 med":          dict(iters=6, win=7),
  "lk L4 i4 w9 med":          dict(levels=4),
  "lk L5 i4 w13 med":         dict(win=13),
  "lk L5 i4 w9 med stop@1":   dict(upto=1),
}
for name, cfg in variants.items():
    sc = []; tt = 0
    for t in idx:
        A, C, B = imgs[t - 1], imgs[t], imgs[t + 1]
        t0 = time.time(); d, lod = lk_flow(A, B, **cfg); tt += time.time() - t0
        P = interpolate(A, B, d, lod).clip(0, 255).astype(np.uint8); sc.append(psnr(P, C))
    print(f"{name:28} warp {np.mean(sc):5.2f}   ({tt/len(idx)*1000:.0f} ms/frame cpu)", flush=True)
