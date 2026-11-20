import sys, math, numpy as np
sys.path.insert(0, '/home/claude/scripts'); import bmdread
from PIL import Image, ImageDraw
def render(pos, nrm, tris, yaw, W=420, H=360, pitch=18, light=(-0.5, 0.75, 0.45), extra=None):
    P = np.array([[pos[v[9]] for v in t] for t in tris], float)
    Nv = np.array([[nrm[v[10]] for v in t] for t in tris], float)
    if extra is not None:
        P = np.concatenate([P, extra[0]]); Nv = np.concatenate([Nv, extra[1]])
    a = math.radians(yaw); b = math.radians(pitch)
    Ry = np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])
    Rx = np.array([[1, 0, 0], [0, math.cos(b), -math.sin(b)], [0, math.sin(b), math.cos(b)]])
    R = Rx @ Ry
    Q = P.reshape(-1, 3) @ R.T
    mn, mx = Q.min(0), Q.max(0); s = 0.88 * min(W / (mx[0] - mn[0]), H / (mx[1] - mn[1]))
    X = (Q[:, 0] - (mn[0] + mx[0]) / 2) * s + W / 2; Y = H / 2 - (Q[:, 1] - (mn[1] + mx[1]) / 2) * s; Z = Q[:, 2]
    X = X.reshape(-1, 3); Y = Y.reshape(-1, 3); Z = Z.reshape(-1, 3)
    L = np.array(light, float); L /= np.linalg.norm(L)
    zb = np.full((H, W), -1e9); img = np.zeros((H, W, 3)); img[:] = (0.55, 0.62, 0.70)
    cam = np.array([0, 0, 1.0]) @ R   # view direction in model space (towards viewer)
    for i in range(len(X)):
        x0, x1 = int(max(0, np.floor(X[i].min()))), int(min(W - 1, np.ceil(X[i].max())))
        y0, y1 = int(max(0, np.floor(Y[i].min()))), int(min(H - 1, np.ceil(Y[i].max())))
        if x1 < x0 or y1 < y0: continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        (ax, bx, cx), (ay, by, cy) = X[i], Y[i]
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9: continue
        w0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
        w1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
        w2 = 1 - w0 - w1
        m = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not m.any(): continue
        z = w0 * Z[i, 0] + w1 * Z[i, 1] + w2 * Z[i, 2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]; m &= z > sub
        if not m.any(): continue
        n = w0[..., None] * Nv[i, 0] + w1[..., None] * Nv[i, 1] + w2[..., None] * Nv[i, 2]
        n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-9)
        lam = np.clip(n @ L, 0, 1); shade = 0.3 + 0.7 * lam
        col = np.stack([shade * 0.92, shade * 0.86, shade * 0.74], -1)
        sub[m] = z[m]; img[y0:y1 + 1, x0:x1 + 1][m] = col[m]
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
def sheet(models, yaws, out, titles):
    rows = []
    for (pos, nrm, tris, extra), title in zip(models, titles):
        ims = [render(pos, nrm, tris, y, extra=extra) for y in yaws]
        row = Image.new('RGB', (420 * len(ims), 384), (30, 30, 30))
        for k, im in enumerate(ims): row.paste(im, (420 * k, 24))
        ImageDraw.Draw(row).text((8, 6), title, fill=(255, 255, 255))
        rows.append(row)
    W = max(r.width for r in rows); full = Image.new('RGB', (W, 384 * len(rows)), (30, 30, 30))
    for k, r in enumerate(rows): full.paste(r, (0, 384 * k))
    full.save(out)
