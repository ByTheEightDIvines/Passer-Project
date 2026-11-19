"""Where hair, skin and gear sit in a traveller body's texture atlas.

Each texel is labelled by the body part painted with it (the joint its triangle hangs from: head,
neck, hands, arms, the rest) and its colour: skin colours on the head, neck, hands and arms are
skin; the rest of the head is hair (or a hat: per-body corrections in HEADWEAR); everything else is
gear (clothes, leather, metal). Used by recolor.py.
"""
import sys, numpy as np, colorsys
sys.path.insert(0, '/home/claude/w/anim')
from bmd import BMD, joint_names

PART = {'head': 1, 'neck': 2, 'handL': 3, 'handR': 3, 'armL1': 4, 'armL2': 4, 'armR1': 4, 'armR2': 4}

def load(bmd_bytes):
    m = BMD(bmd_bytes)
    m.jworld = [m.world(i) for i in range(len(m.jloc))]
    m.read_shp()
    names = joint_names(m)
    return m, names

def body_texture(m):
    """(texture index, image) of the body's main material: the largest texture its triangles use."""
    from collections import Counter
    c = Counter(m.mats[t[0]][0] for t in m.tris)
    idx = max(c, key=lambda i: (m.tex[i].shape[0] * m.tex[i].shape[1], c[i]))
    return idx, np.asarray(m.tex[idx], np.uint8)

def part_map(m, names, tex_idx, shape):
    """per texel: 0 none, 1 head, 2 neck, 3 hands, 4 arms, 5 the rest"""
    H, W = shape[:2]
    out = np.zeros((H, W), np.uint8)
    for (mi, P, UV), J in zip(m.tris, m.trij):
        if m.mats[mi][0] != tex_idx:
            continue
        j = max(set(J), key=list(J).count)
        lab = PART.get(names[j], 5)
        uv = np.array(UV, float) % 1.0 if np.abs(UV).max() > 1.0001 else np.array(UV, float)
        xs, ys = uv[:, 0] * W, uv[:, 1] * H
        x0, x1 = int(max(0, np.floor(xs.min()))), int(min(W - 1, np.ceil(xs.max())))
        y0, y1 = int(max(0, np.floor(ys.min()))), int(min(H - 1, np.ceil(ys.max())))
        (ax, ay), (bx, by), (cx, cy) = zip(xs, ys)
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l1 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / d
        l2 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / d
        inside = (l1 >= -0.02) & (l2 >= -0.02) & (1 - l1 - l2 >= -0.02)
        sub = out[y0:y1 + 1, x0:x1 + 1]
        # a part's own label wins over "the rest" where islands touch
        sub[inside & ((sub == 0) | (sub == 5))] = lab
    return out

def hsv(img):
    f = img[..., :3].astype(np.float32) / 255.0
    mx, mn = f.max(-1), f.min(-1)
    v = mx
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    d = np.maximum(mx - mn, 1e-6)
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
    return h, s, v

def skin_like(img):
    h, s, v = hsv(img)
    return (h >= 4) & (h <= 42) & (s >= 0.16) & (s <= 0.62) & (v >= 0.38)

def classify(img, parts, headwear=False):
    """labels: 0 unused, 1 hair, 2 skin, 3 gear"""
    sk = skin_like(img)
    lab = np.zeros(parts.shape, np.uint8)
    used = parts > 0
    lab[used] = 3
    lab[used & np.isin(parts, [1, 2, 3, 4]) & sk] = 2
    if not headwear:
        lab[(parts == 1) & ~sk] = 1
    return lab

def fold(uv, wrap=(1, 1)):
    """a triangle's UVs brought into the 0..1 texture square the way the GPU samples them
    (repeat, mirrored repeat or clamp), keeping the triangle in one piece"""
    uv = np.array(uv, float).copy()
    for a in (0, 1):
        k = np.floor(uv[:, a].mean())
        if wrap[a] == 0: uv[:, a] = np.clip(uv[:, a], 0, 1); continue
        uv[:, a] -= k
        if wrap[a] == 2 and int(k) % 2: uv[:, a] = 1 - uv[:, a]
    return uv

def tri_uv(m, q):
    return fold(q[2], getattr(m.tex[m.mats[q[0]][0]], 'wrap', (1, 1)))

def tri_island(m, ids, q):
    Hh, Ww = ids.shape; uv = tri_uv(m, q).mean(0)
    return ids[int(np.clip(uv[1] * Hh, 0, Hh - 1)), int(np.clip(uv[0] * Ww, 0, Ww - 1))]

def same_textures(m, tex_idx):
    """the body texture is often stored twice (two materials); both copies count as the body"""
    ref = np.asarray(m.tex[tex_idx])
    return {k for k, T in enumerate(m.tex) if np.asarray(T).shape == ref.shape and np.array_equal(np.asarray(T), ref)}

def islands(m, names, tex_idx, shape):
    """UV islands of the body texture: per texel an island id (0 none), and per island its part
    (the joint most of its triangles hang from) and texel count."""
    H, W = shape[:2]
    same = same_textures(m, tex_idx)
    tris = [(i, t) for i, t in enumerate(m.tris) if m.mats[t[0]][0] in same]
    parent = list(range(len(tris)))
    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    key = {}
    for k, (i, t) in enumerate(tris):
        for uv in np.round(tri_uv(m, t) * 4096).astype(int):
            kk = (uv[0], uv[1])
            if kk in key:
                ra, rb = find(k), find(key[kk])
                if ra != rb: parent[ra] = rb
            else:
                key[kk] = k
    roots = {}
    ids = np.zeros((H, W), np.int32)
    part_votes = {}
    for k, (i, t) in enumerate(tris):
        r = find(k)
        isl = roots.setdefault(r, len(roots) + 1)
        J = m.trij[i]
        j = max(set(J), key=list(J).count)
        part_votes.setdefault(isl, []).append(PART.get(names[j], 5))
        uv = tri_uv(m, t)
        xs, ys = uv[:, 0] * W, uv[:, 1] * H
        x0, x1 = int(max(0, np.floor(xs.min()))), int(min(W - 1, np.ceil(xs.max())))
        y0, y1 = int(max(0, np.floor(ys.min()))), int(min(H - 1, np.ceil(ys.max())))
        (ax, ay), (bx, by), (cx, cy) = zip(xs, ys)
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-9 or x1 < x0 or y1 < y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l1 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / d
        l2 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / d
        inside = (l1 >= -0.03) & (l2 >= -0.03) & (1 - l1 - l2 >= -0.03)
        sub = ids[y0:y1 + 1, x0:x1 + 1]
        sub[inside & (sub == 0)] = isl
    # fill texels no triangle centre covered (thin slivers, edges) from their neighbours
    for _ in range(4):
        z = ids == 0
        if not z.any(): break
        p = np.pad(ids, 1)
        nb = np.stack([p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]])
        fill = nb.max(0)
        ids = np.where(z, fill, ids)
    part = {isl: max(set(v), key=v.count) for isl, v in part_votes.items()}
    return ids, part
