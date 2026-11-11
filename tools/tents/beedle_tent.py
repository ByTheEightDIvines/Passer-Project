"""Beedle's tent: the wall-tent design, bigger, in Beedle's teal-and-cream stripes with a scalloped
valance and his face banner flying from the front pole.

The doorway opens onto a shallow vestibule (lit lantern, rug, a parted curtain with warm light
behind it) so the entrance reads as the way into a real room. Windows on the walls are oiled linen
panes. Everything that glows at night is also emitted as a separate GLOW mesh with its own small
texture, so the game can fade it in at dusk.

Usage: CLOTH=cream python3 -I beedle_tent.py OUT_DIR
Writes beedle_tent_tris.pkl (body), beedle_tent_glow_tris.pkl, beedle_tent_HD_2048.png,
beedle_tent_512.png, beedle_tent_glow_256.png.
"""
import sys, math, pickle, os, numpy as np, cv2
OUT = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
rng = np.random.default_rng(23)

Wt, Lt = 440.0, 560.0
Hw, Hr = 145.0, 345.0
EAVE, ENDS = 18.0, 14.0
DOOR = 82.0                   # half-width of the doorway at the ground
VD = 70.0                     # vestibule depth behind the door
TIP = 22.0
ZF, ZB = Lt / 2, -Lt / 2
STRIPE = 36.0                 # stripe width in game units (one scallop per stripe)

S = 2048
R = {
    'sideL':  (0,    0,    1024, 512),
    'sideR':  (1024, 0,    2048, 512),
    'wall':   (0,    512,  1024, 768),
    'back':   (0,    768,  768,  1408),
    'inner':  (768,  768,  1536, 1408),
    'banner': (1536, 512,  1792, 912),
    'curtain':(1792, 512,  2048, 1280),
    'gap':    (1536, 912,  1792, 1408),   # the room seen through the curtain (tall, like the opening)
    'pane':   (1792, 1280, 2048, 1408),
    'roll':   (0,    1408, 1024, 1664),
    'hem':    (1024, 1408, 2048, 1536),
    'ridge':  (1024, 1536, 2048, 1664),
    'wood':   (0,    1664, 1024, 1920),
    'rope':   (1024, 1664, 2048, 1792),
    'stake':  (1024, 1792, 2048, 2048),
    'valance':(0,    1920, 512,  2048),
    'blanket':(512,  1920, 1024, 2048),   # Beedle's rug (painted over the gum blanket)
}
G = 512   # glow texture
GR = {'pane': (0, 0, 256, 256), 'lamp': (0, 256, 256, 512), 'gap': (256, 0, 448, 512), 'bulb': (448, 0, 512, 64)}

def uvmap(region, u, v):
    x0, y0, x1, y1 = R[region]; pad = 3
    return np.array([(x0 + pad + u * (x1 - x0 - 2 * pad)) / S, (y0 + pad + v * (y1 - y0 - 2 * pad)) / S])
def guv(region, u, v):
    x0, y0, x1, y1 = GR[region]; pad = 2
    return np.array([(x0 + pad + u * (x1 - x0 - 2 * pad)) / G, (y0 + pad + v * (y1 - y0 - 2 * pad)) / G])

TRIS, GLOW = [], []
def tri(p, uv): TRIS.append((np.array(p, float), np.array(uv, float)))
def quad(p00, p10, p11, p01, uv00, uv10, uv11, uv01):
    tri([p00, p10, p11], [uv00, uv10, uv11]); tri([p00, p11, p01], [uv00, uv11, uv01])
def gquad(p00, p10, p11, p01, uv00, uv10, uv11, uv01):
    GLOW.append((np.array([p00, p10, p11], float), np.array([uv00, uv10, uv11], float)))
    GLOW.append((np.array([p00, p11, p01], float), np.array([uv00, uv11, uv01], float)))

src = open(os.path.join(HERE, 'a_tent.py')).read()
exec(src[src.index('def resample('):src.index('for sgn in (-1, 1):\n    hem =')])

# ---------------- roof ----------------
EAVE_Y = Hw + 4.0
def roof_point(sgn, t, s, inset=0.0):
    z = ZB - ENDS + t * (Lt + 2 * ENDS)
    x = sgn * s * (Wt / 2 + EAVE)
    y = Hr + (EAVE_Y - Hr) * s
    sag = 9.0 * math.sin(math.pi * s) * (0.5 + 0.5 * math.sin(math.pi * t))
    nx, ny = sgn * (Hr - EAVE_Y), (Wt / 2 + EAVE)
    ln = math.hypot(nx, ny); nx, ny = nx / ln, ny / ln
    return np.array([x - nx * (sag + inset), y - ny * (sag + inset), z])
NZ, NV = 12, 6
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
    tube([roof_point(sgn, t, 1.0) + np.array([0, -1.5, 0]) for t in np.linspace(0, 1, 17)], 2.8, 'hem', sides=6)
    for t in (0.0, 1.0):
        tube([roof_point(sgn, t, j / 8) for j in range(9)], 2.6, 'ridge', sides=6)
tube([np.array([0, Hr + 1.5, ZB - ENDS - 4 + i * (Lt + 2 * ENDS + 8) / 14]) for i in range(15)], 5.5, 'ridge', sides=8)

