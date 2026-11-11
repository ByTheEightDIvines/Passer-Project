"""Civil War wall tent (the big camp tent): short straight walls, peaked roof that overhangs the
walls and the gable ends, doorway in the front gable with both flaps rolled back and tied.
Same burlap/rope/wood materials as the A-tent (paint_lib.py).
Usage: CLOTH=cream python3 -I wall_tent.py OUT_DIR"""
import sys, math, pickle, os, numpy as np, cv2
OUT = sys.argv[1]
rng = np.random.default_rng(11)

Wt, Lt = 380.0, 480.0        # footprint (x by z)
Hw, Hr = 125.0, 300.0        # wall height, ridge height
EAVE = 16.0                  # roof overhang past the walls
ENDS = 12.0                  # roof overhang past the gable ends
DOOR = 72.0                  # half-width of the doorway at the ground
TIP = 22.0
ZF, ZB = Lt / 2, -Lt / 2

# 2048 atlas laid out so every cloth panel gets ~2 texels per game unit, the same density as the
# A-tent's 1024 sheet; the burlap weave then has the same thread size on both tents.
S = 2048
R = {
    'sideL':  (0,    0,    1024, 512),   # roof, left
    'sideR':  (1024, 0,    2048, 512),   # roof, right
    'wall':   (0,    512,  1024, 768),   # long walls (both use it)
    'back':   (0,    768,  768,  1408),  # gable ends (planar)
    'inner':  (768,  768,  1536, 1408),
    'blanket':(1536, 512,  2048, 1408),
    'roll':   (0,    1408, 1024, 1664),
    'hem':    (1024, 1408, 2048, 1536),
    'ridge':  (1024, 1536, 2048, 1664),
    'wood':   (0,    1664, 1024, 1920),
    'rope':   (1024, 1664, 2048, 1792),
    'stake':  (1024, 1792, 2048, 2048),
}
def uvmap(region, u, v):
    x0, y0, x1, y1 = R[region]; pad = 3
    return np.array([(x0 + pad + u * (x1 - x0 - 2 * pad)) / S, (y0 + pad + v * (y1 - y0 - 2 * pad)) / S])
TRIS = []
def tri(p, uv): TRIS.append((np.array(p, float), np.array(uv, float)))
def quad(p00, p10, p11, p01, uv00, uv10, uv11, uv01):
    tri([p00, p10, p11], [uv00, uv10, uv11]); tri([p00, p11, p01], [uv00, uv11, uv01])

# pull the tube helpers from the A-tent script (geometry part only)
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'a_tent.py')).read()
exec(src[src.index('def resample('):src.index('for sgn in (-1, 1):\n    hem =')])

# ---------------- roof ----------------
EAVE_Y = Hw + 4.0
def roof_point(sgn, t, s, inset=0.0):
    """t: 0 back .. 1 front (including the end overhangs), s: 0 ridge .. 1 eave."""
    z = ZB - ENDS + t * (Lt + 2 * ENDS)
    x = sgn * s * (Wt / 2 + EAVE)
    y = Hr + (EAVE_Y - Hr) * s
    sag = 8.0 * math.sin(math.pi * s) * (0.5 + 0.5 * math.sin(math.pi * t))
    nx, ny = sgn * (Hr - EAVE_Y), (Wt / 2 + EAVE)
    ln = math.hypot(nx, ny); nx, ny = nx / ln, ny / ln
    return np.array([x - nx * (sag + inset), y - ny * (sag + inset), z])
NZ, NV = 10, 6
for sgn, reg in ((-1, 'sideL'), (1, 'sideR')):
    uu = (lambda t: t) if sgn < 0 else (lambda t: 1 - t)
    for i in range(NZ):
        for j in range(NV):
            t0, t1, s0, s1 = i / NZ, (i + 1) / NZ, j / NV, (j + 1) / NV
            P = [roof_point(sgn, t0, s0), roof_point(sgn, t1, s0), roof_point(sgn, t1, s1), roof_point(sgn, t0, s1)]
            U = [uvmap(reg, uu(t0), s0), uvmap(reg, uu(t1), s0), uvmap(reg, uu(t1), s1), uvmap(reg, uu(t0), s1)]
            Pi = [roof_point(sgn, t0, s0, 2), roof_point(sgn, t1, s0, 2), roof_point(sgn, t1, s1, 2), roof_point(sgn, t0, s1, 2)]
            Ui = [uvmap('inner', t0, s0 * 0.5), uvmap('inner', t1, s0 * 0.5), uvmap('inner', t1, s1 * 0.5), uvmap('inner', t0, s1 * 0.5)]
            if sgn > 0: P, U = P[::-1], U[::-1]
            else: Pi, Ui = Pi[::-1], Ui[::-1]
            quad(*P, *U); quad(*Pi, *Ui)
    # rolled edges: along the eave and down both roof ends
    tube([roof_point(sgn, i / 14, 1.0) + np.array([0, -1.5, 0]) for i in range(15)], 2.6, 'hem', sides=6)
    for t in (0.0, 1.0):
        tube([roof_point(sgn, t, j / 8) for j in range(9)], 2.4, 'ridge', sides=6)
