"""Recoloured variants of traveller bodies: hair and gear (skin is left alone).

Per body (BODY): which UV islands (numbers from regions.islands) are the face (skin with hair,
brows and beards painted in), whole hair pieces, and extra skin (ears, hands, neck). Everything
else is gear. Per variant (VARIANTS): a hair colour and a natural dye for the clothes. Skin tone is never changed.
"""
import sys, numpy as np, colorsys
sys.path[:0] = ['/home/claude/w/anim', '/home/claude/pp/tools/looks']
import regions as R

BODY = {  # type: (arc, face islands, hair islands, skin islands)
    3:  ('P_MON_a',  [6],  [7, 12],               [2, 10, 11, 14, 15]),
    4:  ('P_MAN_b',  [2],  [12],                  [4, 7, 9, 10, 13]),
    6:  ('P_MAS_a',  [6],  [5],                   [9, 11]),
    7:  ('P_MBN_a',  [7],  [8, 9],                [10, 16, 20]),
    13: ('P_MAN_c2', [11, 18, 21], [16, 27],      [12, 13, 15, 17, 22, 23, 24]),
    14: ('P_MAS_a2', [2],  [7],                   [9, 10]),
    15: ('P_MBN_a2', [14], [16, 21],              [2, 3, 6, 20]),
    20: ('P_WON_a',  [2],  [6, 7],                [8, 12]),
    27: ('P_WON_a2', [2],  [3, 6, 7, 11],         [15, 16]),
    28: ('P_WGN_a2', [9],  [11, 13],      [5, 6, 7, 8]),
    29: ('P_WAN_b2', [10], [18, 19, 20],              [8, 12, 22, 23, 24]),
}
HAIR = {'black': '#211d1c', 'dark brown': '#3e2b1f', 'chestnut': '#573220', 'auburn': '#5a2618',
        'blond': '#a08452', 'iron grey': '#3e3c3b', 'white': '#8f8d89'}
# natural dyes only: (hue in degrees, saturation, brightness factor)
DYE = {'rust': (14, 0.45, 0.95), 'ochre': (38, 0.42, 1.0), 'olive': (62, 0.32, 0.95),
       'moss': (88, 0.26, 0.9), 'walnut': (24, 0.42, 0.72), 'oxblood': (356, 0.42, 0.8),
       'charcoal': (30, 0.06, 0.7), 'linen': (36, 0.16, 1.12)}
VARIANTS = {  # type: [(hair, dye for the main clothing colour)] — skin tone is never changed
    3:  [('iron grey', 'rust')],
    4:  [('black', 'rust'), ('auburn', 'ochre'), ('blond', 'walnut'), ('chestnut', 'oxblood'), ('black', 'charcoal')],
    6:  [('black', 'moss')],
    7:  [('blond', 'olive')],
    13: [('black', 'oxblood')],
    14: [('auburn', 'moss')],
    15: [('chestnut', 'olive')],
    20: [('black', 'moss')],
    27: [('white', 'rust')],
    28: [('dark brown', 'oxblood')],
    29: [('auburn', 'moss')],
}

def hexrgb(h): return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], float)

def chroma(rgb):
    s = rgb.sum(-1, keepdims=True) + 1e-3
    return rgb / s

KEEP = {4: [1], 7: [11, 13, 17], 13: [25], 20: [13], 27: [12], 29: [14], 28: [10, 12, 15, 16]}
# islands shared between gear and a beard/hair piece: only the triangles around the face are hair
NEAR = {}

def raster(tris, shape):
    H, W = shape[:2]; out = np.zeros((H, W), bool)
    for uv in tris:
        uv = np.array(uv, float); xs, ys = uv[:, 0] * W, uv[:, 1] * H
        x0, x1 = int(max(0, np.floor(xs.min()))), int(min(W - 1, np.ceil(xs.max())))
        y0, y1 = int(max(0, np.floor(ys.min()))), int(min(H - 1, np.ceil(ys.max())))
        (ax, ay), (bx, by), (cx, cy) = zip(xs, ys)
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(d) < 1e-9 or x1 < x0 or y1 < y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l1 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / d
        l2 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / d
        out[y0:y1 + 1, x0:x1 + 1] |= (l1 >= -0.05) & (l2 >= -0.05) & (1 - l1 - l2 >= -0.05)
    return out