# ---------------- scalloped valance under the eaves (one scallop per stripe) ----------------
VAL_TOP, VAL_RECT, VAL_R = 6.0, 12.0, STRIPE / 2
def valance_run(a, b, outward):
    """Valance hanging from the eave line a->b (both at eave height), scallops below."""
    length = np.linalg.norm(b - a); d = (b - a) / length
    # stripe phase in world space so colours line up with the roof stripes
    s0 = math.floor(((a @ np.abs(d)) + STRIPE / 2) / STRIPE)
    n = int(round(length / STRIPE))
    step = length / n
    for k in range(n):
        p0 = a + d * k * step; p1 = a + d * (k + 1) * step
        teal = (k + int(s0)) % 2 == 0
        u0, u1 = (0.02, 0.48) if teal else (0.52, 0.98)
        top0, top1 = p0 + np.array([0, VAL_TOP, 0]), p1 + np.array([0, VAL_TOP, 0])
        bot0, bot1 = p0 - np.array([0, VAL_RECT, 0]), p1 - np.array([0, VAL_RECT, 0])
        H_ = VAL_TOP + VAL_RECT + VAL_R
        vy = lambda y: (VAL_TOP - y) / H_
        quad(top0, top1, bot1, bot0, uvmap('valance', u0, vy(VAL_TOP)), uvmap('valance', u1, vy(VAL_TOP)),
             uvmap('valance', u1, vy(-VAL_RECT)), uvmap('valance', u0, vy(-VAL_RECT)))
        c = (bot0 + bot1) / 2
        for m in range(8):
            a0, a1 = math.pi * m / 8, math.pi * (m + 1) / 8
            q0 = c + d * (step / 2) * math.cos(a0) - np.array([0, VAL_R * math.sin(a0), 0])
            q1 = c + d * (step / 2) * math.cos(a1) - np.array([0, VAL_R * math.sin(a1), 0])
            uv = lambda q: uvmap('valance', u0 + (u1 - u0) * (0.5 + ((q - c) @ d) / step), vy(q[1] - c[1] - VAL_RECT))
            tri([c, q0, q1], [uv(c), uv(q0), uv(q1)])
for sgn in (-1, 1):
    a = roof_point(sgn, 0.0, 1.0) + np.array([0, -2, 0]); b = roof_point(sgn, 1.0, 1.0) + np.array([0, -2, 0])
    valance_run(a, b, sgn)

# ---------------- long walls ----------------
def wall_point(sgn, t, s, inset=0.0):
    z = ZB + t * Lt
    bulge = 4.0 * math.sin(math.pi * s) * math.sin(math.pi * t)
    flare = 5.0 * max(0, s - 0.85) / 0.15
    return np.array([sgn * (Wt / 2 + flare - bulge - inset), Hw * (1 - s), z])
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
    tube([wall_point(sgn, i / 16, 1.0) + np.array([0, 2.2, 0]) for i in range(17)], 2.6, 'hem', sides=6)

# ---------------- windows: oiled-linen panes in a rolled canvas frame, flap rolled up above ----------------
WIN_W, WIN_H, WIN_Y = 74.0, 54.0, 82.0
def window(center, right, up, normal):
    """center on the wall surface; right/up/normal unit vectors (normal points outward)."""
    c = center + normal * 1.2
    corners = [c - right * WIN_W / 2 - up * WIN_H / 2, c + right * WIN_W / 2 - up * WIN_H / 2,
               c + right * WIN_W / 2 + up * WIN_H / 2, c - right * WIN_W / 2 + up * WIN_H / 2]
    u = [(0, 1), (1, 1), (1, 0), (0, 0)]
    quad(*corners, *[uvmap('pane', *x) for x in u])
    gquad(*[p + normal * 0.6 for p in corners], *[guv('pane', *x) for x in u])
    # the same pane seen from inside
    quad(*[p - normal * 3.6 for p in corners[::-1]], *[uvmap('pane', *x) for x in u[::-1]])
    # rolled canvas frame
    ring = corners + [corners[0]]
    for a, b in zip(ring[:-1], ring[1:]):
        tube([a + normal * 1.5, b + normal * 1.5], 2.4, 'hem', sides=6)
    # cross ties on the pane
    for a, b in ((corners[0], corners[2]), (corners[1], corners[3])):
        tube([a + normal * 1.9, b + normal * 1.9], 0.6, 'rope', sides=4, tile=24)
    # outer flap rolled up above the window and tied with two cords
    top = c + up * (WIN_H / 2 + 7) + normal * 4
    tube([top - right * (WIN_W / 2 + 8), top + right * (WIN_W / 2 + 8)], 5.5, 'roll', sides=8, twist=2.0, cap=True)
    for f in (-0.33, 0.33):
        p = top + right * WIN_W * f
        tube([p + up * 8 + normal * -3, p + normal * 6.5, p - up * 8 + normal * -1], 0.8, 'rope', sides=4, tile=24)
for sgn in (-1, 1):
    for t in (0.28, 0.72):
        z = ZB + t * Lt
        n = np.array([sgn, 0, 0.0])
        window(np.array([sgn * (Wt / 2 - 4.0 * math.sin(math.pi * (1 - WIN_Y / Hw)) * math.sin(math.pi * t)), WIN_Y, z]),
               np.array([0, 0, -sgn]), np.array([0, 1, 0]), n)
window(np.array([0, WIN_Y + 20, ZB - 1.5]), np.array([-1, 0, 0]), np.array([0, 1, 0]), np.array([0, 0, -1]))

# ---------------- gable ends ----------------
def gable_uv(p, reg='back'): return uvmap(reg, 0.5 + p[0] / Wt, 1 - p[1] / Hr)
def poly(points, z, inward, flip, hole=None):
    pts = [np.array([x, y, z]) for x, y in points]
    c = sum(pts) / len(pts)
    for a, b in zip(pts, pts[1:] + pts[:1]):
        tr = [c, a, b] if not flip else [c, b, a]
        tri(tr, [gable_uv(p) for p in tr])
        ti = [p + np.array([0, 0, inward]) for p in tr[::-1]]
        tri(ti, [gable_uv(p, 'inner') for p in tr[::-1]])
# back gable, built as a pentagon from two convex pieces so the back window sits on flat cloth
poly([(-Wt / 2, 0), (Wt / 2, 0), (Wt / 2, Hw), (-Wt / 2, Hw)], ZB, 2.0, flip=True)
poly([(-Wt / 2, Hw), (Wt / 2, Hw), (0, Hr)], ZB, 2.0, flip=True)
poly([(-Wt / 2, 0), (-DOOR, 0), (0, Hr), (-Wt / 2, Hw)], ZF, -2.0, flip=False)
poly([(DOOR, 0), (Wt / 2, 0), (Wt / 2, Hw), (0, Hr)], ZF, -2.0, flip=False)
for z in (ZB, ZF):
    tube([np.array([-Wt / 2 + i * Wt / 12, 2.2, z]) for i in range(13)], 2.6, 'hem', sides=6)
