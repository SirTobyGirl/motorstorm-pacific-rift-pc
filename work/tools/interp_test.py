# Offline test of frame interpolation on captured race frames (needs numpy and opencv-python-headless).
#   work/tools/capture_race.sh <dir> PS3RECOMP_FPS=60 PS3RECOMP_DT_MAX=1 PS3RECOMP_RES_SCALE=1   (every frame as f<n>.ppm)
#   python3 work/tools/interp_test.py <dir> <out dir>
# Predicts each frame from its two neighbours (a gap of two game steps: twice the motion of a real 60 -> 120 fps
# case, so a pessimistic test) by copying, blending and optical flow (OpenCV DIS), and prints PSNR against the frame
# the game really drew. PSNR is a poor judge here (the game's own motion blur makes real frames softer than their
# neighbours): also look at <out dir>/cmp_*.jpg (real | blend | flow).
import cv2, numpy as np, glob, time, sys
files = sorted(glob.glob(sys.argv[1] + "/f*.ppm"), key=lambda p: int(''.join(c for c in p.split('/')[-1] if c.isdigit())))
imgs = [cv2.imread(f) for f in files]
print(len(imgs), "frames", imgs[0].shape)
def psnr(a, b):
    mse = np.mean((a.astype(np.float32) - b.astype(np.float32)) ** 2)
    return 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)
dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
H, W = imgs[0].shape[:2]
gx, gy = np.meshgrid(np.arange(W, dtype=np.float32), np.arange(H, dtype=np.float32))
def warp(img, fx, fy, s):   # sample img at x + s*flow
    return cv2.remap(img, gx + s * fx, gy + s * fy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
res = {"copy": [], "blend": [], "flow": [], "flow+occ": []}; tms = []
for t in range(1, len(imgs) - 1):
    a, c, b = imgs[t - 1], imgs[t], imgs[t + 1]          # predict c from a and b
    ga, gb = cv2.cvtColor(a, cv2.COLOR_BGR2GRAY), cv2.cvtColor(b, cv2.COLOR_BGR2GRAY)
    t0 = time.time()
    f_ab = dis.calc(ga, gb, None)                        # a -> b
    f_ba = dis.calc(gb, ga, None)                        # b -> a
    tms.append(time.time() - t0)
    # mid pixel x: take a at x - 0.5*F, b at x + 0.5*F, using the flow at x (approximation at motion edges)
    fx, fy = f_ab[..., 0], f_ab[..., 1]
    wa = warp(a, fx, fy, -0.5); wb = warp(b, fx, fy, +0.5)
    flow_img = (0.5 * wa.astype(np.float32) + 0.5 * wb.astype(np.float32)).astype(np.uint8)
    # occlusion-aware: also use the reverse flow, then per pixel pick the pair that agrees better
    rx, ry = f_ba[..., 0], f_ba[..., 1]
    wa2 = warp(a, -rx, -ry, +0.5); wb2 = warp(b, -rx, -ry, -0.5)
    e1 = np.abs(wa.astype(np.float32) - wb.astype(np.float32)).sum(2)
    e2 = np.abs(wa2.astype(np.float32) - wb2.astype(np.float32)).sum(2)
    m = (e1 <= e2)[..., None]
    c1 = 0.5 * wa.astype(np.float32) + 0.5 * wb.astype(np.float32)
    c2 = 0.5 * wa2.astype(np.float32) + 0.5 * wb2.astype(np.float32)
    occ_img = np.where(m, c1, c2).astype(np.uint8)
    res["copy"].append(psnr(a, c)); res["blend"].append(psnr(((a.astype(np.uint16) + b) // 2).astype(np.uint8), c))
    res["flow"].append(psnr(flow_img, c)); res["flow+occ"].append(psnr(occ_img, c))
    if t in (60, 120, 180):
        row = np.hstack([c, ((a.astype(np.uint16) + b) // 2).astype(np.uint8), occ_img])
        cv2.imwrite(f"{sys.argv[2]}/cmp_{t}.jpg", cv2.resize(row, None, fx=0.6, fy=0.6), [cv2.IMWRITE_JPEG_QUALITY, 90])
print("mean PSNR dB (higher is better) predicting each frame from its two neighbours:")
for k, v in res.items(): print(f"  {k:9} mean {np.mean(v):5.2f}  worst {np.min(v):5.2f}  p10 {np.percentile(v,10):5.2f}")
print(f"flow cost on this CPU (two DIS flows, 720p): {np.mean(tms)*1000:.0f} ms/frame")