def above(m, ti, ids, isl, ymin):
    """texels of islands `isl` drawn by triangles whose centre is above height ymin"""
    same = R.same_textures(m, ti)
    sel = [R.tri_uv(m, q) for q in m.tris if m.mats[q[0]][0] in same and R.tri_island(m, ids, q) in isl
           and np.array(q[1]).mean(0)[1] >= ymin]
    return raster(sel, ids.shape)

def body_keep(t, m, ti, ids):
    k = np.isin(ids, KEEP.get(t, []))
    if t in KEEP_ABOVE:
        isl, y = KEEP_ABOVE[t]; k |= above(m, ti, ids, isl, y) & np.isin(ids, isl)
    return k

def body_masks(t, m, ti, img, ids):
    arc, face, hair, skin = BODY[t]
    mix = MIX.get(t, ()); area = above(m, ti, ids, mix, MIX_YMIN[t]) if t in MIX_YMIN else None
    if t in MIX_XOUT:
        same = R.same_textures(m, ti)
        P = [(q, np.array(q[1]).mean(0)) for q in m.tris if m.mats[q[0]][0] in same]
        xm = max(abs(p[0]) for _, p in P)
        area = raster([R.tri_uv(m, q) for q, p in P if R.tri_island(m, ids, q) in mix and abs(p[0]) > xm * MIX_XOUT[t]], ids.shape)
    beard = near_face(m, ti, ids, face, NEAR.get(t, [])).astype(float)
    if t in HAIR_ABOVE:
        isl, y = HAIR_ABOVE[t]; beard = np.maximum(beard, (above(m, ti, ids, isl, y) & np.isin(ids, isl)).astype(float))
    if t in HAIR_SIDES:
        isl, y, x = HAIR_SIDES[t]; same = R.same_textures(m, ti)
        sel = [R.tri_uv(m, q) for q in m.tris if m.mats[q[0]][0] in same and R.tri_island(m, ids, q) in isl
               and np.array(q[1]).mean(0)[1] >= y and abs(np.array(q[1]).mean(0)[0]) >= x]
        beard = np.maximum(beard, (raster(sel, ids.shape) & np.isin(ids, isl)).astype(float))
    if t in BACKHAIR:
        same = R.same_textures(m, ti)
        ft = [q for q in m.tris if m.mats[q[0]][0] in same and R.tri_island(m, ids, q) in face]
        P = np.array([np.array(q[1]).mean(0) for q in ft]); zc = (P[:, 2].min() + P[:, 2].max()) / 2
        back = raster([R.tri_uv(m, q) for q, p in zip(ft, P) if p[2] < zc - 2], ids.shape)
        front = raster([R.tri_uv(m, q) for q, p in zip(ft, P) if p[2] >= zc - 2], ids.shape)
        beard = np.maximum(beard, (back & ~front).astype(float))
    hm, sm = masks(img, ids, face, hair, skin, beard, mix, area, HAIRMIX.get(t, ()))
    if t in SKIN_ABOVE:
        isl, y = SKIN_ABOVE[t]; nk = above(m, ti, ids, isl, y) & np.isin(ids, isl)
        sm = np.where(nk, 1.0, sm); hm = np.where(nk, 0.0, hm)
    return hm, sm