# valance across both gable ends too, under the roof's end overhang
for z in (ZB - ENDS + 1, ZF + ENDS - 1):
    for sgn in (-1, 1):
        a = np.array([sgn * (Wt / 2 + EAVE), EAVE_Y - 2, z]); b = np.array([sgn * 4.0, Hr - 6, z])
        # sloped run: short valance strips that follow the roof edge
        n = 8
        for k in range(n):
            p0 = a + (b - a) * k / n; p1 = a + (b - a) * (k + 1) / n
            teal = k % 2 == 0
            u0, u1 = (0.02, 0.48) if teal else (0.52, 0.98)
            quad(p0 + np.array([0, 4, 0]), p1 + np.array([0, 4, 0]), p1 - np.array([0, 16, 0]), p0 - np.array([0, 16, 0]),
                 uvmap('valance', u0, 0.0), uvmap('valance', u1, 0.0), uvmap('valance', u1, 0.55), uvmap('valance', u0, 0.55))

# door flaps rolled back and tied out to the sides
for sgn in (-1, 1):
    pts = [np.array([sgn * DOOR * f + sgn * 6 * math.sin(math.pi * f), Hr * (1 - f), ZF + 4 + 5 * math.sin(math.pi * f)])
           for f in np.linspace(0, 1, 12)]
    tube(pts, lambda f: 3.0 + 11.0 * math.sin(math.pi * min(1, f * 1.1)) ** 0.8, 'roll', sides=8, twist=3.0, cap=True)
    mid = pts[7]
    tube([mid + 16 * np.array([math.cos(a) * 0.9, math.sin(a), 0.5 * math.cos(a)]) for a in np.linspace(0, 2 * math.pi, 9)], 1.2, 'rope', sides=5, tile=24)
    peg = np.array([sgn * (DOOR + 75), 0, ZF + 45])
    tube([mid, (mid + peg) / 2 + np.array([0, 10, 0]), peg + np.array([0, 12, 0])], 0.9, 'rope', sides=5, tile=24)
    tube([peg - np.array([0, 6, 0]), peg + np.array([0, 16, 0])], lambda f: 2.6 - f, 'stake', sides=4, cap=True)

# ---------------- vestibule: the shallow room you see through the door ----------------
VX = DOOR + 14
ZV = ZF - VD
def roof_y_at(x):   # underside of the roof at |x|
    return Hr - (Hr - EAVE_Y) * abs(x) / (Wt / 2 + EAVE) - 4
for sgn in (-1, 1):     # side walls of the vestibule (inner canvas)
    x = sgn * VX
    p = [np.array([x, 0, ZF - 2]), np.array([x, 0, ZV]), np.array([x, roof_y_at(x), ZV]), np.array([x, roof_y_at(x), ZF - 2])]
    u = [uvmap('inner', 0.1, 1), uvmap('inner', 0.4, 1), uvmap('inner', 0.4, 0.3), uvmap('inner', 0.1, 0.3)]
    if sgn > 0: quad(*p, *u)
    else: quad(*p[::-1], *u[::-1])
# rug on the floor
quad(np.array([-VX + 10, 0.9, ZF - 6]), np.array([VX - 10, 0.9, ZF - 6]), np.array([VX - 10, 0.9, ZV + 4]), np.array([-VX + 10, 0.9, ZV + 4]),
     uvmap('blanket', 0, 1), uvmap('blanket', 1, 1), uvmap('blanket', 1, 0), uvmap('blanket', 0, 0))
GAP_W = 26.0
# ---- the room behind the curtain, Rockstar style: real 3D up close, a painted backdrop far back ----
# Near: the Kakariko rug running back from the curtain, two display tables, a hanging lantern (props
# drawn from the game's own archives, listed in STAGE). Far: a backdrop card rendered through this exact
# frame from where the player's camera usually stands, so its perspective lines up with the 3D in front.
CARD_Z, CARD_X, CARD_H = -110.0, 125.0, 345.0
for z0, z1 in ((ZV - 2, ZV - 160), (ZV - 162, CARD_Z + 2)):
    quad(np.array([-62, 0.9, z0]), np.array([62, 0.9, z0]), np.array([62, 0.9, z1]), np.array([-62, 0.9, z1]),
         uvmap('blanket', 0, 1), uvmap('blanket', 1, 1), uvmap('blanket', 1, 0), uvmap('blanket', 0, 0))
# the card is cut to the tent's cross-section so it can't poke through the roof
yc = roof_y_at(CARD_X) - 4
card_pts = [(-CARD_X, 0), (CARD_X, 0), (CARD_X, yc), (0, Hr - 8), (-CARD_X, yc)]
cuv = lambda x, y: ((x + CARD_X) / (2 * CARD_X), 1 - y / CARD_H)
P3 = [np.array([x, y, CARD_Z]) for x, y in card_pts]; c3 = sum(P3) / 5
for a_, b_ in zip(P3, P3[1:] + P3[:1]):
    tri_uv = [cuv(q[0], q[1]) for q in (c3, a_, b_)]
    TRIS.append((np.array([c3, a_, b_]), np.array([uvmap('gap', *t) for t in tri_uv])))
    GLOW.append((np.array([c3, a_, b_]) + np.array([0, 0, 0.5]), np.array([guv('gap', *t) for t in tri_uv])))
STAGE = [('HTable', 'ytable.bmd', (-130.0, 0.0, -50.0), 0.0, 0.62), ('HTable', 'ytable.bmd', (130.0, 0.0, -50.0), 0.0, 0.62),
         ('Obj_kntr', 'ob_kantera.bmd', (0.0, 250.0, 40.0), 0.0, 1.6)]