tube([np.array([0, Hr + 1.5, ZB - ENDS - 4 + i * (Lt + 2 * ENDS + 8) / 12]) for i in range(13)], 5.0, 'ridge', sides=8)

# ---------------- long walls ----------------
def wall_point(sgn, t, s, inset=0.0):
    """t: 0 back .. 1 front, s: 0 top (under the eave) .. 1 ground."""
    z = ZB + t * Lt
    bulge = 4.0 * math.sin(math.pi * s) * math.sin(math.pi * t)
    flare = 5.0 * max(0, s - 0.85) / 0.15
    x = sgn * (Wt / 2 + flare - bulge - inset)
    return np.array([x, Hw * (1 - s), z])
for sgn in (-1, 1):
    uu = (lambda t: t) if sgn < 0 else (lambda t: 1 - t)
    for i in range(NZ):
        for j in range(4):
            t0, t1, s0, s1 = i / NZ, (i + 1) / NZ, j / 4, (j + 1) / 4
            P = [wall_point(sgn, t0, s0), wall_point(sgn, t1, s0), wall_point(sgn, t1, s1), wall_point(sgn, t0, s1)]
            U = [uvmap('wall', uu(t0), s0), uvmap('wall', uu(t1), s0), uvmap('wall', uu(t1), s1), uvmap('wall', uu(t0), s1)]
            Pi = [wall_point(sgn, t0, s0, 2), wall_point(sgn, t1, s0, 2), wall_point(sgn, t1, s1, 2), wall_point(sgn, t0, s1, 2)]
            Ui = [uvmap('inner', t0, 0.5 + s0 * 0.5), uvmap('inner', t1, 0.5 + s0 * 0.5), uvmap('inner', t1, 0.5 + s1 * 0.5), uvmap('inner', t0, 0.5 + s1 * 0.5)]
            if sgn > 0: P, U = P[::-1], U[::-1]
            else: Pi, Ui = Pi[::-1], Ui[::-1]
            quad(*P, *U); quad(*Pi, *Ui)
    tube([wall_point(sgn, i / 14, 1.0) + np.array([0, 2.2, 0]) for i in range(15)], 2.6, 'hem', sides=6)

# ---------------- gable ends (planar, flat) ----------------
def gable_uv(p, reg='back'): return uvmap(reg, 0.5 + p[0] / Wt, 1 - p[1] / Hr)
def poly(points, z, inward, flip):
    """Fan-triangulate a convex outline at depth z, with an inner face offset inward."""
    pts = [np.array([x, y, z]) for x, y in points]
    c = sum(pts) / len(pts)
    for a, b in zip(pts, pts[1:] + pts[:1]):
        tr = [c, a, b] if not flip else [c, b, a]
        tri(tr, [gable_uv(p) for p in tr])
        ti = [p + np.array([0, 0, inward]) for p in tr[::-1]]
        tri(ti, [gable_uv(p, 'inner') - np.array([0, 0]) for p in tr[::-1]])
# back: full pentagon, split into strips so the fan centre doesn't smear the texture
back = [(-Wt / 2, 0), (Wt / 2, 0), (Wt / 2, Hw), (0, Hr), (-Wt / 2, Hw)]
poly(back, ZB, 2.0, flip=True)
# front: two pieces either side of the doorway (a slit from the ridge down to the ground)
poly([(-Wt / 2, 0), (-DOOR, 0), (0, Hr), (-Wt / 2, Hw)], ZF, -2.0, flip=False)
poly([(DOOR, 0), (Wt / 2, 0), (Wt / 2, Hw), (0, Hr)], ZF, -2.0, flip=False)
for z in (ZB, ZF):
    tube([np.array([-Wt / 2 + i * Wt / 10, 2.2, z]) for i in range(11)], 2.6, 'hem', sides=6)