def near_face(m, ti, ids, face, near):
    """texels of the `near` islands drawn by triangles inside the face's bounding box"""
    if not near: return np.zeros(ids.shape, bool)
    Hh, Ww = ids.shape
    isl = lambda q: R.tri_island(m, ids, q)
    same = R.same_textures(m, ti); mine = [q for q in m.tris if m.mats[q[0]][0] in same]
    FP = np.array([q[1] for q in mine if isl(q) in face]).reshape(-1, 3)
    mn, mx = FP.min(0) - np.array([2, 14, 2]), FP.max(0) + 2   # beards hang below the face
    sel = [R.tri_uv(m, q) for q in mine if isl(q) in near and (np.array(q[1]).mean(0) > mn).all() and (np.array(q[1]).mean(0) < mx).all()]
    return raster(sel, ids.shape) & np.isin(ids, near)  # islands left exactly as they are (metal hats, trims)

MIX = {7: [3], 15: [5], 14: [11], 6: [1]}
MIX_XOUT = {14: 0.5, 6: 0.5}   # part-skin arm pieces: only past this share of the arm span (forearms, not sleeves)
MIX_YMIN = {15: 128, 7: 120}   # part-skin islands only count above this height (a cream shirt below the neckline)
HAIRMIX = {29: [21]}
BACKHAIR = {13}
HAIR_ABOVE = {13: ([18, 21], 186.5)}
HAIR_SIDES = {13: ([11], 178, 9)}   # (islands, above height, beyond |x|): hair over the temples, painted on the face piece
SKIN_ABOVE = {13: ([14], 151)}     # his neck is drawn from the vest piece above the collar   # the fringe piece also carries forehead skin: above this height it's all hair
KEEP_ABOVE = {28: ([1, 2], 92)}   # her blouse (with the bare neckline and back keyhole) stays as it is; only the skirt is dyed   # the face island wraps the back of the skull: its back half is hair   # a hair clasp and the hair around it share an island: saturated warm texels are hair
# islands that are part skin, part clothing (necklines, bare arms with sleeves)

def masks(img, ids, face, hair, skin, beard=None, mix=(), mixarea=None, hairmix=()):
    """soft per-texel weights (hair, skin) in 0..1; gear = the rest"""
    rgb = img[..., :3].astype(float)
    H = np.isin(ids, hair).astype(float)
    if beard is not None: H = np.maximum(H, beard)
    S = np.isin(ids, skin).astype(float)
    F = np.isin(ids, face); M = np.isin(ids, list(mix))
    if mixarea is not None: M &= mixarea
    c = chroma(rgb); v = rgb.max(-1) / 255
    sat = (rgb.max(-1) - rgb.min(-1)) / (rgb.max(-1) + 1e-3)
    # skin vs hair on the face: skin keeps a good share of blue (pinkish beige), brown/blond/red
    # hair has little; greys (white or grey beards) are hair by their low saturation
    sb = np.median(c[S > 0][:, 2]) if (S > 0).any() else 0.245
    if F.any(): sb = min(sb, np.percentile(c[F & (v > 0.25)][:, 2], 75))
    s = np.clip((c[..., 2] - (sb - 0.075)) / 0.05, 0, 1)
    s *= np.clip((sat - 0.14) / 0.1, 0, 1) * np.clip((v - 0.12) / 0.12, 0, 1)
    s *= np.clip((c[..., 0] - c[..., 1] - 0.02) / 0.03, 0, 1)   # skin is always redder than green; olive hair isn't
    from numpy.lib.stride_tricks import sliding_window_view as sw
    sp = np.pad(s, 1, mode='edge'); s = sw(sp, (3, 3)).mean((-1, -2))
    cool = c[..., 2] > c[..., 0] + 0.02          # blue/teal: headscarf etc, leave as gear
    if M.any():
        # skin in a clothing island must also sit close to the body's own skin colour
        fs = F & (s > 0.6)
        ref = np.median(c[fs], 0) if fs.sum() > 20 else np.median(c[S > 0], 0)
        sref = np.median(sat[fs]) if fs.sum() > 20 else np.median(sat[S > 0])
        vref = np.median(v[fs]) if fs.sum() > 20 else np.median(v[S > 0])
        near = np.exp(-(np.linalg.norm(c - ref, axis=-1) / 0.08) ** 2) * np.exp(-((sat - sref) / 0.15) ** 2)
        S = np.where(M, np.clip(near * 1.3, 0, 1) * np.clip((sat - 0.14) / 0.1, 0, 1), S)
    S = np.where(F, s, S); H = np.where(F, np.clip((0.55 - s) / 0.35, 0, 1) * (~cool), H)
    if beard is not None:
        H = np.maximum(H, beard); S = S * (1 - beard)
    HM = np.isin(ids, list(hairmix))
    if HM.any():
        warm = np.clip((sat - 0.12) / 0.08, 0, 1) * (c[..., 0] > c[..., 2] + 0.05)
        H = np.where(HM, warm, H)
    return np.clip(H, 0, 1), np.clip(S, 0, 1)