tube([np.array([0, Hr - 6, 40.0]), np.array([0, 280, 40.0])], 0.7, 'rope', sides=4, tile=24)
def cushion(c):
    for k in range(12):
        a0, a1 = 2 * math.pi * k / 12, 2 * math.pi * (k + 1) / 12
        for (y0, r0), (y1, r1) in zip([(0, 18), (3, 19), (6, 15)], [(3, 19), (6, 15), (7, 0)]):
            p = [c + np.array([r0 * math.cos(a0), y0, r0 * math.sin(a0)]), c + np.array([r0 * math.cos(a1), y0, r0 * math.sin(a1)]),
                 c + np.array([r1 * math.cos(a1), y1, r1 * math.sin(a1)]), c + np.array([r1 * math.cos(a0), y1, r1 * math.sin(a0)])]
            quad(*p, uvmap('curtain', k / 12, 0.3), uvmap('curtain', (k + 1) / 12, 0.3), uvmap('curtain', (k + 1) / 12, 0.32), uvmap('curtain', k / 12, 0.32))
for x in (-130.0, 130.0):
    cushion(np.array([x, 104 * 0.62 + 0.5, -50.0]))
# the curtain: two heavy drapes parting in the middle, folds rippling, gathered toward the bottom
CUR_TOP = roof_y_at(VX) - 8
def drape(sgn):
    cols, rows = 10, 8
    for i in range(cols):
        for j in range(rows):
            def pt(ci, rj):
                f = ci / cols; g = rj / rows
                inner_edge = GAP_W * (0.25 + 0.75 * g ** 1.5)      # parting widens toward the floor
                x = sgn * (inner_edge + (VX - inner_edge) * f)
                y = CUR_TOP * (1 - g)
                z = ZV + 3.5 * math.sin(f * cols * math.pi * 0.9 + sgn) + 2
                return np.array([x, y, z])
            p = [pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), pt(i, j + 1)]
            u = [uvmap('curtain', i / cols, j / rows), uvmap('curtain', (i + 1) / cols, j / rows),
                 uvmap('curtain', (i + 1) / cols, (j + 1) / rows), uvmap('curtain', i / cols, (j + 1) / rows)]
            if sgn < 0: quad(*p[::-1], *u[::-1])
            else: quad(*p, *u)
drape(-1); drape(1)
tube([np.array([-VX, CUR_TOP + 4, ZV + 2]), np.array([VX, CUR_TOP + 4, ZV + 2])], 2.2, 'wood', sides=6, cap=True, tile=72)
# canvas partition above the curtain rod, up to the roof, so nothing shows through over the curtain
part = [(-VX, CUR_TOP), (VX, CUR_TOP), (VX, roof_y_at(VX) + 2), (0, roof_y_at(0) + 2), (-VX, roof_y_at(VX) + 2)]
pp = [np.array([x, y, ZV - 1]) for x, y in part]
pc = sum(pp) / len(pp)
for a_, b_ in zip(pp, pp[1:] + pp[:1]):
    tri([pc, b_, a_], [uvmap('inner', 0.5 + q[0] / Wt, 1 - q[1] / Hr) for q in (pc, b_, a_)])
# hanging lantern just inside the door
LAMP = np.array([0, 205, ZF - 30])
tube([np.array([0, roof_y_at(0) - 2, ZF - 30]), LAMP + np.array([0, 24, 0])], 0.7, 'rope', sides=4, tile=24)
def lantern(c):
    hh, r = 22.0, 8.5
    ring = lambda y, rr: [c + np.array([rr * math.cos(2 * math.pi * k / 6), y, rr * math.sin(2 * math.pi * k / 6)]) for k in range(7)]
    lo, hi = ring(-hh / 2, r), ring(hh / 2, r)
    for k in range(6):
        u0, u1 = k / 6, (k + 1) / 6
        quad(lo[k], lo[k + 1], hi[k + 1], hi[k], uvmap('pane', u0, 1), uvmap('pane', u1, 1), uvmap('pane', u1, 0), uvmap('pane', u0, 0))
        out = lambda p: p + (p - np.array([c[0], p[1], c[2]])) / r * 0.5
        gquad(out(lo[k]), out(lo[k + 1]), out(hi[k + 1]), out(hi[k]), guv('lamp', u0, 1), guv('lamp', u1, 1), guv('lamp', u1, 0), guv('lamp', u0, 0))
    tube([c - np.array([0, hh / 2 + 3, 0]), c - np.array([0, hh / 2 - 1, 0])], r + 2, 'wood', sides=6, cap=True)
    tube([c + np.array([0, hh / 2 - 1, 0]), c + np.array([0, hh / 2 + 7, 0])], lambda f: (r + 2) * (1 - 0.8 * f), 'wood', sides=6, cap=True)
    for k in range(0, 6, 2):
        tube([lo[k], hi[k]], 0.9, 'wood', sides=4)
    tube([c + np.array([0, hh / 2 + 7, 0]), c + np.array([0, hh / 2 + 13, 0]) + np.array([0, 0, 0.01])], 1.6, 'rope', sides=4, tile=24)
STAGE.append(('Obj_kntr', 'ob_kantera.bmd', tuple(LAMP), 0.0, 1.6))

# ---------------- poles, banner ----------------
# back: a single upright. Front: two poles leaning in from either side of the doorway, crossing at
# the ridge (shear legs), so the doorway is clear; the flagpole rises from where they cross.
tube([np.array([0, 0, ZB + 4]), np.array([0, Hr + TIP, ZB + 4])], 5.0, 'wood', sides=7, cap=True, tile=72)
tube([np.array([0, Hr + TIP - 2, ZB + 4]), np.array([0, Hr + TIP + 6, ZB + 4])], 2.2, 'wood', sides=6, cap=True)
CROSS = np.array([0, Hr + 6, ZF + 7])
for sgn in (-1, 1):
    foot = np.array([sgn * (DOOR + 34), 0, ZF + 7])
    d = CROSS - foot
    tube([foot, CROSS + d / np.linalg.norm(d) * 18], 4.6, 'wood', sides=7, cap=True, tile=72)