# doorway flaps rolled back and tied, from the ridge down each side of the opening
for sgn in (-1, 1):
    pts = []
    for i in range(12):
        f = i / 11
        x = sgn * DOOR * f; y = Hr * (1 - f)
        pts.append(np.array([x + sgn * 6 * math.sin(math.pi * f), y, ZF + 4 + 5 * math.sin(math.pi * f)]))
    tube(pts, lambda f: 3.0 + 10.0 * math.sin(math.pi * min(1, f * 1.1)) ** 0.8, 'roll', sides=8, twist=3.0, cap=True)
    mid = pts[7]
    tube([mid + 15 * np.array([math.cos(a) * 0.9, math.sin(a), 0.5 * math.cos(a)]) for a in np.linspace(0, 2 * math.pi, 9)], 1.2, 'rope', sides=5, tile=24)
    # tie-back cord pegged out to the side
    peg = np.array([sgn * (DOOR + 70), 0, ZF + 40])
    tube([mid, (mid + peg) / 2 + np.array([0, 10, 0]), peg + np.array([0, 12, 0])], 0.9, 'rope', sides=5, tile=24)
    tube([peg - np.array([0, 6, 0]), peg + np.array([0, 16, 0])], lambda f: 2.6 - f, 'stake', sides=4, cap=True)

# ---------------- poles ----------------
for z in (ZF - 3, ZB + 4):
    tube([np.array([0, 0, z]), np.array([0, Hr + TIP, z])], 4.5, 'wood', sides=7, cap=True, tile=72)
    tube([np.array([0, Hr + TIP - 2, z]), np.array([0, Hr + TIP + 5, z])], 2.0, 'wood', sides=6, cap=True)

# ---------------- guy ropes and stakes ----------------
def stake(base, lean):
    top = base + np.array([0, 18, 0]) + lean * 4
    tube([base - np.array([0, 6, 0]), top], lambda f: 3.0 - 1.1 * (1 - f), 'stake', sides=4, cap=True)
    return top - np.array([0, 3, 0])
def rope(a, b, droop=4.0, n=7):
    tube([a + (b - a) * (i / (n - 1)) - np.array([0, droop * math.sin(math.pi * i / (n - 1)), 0]) for i in range(n)], 1.0, 'rope', sides=5, tile=24)
for sgn in (-1, 1):
    for k in range(5):                                   # eave guys
        t = 0.08 + 0.84 * k / 4
        e = roof_point(sgn, (t * Lt + ENDS) / (Lt + 2 * ENDS), 1.0) + np.array([0, -3, 0])
        top = stake(np.array([sgn * (Wt / 2 + 120), 0, e[2]]), np.array([sgn, 0, 0]))
        rope(e, top, droop=6)
    for k in range(7):                                   # wall hem loops
        t = 0.06 + 0.88 * k / 6
        h = wall_point(sgn, t, 1.0) + np.array([0, 3, 0])
        top = stake(np.array([h[0] + sgn * 24, 0, h[2]]), np.array([sgn, 0, 0]))
        rope(h, top, droop=1, n=3)
for z, sg in ((ZF - 3, 1), (ZB + 4, -1)):              # ridge guys
    tip = np.array([0, Hr + TIP - 6, z])
    for dx in (-40, 40):
        top = stake(np.array([dx, 0, z + sg * 210]), np.array([0, 0, sg]))
        rope(tip, top, droop=8, n=9)

# ---------------- floor ----------------
bx, bz0, bz1 = Wt / 2 - 30, ZB + 25, ZF - 60
quad(np.array([-bx, 0.8, bz1]), np.array([bx, 0.8, bz1]), np.array([bx, 0.8, bz0]), np.array([-bx, 0.8, bz0]),
     uvmap('blanket', 0, 1), uvmap('blanket', 1, 1), uvmap('blanket', 1, 0), uvmap('blanket', 0, 0))

print('triangles:', len(TRIS))
pickle.dump(TRIS, open(f'{OUT}/wall_tent_tris.pkl', 'wb'))

# ---------------- textures: shared painting, then the panels specific to this tent ----------------
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'paint_lib.py')).read().split("A = np.clip(A, 0, 255)")[0])
roof_guys = [0.08 + 0.84 * k / 4 for k in range(5)]
canvas_panel('sideL', 101, stake_us=roof_guys, guy_u=(), dirt=False, seams=6)
canvas_panel('sideR', 202, stake_us=[1 - t for t in roof_guys], guy_u=(), dirt=False, seams=6)
canvas_panel('wall', 151, stake_us=[0.06 + 0.88 * k / 6 for k in range(7)], guy_u=(), seams=8)
canvas_panel('back', 303, stake_us=(), guy_u=(), seams=5)
A = np.clip(A, 0, 255).astype(np.uint8)
cv2.imwrite(f'{OUT}/wall_tent_HD_2048.png', cv2.cvtColor(A, cv2.COLOR_RGB2BGR))
cv2.imwrite(f'{OUT}/wall_tent_512.png', cv2.cvtColor(cv2.resize(A, (512, 512), interpolation=cv2.INTER_AREA), cv2.COLOR_RGB2BGR))
print('textures written')