def apply(img, hm, sm, hair, dye, keep=None):
    rgb = img[..., :3].astype(float); out = rgb.copy()
    lum = rgb @ [0.299, 0.587, 0.114]
    # hair: keep the strands' light/dark pattern, take the new colour
    w = hm > 0.5
    if w.any():
        # the target is the colour of the hair's lit strands (its upper quartile); shadows keep
        # their depth, highlights can't blow out
        L = np.clip(lum / max(np.percentile(lum[w], 75), 1), 0, 1.2)
        t = hexrgb(HAIR[hair])
        nh = t[None, None] * L[..., None] ** 1.1
        g = nh.mean(-1, keepdims=True); nh = np.clip(g + (nh - g) * 0.8, 0, 255)
        out = out * (1 - hm[..., None]) + nh * hm[..., None]
    # skin is never recoloured: the mask only keeps gear shifts and hair off it
    # gear: the main clothing colour becomes a natural dye; greys, metal and other colours stay
    gm = np.clip(1 - hm - sm, 0, 1)
    flat = rgb.reshape(-1, 3) / 255
    hsv = np.array([colorsys.rgb_to_hsv(*p) for p in flat])
    sat = hsv[:, 1].reshape(rgb.shape[:2]); hh = hsv[:, 0].reshape(rgb.shape[:2])
    if keep is not None: gm = gm * ~keep
    sel = (gm > 0.5) & (sat > 0.2) & (hsv[:, 2].reshape(rgb.shape[:2]) > 0.15)
    hist = np.histogram(hh[sel], 36, (0, 1), weights=sat[sel])[0]
    hist = np.convolve(np.r_[hist[-1], hist, hist[0]], [1, 2, 1], 'valid')
    main = (np.argmax(hist) + 0.5) / 36
    dist = np.abs((hh - main + 0.5) % 1 - 0.5) * 360
    fam = np.clip((38 - dist) / 14, 0, 1)            # within ~25-38 degrees of the main cloth colour
    dh, ds, dv = DYE[dye]
    fsel = sel & (fam > 0.5)
    msat = np.median(sat[fsel]) if fsel.any() else 0.3
    H2 = hsv.copy()
    rel = ((hsv[:, 0] - main + 0.5) % 1 - 0.5) * 0.6          # keep a softened version of the colour variation
    H2[:, 0] = (dh / 360 + rel) % 1
    H2[:, 1] = np.clip(hsv[:, 1] * ds / max(msat, 0.05), 0, 0.55)
    H2[:, 2] = np.clip(hsv[:, 2] * dv, 0, 1)
    ng = np.array([colorsys.hsv_to_rgb(*p) for p in H2]).reshape(rgb.shape) * 255
    k = (gm * fam * np.clip((sat - 0.08) / 0.12, 0, 1))[..., None]
    out = out * (1 - k) + ng * k
    out = out * (1 - sm[..., None]) + rgb * sm[..., None]   # skin stays exactly the original
    if keep is not None: out[keep] = rgb[keep]
    res = img.copy(); res[..., :3] = np.clip(out, 0, 255).astype(np.uint8)
    return res