tube([CROSS - np.array([0, 4, 0]), np.array([0, Hr + 150, ZF + 7])], 3.6, 'wood', sides=7, cap=True, tile=72)
tube([np.array([0, Hr + 148, ZF + 7]), np.array([0, Hr + 156, ZF + 7])], 2.0, 'wood', sides=6, cap=True)
# lashing where the poles cross
tube([CROSS + np.array([7 * math.cos(a), 7 * math.sin(a) * 0.6, 0]) for a in np.linspace(0, 4 * math.pi, 15)], 1.0, 'rope', sides=4, tile=24)
BAN_W, BAN_H = 64.0, 101.0
bar_y = Hr + 140
tube([np.array([-BAN_W / 2 - 8, bar_y, ZF + 11]), np.array([BAN_W / 2 + 8, bar_y, ZF + 11])], 2.0, 'wood', sides=6, cap=True)
def banner_pt(f, g):   # f across 0..1, g down 0..1; a gentle wave in the cloth
    return np.array([-BAN_W / 2 + BAN_W * f, bar_y - 3 - BAN_H * g, ZF + 13 + 3.0 * math.sin(f * 2.4 + g * 1.7) * g])
for i in range(4):
    for j in range(5):
        f0, f1, g0, g1 = i / 4, (i + 1) / 4, j / 5, (j + 1) / 5
        p = [banner_pt(f0, g0), banner_pt(f1, g0), banner_pt(f1, g1), banner_pt(f0, g1)]
        u = [uvmap('banner', f0, g0), uvmap('banner', f1, g0), uvmap('banner', f1, g1), uvmap('banner', f0, g1)]
        quad(*p[::-1], *u[::-1])                              # front face (toward +z)
        quad(*[q - np.array([0, 0, 0.6]) for q in p], *u)                 # back face
for f in (0.08, 0.92):
    tube([np.array([-BAN_W / 2 + BAN_W * f, bar_y + 1, ZF + 11]), np.array([-BAN_W / 2 + BAN_W * f, bar_y - 6, ZF + 13])], 0.7, 'rope', sides=4, tile=24)

# ---------------- guy ropes and stakes ----------------
def stake(base, lean):
    top = base + np.array([0, 18, 0]) + lean * 4
    tube([base - np.array([0, 6, 0]), top], lambda f: 3.0 - 1.1 * (1 - f), 'stake', sides=4, cap=True)
    return top - np.array([0, 3, 0])
def rope(a, b, droop=4.0, n=7):
    tube([a + (b - a) * (i / (n - 1)) - np.array([0, droop * math.sin(math.pi * i / (n - 1)), 0]) for i in range(n)], 1.0, 'rope', sides=5, tile=24)
for sgn in (-1, 1):
    for k in range(6):
        t = 0.07 + 0.86 * k / 5
        e = roof_point(sgn, (t * Lt + ENDS) / (Lt + 2 * ENDS), 1.0) + np.array([0, -3, 0])
        rope(e, stake(np.array([sgn * (Wt / 2 + 130), 0, e[2]]), np.array([sgn, 0, 0])), droop=6)
    for k in range(8):
        t = 0.05 + 0.9 * k / 7
        if abs(ZB + t * Lt - (ZB + 0.28 * Lt)) < 45 or abs(ZB + t * Lt - (ZB + 0.72 * Lt)) < 45:
            continue   # no loops right under the windows
        h = wall_point(sgn, t, 1.0) + np.array([0, 3, 0])
        rope(h, stake(np.array([h[0] + sgn * 24, 0, h[2]]), np.array([sgn, 0, 0])), droop=1, n=3)
for z, sg, top in ((ZF + 7, 1, Hr + 14), (ZB + 4, -1, Hr + TIP)):
    tip = np.array([0, top - 8, z])
    for dx in (-50, 50):
        rope(tip, stake(np.array([dx, 0, z + sg * 240]), np.array([0, 0, sg])), droop=9, n=9)

print('triangles:', len(TRIS), 'glow:', len(GLOW))
pickle.dump(TRIS, open(f'{OUT}/beedle_tent_tris.pkl', 'wb'))
pickle.dump(GLOW, open(f'{OUT}/beedle_tent_glow_tris.pkl', 'wb'))
pickle.dump(STAGE, open(f'{OUT}/beedle_stage_props.pkl', 'wb'))

# ======================= textures =======================
exec(open(os.path.join(HERE, 'paint_lib.py')).read().split("A = np.clip(A, 0, 255)")[0])
TEAL = np.array([38, 112, 116], np.float32)

roof_guys = [0.07 + 0.86 * k / 5 for k in range(6)]
canvas_panel('sideL', 101, stake_us=roof_guys, guy_u=(), dirt=False, seams=1)
canvas_panel('sideR', 202, stake_us=[1 - t for t in roof_guys], guy_u=(), dirt=False, seams=1)
canvas_panel('wall', 151, stake_us=[0.05 + 0.9 * k / 7 for k in range(8)], guy_u=(), seams=1)
canvas_panel('back', 303, stake_us=(), guy_u=(), seams=1)

