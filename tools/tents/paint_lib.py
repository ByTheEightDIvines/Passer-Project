# ======================= texture painting =======================
A = np.zeros((S, S, 3), np.float32)
def region_px(name):
    x0, y0, x1, y1 = R[name]; return slice(y0, y1), slice(x0, x1), y1 - y0, x1 - x0
def smooth_noise(h, w, cells_y, cells_x, seed):
    r = np.random.default_rng(seed).random((cells_y + 3, cells_x + 3))
    big = cv2.resize(r, ((cells_x + 3) * 32, (cells_y + 3) * 32), interpolation=cv2.INTER_CUBIC)
    return cv2.resize(big[16:-48, 16:-48], (w, h), interpolation=cv2.INTER_LINEAR)
def fbm(h, w, seed, layers=((3, 0.4), (8, 0.3), (24, 0.2), (64, 0.1))):
    """layers: (cells across the width, weight)."""
    out = np.zeros((h, w), np.float32)
    for i, (c, wt) in enumerate(layers):
        out += wt * smooth_noise(h, w, max(1, int(c * h / w)), c, seed + 17 * i)
    return out

CANVAS = np.array([238, 230, 206], np.float32)   # unbleached cotton duck
DIRT = np.array([122, 104, 78], np.float32)

def duck_weave(h, w, seed, period=2.0):
    """Tightly woven cotton duck as it reads at game scale: individual threads are far too fine to
    draw, so it's a faint micro-weave plus the long irregular thread streaks (slubs) running both
    ways that make canvas look like canvas."""
    g = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    micro = 0.5 * (np.sin(xx * 2 * math.pi / period) * np.sin(yy * 2 * math.pi / period))
    # warp slubs: every pixel column is a bundle of threads with its own tone along its length
    warp = cv2.resize(g.normal(0, 1, (max(2, h // 40), w)).astype(np.float32), (w, h), interpolation=cv2.INTER_CUBIC)
    weft = cv2.resize(g.normal(0, 1, (h, max(2, w // 40))).astype(np.float32), (w, h), interpolation=cv2.INTER_CUBIC)
    # a few thicker slub threads
    thick_w = cv2.resize((g.random((max(2, h // 60), w)) > 0.985).astype(np.float32), (w, h), interpolation=cv2.INTER_LINEAR)
    thick_f = cv2.resize((g.random((h, max(2, w // 60))) > 0.985).astype(np.float32), (w, h), interpolation=cv2.INTER_LINEAR)
    fuzz = cv2.GaussianBlur(g.random((h, w)).astype(np.float32), (0, 0), 0.8) - 0.5
    return (1.0 + 0.018 * micro + 0.022 * warp + 0.022 * weft
            - 0.05 * thick_w - 0.05 * thick_f + 0.03 * fuzz)

def burlap_weave(h, w, seed, period=6.0):
    """Coarse jute burlap: thick, uneven over/under threads with visible gaps, some threads
    thinner or thicker than their neighbours, and loose fibres."""
    g = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    wob_x = 0.45 * (smooth_noise(h, w, 4, 30, seed + 1) - 0.5)
    wob_y = 0.45 * (smooth_noise(h, w, 30, 4, seed + 2) - 0.5)
    fx = xx / period + wob_x; fy = yy / period + wob_y
    ix, iy = np.floor(fx).astype(int), np.floor(fy).astype(int)
    px, py = fx - ix, fy - iy
    nw, nh = ix.max() + 3, iy.max() + 3
    ix = np.clip(ix, 0, nw - 1); iy = np.clip(iy, 0, nh - 1)
    # every thread has its own thickness and tone, drifting along its length
    warp_th = cv2.resize(g.uniform(0.55, 0.95, (6, nw)).astype(np.float32), (nw, h), interpolation=cv2.INTER_LINEAR)
    weft_th = cv2.resize(g.uniform(0.55, 0.95, (nh, 6)).astype(np.float32), (w, nh), interpolation=cv2.INTER_LINEAR)
    warp_tn = cv2.resize(g.normal(0, 1, (6, nw)).astype(np.float32), (nw, h), interpolation=cv2.INTER_LINEAR)
    weft_tn = cv2.resize(g.normal(0, 1, (nh, 6)).astype(np.float32), (w, nh), interpolation=cv2.INTER_LINEAR)
    Y = yy.astype(int); X = xx.astype(int)
    wth = warp_th[Y, ix]; fth = weft_th[iy, X]
    wtn = warp_tn[Y, ix]; ftn = weft_tn[iy, X]
    in_warp = np.abs(px - 0.5) < wth / 2
    in_weft = np.abs(py - 0.5) < fth / 2
    over_warp = ((ix + iy) % 2 == 0)
    # thread crown shading across its width, and dipping where it passes under the other thread
    warp_c = np.cos((px - 0.5) / np.maximum(wth, 0.1) * math.pi) * (0.75 + 0.25 * np.sin(py * math.pi))
    weft_c = np.cos((py - 0.5) / np.maximum(fth, 0.1) * math.pi) * (0.75 + 0.25 * np.sin(px * math.pi))
    top_is_warp = np.where(in_warp & in_weft, over_warp, in_warp)
    crown = np.where(top_is_warp, warp_c, weft_c)
    tone = np.where(top_is_warp, wtn, ftn)
    thread = in_warp | in_weft
    v = np.where(thread, 0.80 + 0.22 * np.clip(crown, 0, 1) + 0.05 * tone, 0.52)
    # twist ridges along each thread
    twist = np.where(top_is_warp, np.sin((yy * 1.6 + xx * 0.6)), np.sin((xx * 1.6 + yy * 0.6)))
    v += 0.03 * twist * thread
    # loose fibres lying across the surface
    fib = np.zeros((h, w), np.float32)
    for _ in range(int(h * w / 900)):
        x0, y0 = g.uniform(0, w), g.uniform(0, h); ang = g.uniform(0, math.pi); ln = g.uniform(4, 14)
        x1, y1 = x0 + ln * math.cos(ang), y0 + ln * math.sin(ang)
        cv2.line(fib, (int(x0), int(y0)), (int(x1), int(y1)), 1.0, 1, cv2.LINE_AA)
    v = v * (1 - 0.10 * fib) + 0.06 * fib
    return cv2.GaussianBlur(v.astype(np.float32), (0, 0), 0.45)

WEAVE = burlap_weave
import os
if os.environ.get('CLOTH','cream')=='jute':
    CANVAS = np.array([196, 168, 120], np.float32)

def canvas_panel(name, seed, length_u_is_t=True, mirror=False, stake_us=(), guy_u=(), seams=4, dirt=True):
    ys, xs, h, w = region_px(name)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u = xx / w; v = yy / h
    weave = WEAVE(h, w, seed)
    mott = fbm(h, w, seed + 5)
    img = CANVAS[None, None] * (weave * (0.94 + 0.1 * (mott - 0.5)))[..., None]
    # slight yellowing in the middle, bleached toward the ridge where the sun hits hardest
    img *= (1.03 - 0.05 * v)[..., None]
    img += (np.array([4, 2, -6], np.float32) * (mott - 0.4)[..., None])

    shade = np.ones((h, w), np.float32)
    # ambient occlusion: darker toward the ground and in the fold right under the ridge roll
    shade -= 0.16 * np.clip((v - 0.72) / 0.28, 0, 1) ** 1.6
    shade -= 0.12 * np.exp(-v / 0.035)
    # cloth sags between the poles: the middle of the panel sits a touch deeper
    shade -= 0.05 * np.sin(math.pi * v) * np.sin(math.pi * u) ** 1.5
    # tension creases fanning up from each stake loop: a soft valley with a lit ridge beside it
    for us in stake_us:
        for ang in (-0.42, -0.15, 0.15, 0.42):
            # distance from the line leaving (us, 1) at angle ang (u per v)
            du = (u - (us + ang * (1 - v) * (h / w)))
            d = du * w
            reach = np.clip(1 - (1 - v) / 0.55, 0, 1) ** 1.3
            shade -= 0.07 * np.exp(-(d / 3.5) ** 2) * reach
            shade += 0.04 * np.exp(-((d - 5) / 3.5) ** 2) * reach
    # guy ropes pull the ridge ends: creases running down-diagonally from the top corners
    for ug in guy_u:
        for ang in (0.5, 0.9):
            sgn = 1 if ug < 0.5 else -1
            d = ((u - ug) - sgn * ang * v * (h / w)) * w
            reach = np.clip(1 - v / 0.45, 0, 1)
            shade -= 0.06 * np.exp(-(d / 4) ** 2) * reach
            shade += 0.03 * np.exp(-((d + 5 * sgn) / 4) ** 2) * reach
    # flat-felled panel seams: a raised double layer with a stitch line each side and a shadow
    for k in range(1, seams):
        sx = w * k / seams + np.random.default_rng(seed + k).uniform(-5, 5)
        d = xx - sx
        shade += 0.05 * np.exp(-((d - 1.5) / 2.0) ** 2)          # lit raised edge
        shade -= 0.10 * np.exp(-((d + 3.5) / 1.6) ** 2)          # shadow under the fold
        stitches = (((yy + k * 2) // 3) % 2 == 0)
        shade -= 0.12 * stitches * (np.exp(-((d - 3.5) / 0.6) ** 2) + np.exp(-((d + 1.5) / 0.6) ** 2))
    img *= np.clip(shade, 0.5, 1.2)[..., None]
    if dirt:
        edge = 0.76 + 0.07 * (fbm(h, w, seed + 9, layers=((6, 0.6), (20, 0.4))) - 0.5) * 2
        d = np.clip((v - edge) / 0.2, 0, 1) ** 1.1 * 0.9
        streak = cv2.resize(np.random.default_rng(seed + 13).random((10, max(2, w // 5))).astype(np.float32), (w, h), interpolation=cv2.INTER_CUBIC)
        d += 0.4 * np.clip((streak - 0.5) * 2.5, 0, 1) * np.clip((v - 0.58) / 0.4, 0, 1) ** 1.4
        green = np.clip((v - 0.93) / 0.07, 0, 1) * 0.25          # a faint grass stain right at the hem
        d = np.clip(d, 0, 1)[..., None]
        img = img * (1 - d) + DIRT * (0.85 + 0.3 * (mott[..., None] - 0.5)) * weave[..., None] * d
        img = img * (1 - green[..., None]) + np.array([108, 116, 72], np.float32) * weave[..., None] * green[..., None]
    A[ys, xs] = img

stake_ts = [0.12 + 0.76 * i / 3 for i in range(4)]
canvas_panel('sideL', 101, stake_us=stake_ts, guy_u=(0.0, 1.0))
canvas_panel('sideR', 202, stake_us=[1 - t for t in stake_ts], guy_u=(0.0, 1.0))
canvas_panel('back', 303, stake_us=(), guy_u=(), seams=4)
# the back gable: AO along its sloped edges where it meets the sides
ys, xs, h, w = region_px('back')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
uu = xx / w - 0.5; vv = yy / h
edge_d = (vv * 0.5 - np.abs(uu)) * w      # distance (px) in from the sloped edge
A[ys, xs] *= (1 - 0.14 * np.exp(-np.clip(edge_d, 0, None) / 10))[..., None]

# interior: the same cloth seen from inside, daylight glowing through; doubled seams block it
ys, xs, h, w = region_px('inner')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
weave = WEAVE(h, w, 404)
glow = 0.62 + 0.10 * (1 - yy / h) + 0.05 * (fbm(h, w, 405) - 0.5)
inner = (CANVAS * np.array([0.93, 0.89, 0.81], np.float32)) * (weave * glow)[..., None]
for k in range(1, 4):
    d = xx - w * k / 4
    inner *= (1 - 0.22 * np.exp(-(d / 4.0) ** 2))[..., None]
inner *= (1 - 0.25 * np.clip((yy / h - 0.75) / 0.25, 0, 1))[..., None]
A[ys, xs] = inner

# rolled door flaps: canvas wound on itself; deep shadow in the spiral grooves, worn bright crowns
ys, xs, h, w = region_px('roll')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
weave = WEAVE(h, w, 505)
ph = (yy / h * 2 + xx / w * 5) % 1.0
crown = np.sin(ph * math.pi) ** 0.6
img = CANVAS * (weave * (0.72 + 0.3 * crown))[..., None]
img *= (1 - 0.15 * np.clip((xx / w - 0.8) / 0.2, 0, 1))[..., None]
A[ys, xs] = img

# rolled hem (dirty) and the ridge roll (clean, bleached)
for name, dirty, seed in (('hem', 0.5, 606), ('ridge', 0.0, 616)):
    ys, xs, h, w = region_px(name)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    weave = WEAVE(h, w, seed, period=5.0)
    around = 0.8 + 0.2 * np.sin(math.pi * yy / h)
    img = CANVAS * (weave * around)[..., None] * (1.02 if not dirty else 1.0)
    if dirty:
        m = fbm(h, w, seed + 1, layers=((10, 0.6), (30, 0.4)))
        img = img * (1 - dirty) + DIRT * (weave * (0.8 + 0.4 * m))[..., None] * dirty
    stitch = (((xx // 4) % 2) == 0) * np.exp(-((yy - h * 0.25) / 0.7) ** 2)
    A[ys, xs] = img * (1 - 0.18 * stitch)[..., None]

# wood: weathered, split-and-shaved ash poles; long wavy grain, a couple of knots, greyed surface
def wood(name, seed, base, tile_len_px_ratio=1.0, muddy_end=False):
    ys, xs, h, w = region_px(name)
    g = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    warp = 6 * (smooth_noise(h, w, 3, 6, seed + 1) - 0.5) + 2 * (smooth_noise(h, w, 8, 20, seed + 2) - 0.5)
    yk = yy + warp
    # knots push the grain around them
    knots = []
    for _ in range(2):
        kx, ky, kr = g.uniform(0.15, 0.85) * w, g.uniform(0.2, 0.8) * h, g.uniform(4, 7)
        knots.append((kx, ky, kr))
        dist = np.hypot((xx - kx) / 3.2, yy - ky)
        yk += 10 * np.exp(-(dist / (kr * 2.2)) ** 2) * np.sign(yy - ky)
    rings = np.sin(yk * 0.9) * 0.5 + 0.5
    fine = np.sin(yk * 3.1 + 2 * smooth_noise(h, w, 4, 30, seed + 3)) * 0.5 + 0.5
    streak = cv2.resize(g.random((h, 6)).astype(np.float32), (w, h), interpolation=cv2.INTER_CUBIC)
    tone = 0.78 + 0.12 * rings + 0.06 * fine + 0.12 * (streak - 0.5)
    img = np.array(base, np.float32) * tone[..., None]
    grey = np.array([142, 134, 120], np.float32)
    weather = 0.35 + 0.25 * fbm(h, w, seed + 4, layers=((4, 0.6), (16, 0.4)))
    img = img * (1 - weather[..., None]) + grey * tone[..., None] * weather[..., None]
    for kx, ky, kr in knots:
        dist = np.hypot((xx - kx) / 1.8, yy - ky)
        img *= (1 - 0.55 * np.exp(-(dist / kr) ** 2))[..., None]
        img *= (1 - 0.18 * np.exp(-((dist - kr * 1.1) / 1.2) ** 2))[..., None]
    # checks / small splits along the grain
    for _ in range(5):
        cy, cx0, ln = g.uniform(0, h), g.uniform(0, w), g.uniform(30, 120)
        d = np.abs(yk - cy)
        img *= (1 - 0.35 * np.exp(-(d / 0.6) ** 2) * ((xx > cx0) & (xx < cx0 + ln)))[..., None]
    if muddy_end:
        m = np.clip((0.3 - xx / w) / 0.3, 0, 1) ** 0.8
        m = np.clip(m + 0.15 * (fbm(h, w, seed + 5) - 0.5), 0, 1)
        img = img * (1 - m[..., None]) + DIRT * 0.8 * tone[..., None] * m[..., None]
    A[ys, xs] = img
wood('wood', 707, (150, 112, 74))
wood('stake', 808, (164, 128, 88), muddy_end=True)

# rope: three-strand twisted hemp, fibres running along each strand, dark grooves between
ys, xs, h, w = region_px('rope')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
strands = 3; turns = 4.0                       # strand passes per tile
ph = (yy / h * strands + xx / w * turns * strands / 3) % 1.0
prof = np.sin(ph * math.pi)                    # round strand cross-section
fib_coord = (xx / w * 60 - yy / h * 9)         # fibres follow the strand direction
fibres = 0.5 + 0.5 * np.sin(fib_coord * 2 * math.pi + 3 * smooth_noise(h, w, 4, 40, 901))
noise = smooth_noise(h, w, 8, 80, 902)
tone = 0.32 + 0.66 * prof ** 0.55 + 0.10 * (fibres - 0.5) * prof + 0.06 * (noise - 0.5)
A[ys, xs] = np.array([186, 158, 110], np.float32) * tone[..., None]

# gum blanket: rubberised cloth, black-brown with a soft sheen worn grey at the folds, brass grommets
ys, xs, h, w = region_px('blanket')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
weave = duck_weave(h, w, 909, period=3.0)
m = fbm(h, w, 910)
img = np.array([50, 45, 38], np.float32) * (0.8 + 0.4 * m)[..., None] * (0.9 + 0.2 * (weave[..., None] - 0.9))
crease = np.exp(-((xx - w * 0.5) / 4) ** 2) * 22 + np.exp(-((yy - h * 0.48) / 4) ** 2) * 16
img += crease[..., None]
for cx in (9, w - 9):
    for cy in np.linspace(12, h - 12, 4):
        r = np.hypot(xx - cx, yy - cy)
        ring = (r > 2.2) & (r < 4.2)
        img = np.where(ring[..., None], np.array([168, 140, 82], np.float32), img)
        img = np.where((r <= 2.2)[..., None], np.array([30, 26, 22], np.float32), img)
A[ys, xs] = img

A = np.clip(A, 0, 255).astype(np.uint8)
cv2.imwrite(f'{OUT}/tent_canvas_HD_1024.png', cv2.cvtColor(A, cv2.COLOR_RGB2BGR))
small = cv2.resize(A, (256, 256), interpolation=cv2.INTER_AREA)
cv2.imwrite(f'{OUT}/tent_canvas_256.png', cv2.cvtColor(small, cv2.COLOR_RGB2BGR))
print('textures written')