def stripe_region(name, world_of_u):
    """Recolour alternate stripes teal, with a sewn seam where the panels meet."""
    ys, xs, h, w = region_px(name)
    xx = np.arange(w, dtype=np.float32) / w
    world = world_of_u(xx)
    idx = np.floor((world + STRIPE / 2) / STRIPE)
    teal = (idx % 2 == 0)
    frac = (world + STRIPE / 2) / STRIPE - idx
    seam = np.minimum(frac, 1 - frac) * STRIPE      # units from the nearest seam
    img = A[ys, xs]
    ratio = TEAL / CANVAS
    tint = np.where(teal[None, :, None], ratio[None, None, :], 1.0)
    img = img * tint
    img *= (1 - 0.18 * np.exp(-(seam / 0.9) ** 2))[None, :, None]
    yy = np.arange(h)[:, None]
    stitch = ((yy // 4) % 2 == 0) & (np.abs(seam - 1.6) < 0.35)[None, :]
    img *= (1 - 0.25 * stitch)[..., None]
    A[ys, xs] = img
stripe_region('sideL', lambda u: ZB - ENDS + u * (Lt + 2 * ENDS))
stripe_region('sideR', lambda u: ZB - ENDS + (1 - u) * (Lt + 2 * ENDS))
stripe_region('wall', lambda u: ZB + u * Lt)
stripe_region('back', lambda u: (u - 0.5) * Wt)

# valance: teal half and cream half, burlap, stitched hem along the scallop edge
ys, xs, h, w = region_px('valance')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
weave = WEAVE(h, w, 991, period=5.0)
img = np.where((xx < w / 2)[..., None], TEAL, CANVAS) * weave[..., None]
img *= (1 - 0.12 * (yy / h))[..., None]
A[ys, xs] = img

# day pane: oiled linen, amber and a little see-through dark
ys, xs, h, w = region_px('pane')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
weave = WEAVE(h, w, 992, period=4.0)
edge = np.minimum(np.minimum(xx, w - xx) / w, np.minimum(yy, h - yy) / h)
img = np.array([170, 128, 70], np.float32) * (0.55 + 0.25 * np.clip(edge * 6, 0, 1))[..., None] * weave[..., None]
A[ys, xs] = img

# curtain: deep plum wool, vertical folds, a gold-embroidered border at the hem
ys, xs, h, w = region_px('curtain')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
fold = 0.5 + 0.5 * np.sin(xx / w * 9 * math.pi)
weave = duck_weave(h, w, 993, period=2.0)
img = np.array([92, 52, 96], np.float32) * (0.55 + 0.5 * fold ** 0.8)[..., None] * weave[..., None]
band = (yy > h * 0.86) & (yy < h * 0.95)
zig = (np.abs(((xx / 12) % 2) - 1) * 8 - (yy - h * 0.905)) ** 2 < 6
img = np.where(band[..., None], np.array([150, 118, 50], np.float32) * (0.7 + 0.3 * fold[..., None]), img)
img = np.where((band & zig)[..., None], np.array([220, 186, 96], np.float32), img)
A[ys, xs] = img

# rug: the same Kakariko woven rug as inside (the game's own texture), so the entry matches the room
import pickle as _pk
_lib = _pk.load(open('/home/claude/stages/texlib.pkl', 'rb'))
ys, xs, h, w = region_px('blanket')
A[ys, xs] = cv2.resize(_lib['U_cloth_002_2'][0][..., :3].astype(np.float32), (w, h), interpolation=cv2.INTER_AREA)

# day view through the curtain's parting: a dim warm room, shelves and hanging goods in shadow
def room_glimpse(h, w, bright):
    """Beedle's shop as seen through the parting in the curtain: shelves of glazed pots and jars,
    apples, cheese and a jug of oil, a crate, a grain sack and a coil of rope on the floor, herbs
    and peppers hanging from above. Nothing Link uses as an item, so model-replacement mods never
    clash with it. Coordinates are in game units on the plane (83 wide, 230 tall), y up from the floor."""
    SS = 4
    H, W = h * SS, w * SS
    PW, PH = GAP_W * 3.2, 230.0
    def X(x): return (x / PW + 0.5) * W            # x: -PW/2 .. PW/2
    def Y(y): return (1 - y / PH) * H              # y: 0 floor .. PH
    sc = W / PW                                    # pixels per unit
    img = np.zeros((H, W, 3), np.float32)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # back wall: lamplit canvas, brightest round the middle
    wall = np.array([168, 120, 72], np.float32)
    lamp = np.exp(-((xx - W * 0.5) / (W * 0.55)) ** 2 - ((yy - Y(150)) / (H * 0.45)) ** 2)
    img[:] = wall * (0.45 + 0.6 * lamp)[..., None]
    img *= (0.94 + 0.06 * np.sin(xx / sc * 0.9))[..., None]          # canvas seams
    floor = yy > Y(14)
    img[floor] = np.array([92, 64, 40], np.float32) * (0.7 + 0.3 * (yy[floor] / H))[..., None]

    def shade_mask(mask, color, cx, cy, rx, ry, gloss=0.0):
        """Round-ish shading inside mask: lit from upper left, rim darker, optional glossy spot."""
        nx = (xx - cx) / max(rx, 1); ny = (yy - cy) / max(ry, 1)
        lit = np.clip(1.05 - 0.55 * np.hypot(nx + 0.35, ny + 0.4), 0.25, 1.1)
        c = np.array(color, np.float32) * lit[..., None]
        if gloss:
            spot = np.exp(-(((nx + 0.4) / 0.18) ** 2 + ((ny + 0.45) / 0.25) ** 2)) * gloss
            c = c + (255 - c) * spot[..., None]
        img[mask] = c[mask]
    def ell(cx, cy, rx, ry):
        return ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1
    def rect(x0, y0, x1, y1):
        return (xx >= min(x0, x1)) & (xx <= max(x0, x1)) & (yy >= min(y0, y1)) & (yy <= max(y0, y1))

    # shelves (two boards on brackets)
    for sy in (128.0, 80.0):
        board = rect(X(-PW / 2), Y(sy), X(PW / 2), Y(sy - 3.2))
        img[board] = np.array([118, 80, 46], np.float32) * (0.8 + 0.2 * np.sin(xx[board] / sc * 3))[..., None]
        img[rect(X(-PW / 2), Y(sy - 3.2), X(PW / 2), Y(sy - 5))] *= 0.55     # shadow under the board
        for bx in (-28.0, 28.0):
            img[rect(X(bx - 1.2), Y(sy - 3), X(bx + 1.2), Y(sy - 12))] = np.array([96, 64, 36], np.float32)

    # top shelf: glazed clay pots and lidded jars (generic wares, nothing Link carries)
    def pot(x, base, color, kind):
        if kind == 0:     # round pot with a lip
            r = 5.0; cy = base + r * 0.9
            body = ell(X(x), Y(cy), r * sc, r * 0.9 * sc)
            lip = rect(X(x - r * 0.55), Y(cy + r * 0.8), X(x + r * 0.55), Y(cy + r * 1.05))
            shade_mask(body, color, X(x), Y(cy), r * sc, r * 0.9 * sc, gloss=0.25)
            img[lip] = np.array(color, np.float32) * 0.75
            img[body & (np.abs(yy - Y(cy + r * 0.25)) < 0.35 * sc)] *= 0.7      # painted band
        else:             # tall lidded jar
            w_, h_ = 4.2, 11.0
            body = rect(X(x - w_), Y(base), X(x + w_), Y(base + h_)) & ell(X(x), Y(base + h_ / 2), w_ * sc * 1.08, h_ / 2 * sc * 1.25)
            shade_mask(body, color, X(x), Y(base + h_ / 2), w_ * sc, h_ / 2 * sc, gloss=0.2)
            lid = ell(X(x), Y(base + h_), w_ * 0.9 * sc, 1.4 * sc)
            shade_mask(lid, (150, 108, 64), X(x), Y(base + h_), w_ * sc, 1.4 * sc)
            img[ell(X(x), Y(base + h_ + 1.6), 1.0 * sc, 1.0 * sc)] = np.array([120, 84, 52], np.float32)
    for x, col, kind in ((-30, (176, 98, 60), 0), (-19, (92, 120, 112), 1), (-8, (196, 170, 120), 0),
                         (3, (150, 70, 52), 1), (14, (110, 128, 86), 0), (25, (176, 98, 60), 1)):
        pot(x, 128.0, col, kind)

    # lower shelf: a wooden bowl of apples and a wheel of cheese with a wedge cut out
    bowl = ell(X(-24), Y(80), 9 * sc, 4.5 * sc) & (yy >= Y(84))
    for k, (ax, ay) in enumerate(((-29, 84.5), (-24, 85.5), (-19, 84.5), (-26.5, 88.5), (-21.5, 88.5))):
        shade_mask(ell(X(ax), Y(ay), 2.9 * sc, 2.7 * sc), (186, 44, 36) if k != 3 else (150, 170, 60), X(ax), Y(ay), 2.9 * sc, 2.7 * sc, gloss=0.45)
    shade_mask(bowl, (132, 90, 52), X(-24), Y(81), 9 * sc, 4.5 * sc)
    cheese = rect(X(-9), Y(80), X(3), Y(86)) & ell(X(-3), Y(83), 6.4 * sc, 4 * sc)
    shade_mask(cheese, (224, 182, 80), X(-3), Y(83), 6 * sc, 3 * sc)
    img[rect(X(-9), Y(86), X(3), Y(87.4)) & ell(X(-3), Y(86.7), 6.2 * sc, 1.6 * sc)] = np.array([240, 206, 110], np.float32)
    wedge = np.array([[X(-3), Y(86.7)], [X(3), Y(86.7)], [X(3), Y(80)], [X(-1), Y(80)]], np.int32)
    cv2.fillPoly(img, [wedge], (236, 200, 104))
    jx = 10.0
    jug = ell(X(jx), Y(80 + 7), 7.5 * sc, 7 * sc) & (yy <= Y(80))
    jug |= ell(X(jx), Y(80 + 7), 7.5 * sc, 7 * sc)
    shade_mask(jug, (170, 112, 54), X(jx), Y(87), 7.5 * sc, 7 * sc, gloss=0.25)
    img[rect(X(jx - 2), Y(94), X(jx + 2), Y(99))] = np.array([150, 98, 48], np.float32)
    img[rect(X(jx - 2.6), Y(99), X(jx + 2.6), Y(101))] = np.array([120, 84, 52], np.float32)
    # a little Hylian price tag on the jug
    img[rect(X(jx + 3), Y(90), X(jx + 8), Y(86))] = np.array([230, 216, 180], np.float32)
    # a stack of folded cloth at the end of the shelf
    for k, col in enumerate(((60, 110, 112), (140, 50, 48), (210, 196, 160))):
        img[rect(X(22), Y(80 + k * 2.6), X(36), Y(80 + (k + 1) * 2.6 - 0.4))] = np.array(col, np.float32)

    # floor: wooden crate, a quiver of arrows leaning on it, a round wooden shield leaning on the right
    crate = rect(X(-36), Y(0), X(-8), Y(26))
    img[crate] = np.array([128, 88, 52], np.float32) * (0.75 + 0.25 * ((yy[crate] / sc) % 6 > 0.8))[..., None]
    for x in (-36, -8):
        img[rect(X(x), Y(0), X(x + 2.4 * (1 if x < 0 else -1)), Y(26))] *= 0.7
    # a grain sack slumped against the crate, tied at the neck
    sack = ell(X(4), Y(10), 9 * sc, 10 * sc) | ell(X(4), Y(20), 5.5 * sc, 5 * sc)
    shade_mask(sack, (186, 160, 112), X(4), Y(12), 9 * sc, 11 * sc)
    img[rect(X(1), Y(23.5), X(7), Y(25))] = np.array([120, 92, 60], np.float32)
    tuft = ell(X(4), Y(27), 3.2 * sc, 2.2 * sc)
    shade_mask(tuft, (196, 172, 120), X(4), Y(27), 3.2 * sc, 2.2 * sc)
    # a coil of rope on the floor
    for k in range(5):
        r_ = 11.5 - k * 2.0
        ring = (np.abs(np.hypot((xx - X(26)) / (r_ * sc), (yy - Y(5 + k * 0.3)) / (r_ * 0.38 * sc)) - 1) < 0.12)
        img[ring] = np.array([184, 152, 100], np.float32) * (0.85 + 0.15 * (k % 2))
    img[ell(X(26), Y(5.6), 3.5 * sc, 1.3 * sc)] = np.array([60, 44, 30], np.float32)

    # hanging from above: bundles of dried herbs and a string of red peppers
    for k, (x, ln) in enumerate(((-24, 26), (-6, 18), (12, 30), (28, 22))):
        cv2.line(img, (int(X(x)), 0), (int(X(x)), int(Y(PH - ln))), (90, 70, 50), int(0.5 * sc))
        if k == 2:
            for m in range(7):
                cy_ = PH - ln - m * 4.2
                shade_mask(ell(X(x + (m % 2) * 1.2 - 0.6), Y(cy_), 1.8 * sc, 2.6 * sc), (190, 40, 30), X(x), Y(cy_), 2 * sc, 2.6 * sc, gloss=0.4)
        else:
            for m in range(9):
                a = math.radians(-35 + m * 9)
                x1, y1 = X(x + math.sin(a) * 10), Y(PH - ln - math.cos(a) * 14)
                cv2.line(img, (int(X(x)), int(Y(PH - ln))), (int(x1), int(y1)), (88 + m * 4, 120 - m * 2, 60), int(1.1 * sc))

    # a warm haze from the lamp, then down to the region size
    img = cv2.GaussianBlur(img, (0, 0), 0.6 * SS)
    img = cv2.resize(img, (w, h), interpolation=cv2.INTER_AREA)
    if bright:
        yy2, xx2 = np.mgrid[0:h, 0:w].astype(np.float32)
        haze = np.exp(-((xx2 - w * 0.5) / (w * 0.6)) ** 2 - ((yy2 - h * 0.35) / (h * 0.5)) ** 2)
        img = img * 1.25 + np.array([60, 36, 10], np.float32) * haze[..., None]
    else:
        img = img * 0.55
    return np.clip(img, 0, 255)

ys, xs, h, w = region_px('gap')
# the screenshot of the real interior, edited for daylight (see edit_glimpse.py)
_g = cv2.cvtColor(cv2.imread(os.path.join(HERE, 'interior2/card_day.png')), cv2.COLOR_BGR2RGB).astype(np.float32)
A[ys, xs] = cv2.resize(_g, (w, h), interpolation=cv2.INTER_AREA)

# banner: Beedle's own face banner from his pack, cleaned up and scaled into place
ys, xs, h, w = region_px('banner')
ban = cv2.cvtColor(cv2.imread(os.path.join(HERE, 'beedle_banner_src.png')), cv2.COLOR_BGR2RGB).astype(np.float32)
A[ys, xs] = cv2.resize(ban, (w, h), interpolation=cv2.INTER_CUBIC)

A = np.clip(A, 0, 255).astype(np.uint8)
cv2.imwrite(f'{OUT}/beedle_tent_HD_2048.png', cv2.cvtColor(A, cv2.COLOR_RGB2BGR))
cv2.imwrite(f'{OUT}/beedle_tent_512.png', cv2.cvtColor(cv2.resize(A, (512, 512), interpolation=cv2.INTER_AREA), cv2.COLOR_RGB2BGR))

# ---------------- glow texture (night) ----------------
GL = np.zeros((G, G, 3), np.float32)
def gpx(name):
    x0, y0, x1, y1 = GR[name]; return slice(y0, y1), slice(x0, x1), y1 - y0, x1 - x0
ys, xs, h, w = gpx('pane')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
u, v = xx / w, yy / h
glow = np.exp(-(((u - 0.5) / 0.55) ** 2 + ((v - 0.55) / 0.6) ** 2))
img = np.array([255, 200, 120], np.float32) * (0.55 + 0.5 * glow)[..., None]
# silhouettes of hanging goods behind the oiled linen
sil = np.zeros((h, w), np.float32)
for k, cx in enumerate((0.22, 0.47, 0.74)):
    sil += (np.hypot((u - cx) / 0.08, (v - 0.42 - 0.08 * (k % 2)) / 0.12) < 1)
    sil += (np.abs(u - cx) < 0.008) & (v < 0.32)
sil = cv2.GaussianBlur(np.clip(sil, 0, 1), (0, 0), 3.0)
img *= (1 - 0.45 * sil)[..., None]
img *= (0.85 + 0.15 * duck_weave(h, w, 995, period=3.0))[..., None]
GL[ys, xs] = img
ys, xs, h, w = gpx('gap')
_g = cv2.cvtColor(cv2.imread(os.path.join(HERE, 'interior2/card_night.png')), cv2.COLOR_BGR2RGB).astype(np.float32)
GL[ys, xs] = cv2.resize(_g, (w, h), interpolation=cv2.INTER_AREA)
ys, xs, h, w = gpx('lamp')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
u, v = xx / w, yy / h
glass = np.array([255, 196, 104], np.float32) * (0.7 + 0.3 * np.exp(-((u - 0.5) / 0.45) ** 2 - ((v - 0.6) / 0.45) ** 2))[..., None]
flame_r = np.hypot((u - 0.5) / 0.13, (v - 0.62) / 0.24 * np.where(v < 0.62, 0.75, 1.3))
flame = np.clip(1.25 - flame_r, 0, 1) ** 0.8
core = np.clip(1 - np.hypot((u - 0.5) / 0.06, (v - 0.68) / 0.1), 0, 1)
img = glass * (1 - flame[..., None]) + np.array([255, 236, 170], np.float32) * flame[..., None]
img = img * (1 - core[..., None]) + np.array([255, 252, 232], np.float32) * core[..., None]
GL[ys, xs] = img
GL[gpx('bulb')[0], gpx('bulb')[1]] = np.array([255, 240, 190], np.float32)
GL = np.clip(GL, 0, 255).astype(np.uint8)
cv2.imwrite(f'{OUT}/beedle_tent_glow_256.png', cv2.cvtColor(cv2.resize(GL, (256, 256), interpolation=cv2.INTER_AREA), cv2.COLOR_RGB2BGR))
cv2.imwrite(f'{OUT}/beedle_tent_glow_512.png', cv2.cvtColor(GL, cv2.COLOR_RGB2BGR))
print('textures written')
