"""Beedle's tent interior, v2: built from the game's own assets wherever possible.

Our own: the tent shell (striped canvas from inside), curtain, windows, display cushions.
From Twilight Princess (your ROM dump): wood textures for the counter, shelves, poles and frames
(Ordon house walls, shelving, pillars), the floor dirt, the rugs (Kakariko woven runner, the doctor's
round rug), and real prop models: barrels, crates, clay pots, tables, a chair, a bench, a candlestick,
a lamp pot and the hanging lantern. Props are listed as placements (the game draws them from its own
archives; nothing is copied).

Writes interior2.pkl: (tris, props, layout). tris are (pos, uv, texname); texname 'ATLAS' is our atlas,
anything else is a vanilla texture by name (repeating UVs).
Usage: CLOTH=cream python3 -I interior2.py OUT_DIR"""
import sys, os, math, pickle, numpy as np, cv2
OUT = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
rng = np.random.default_rng(31)
W, L, Hw, Hr = 800.0, 1000.0, 230.0, 540.0
ZF, ZB = L / 2, -L / 2
STRIPE = 36.0 * 1.6
DOOR_W, DOOR_H = 170.0, 260.0
d = DOOR_W / 2
V3 = lambda x, y, z: np.array([x, y, z], float)

# ---------------- our atlas (shell, curtain, windows, cushions) ----------------
S = 2048
R = {
    'roofL': (0, 0, 1024, 512), 'roofR': (1024, 0, 2048, 512), 'wall': (0, 512, 1024, 768),
    'gable': (1024, 512, 1792, 1024), 'curtain': (1792, 512, 2048, 1024),
    'pane': (1536, 1344, 2048, 1472), 'swatch': (1024, 1408, 1536, 1664), 'outside': (0, 1792, 512, 2048),
    'iron': (512, 1792, 1024, 2048), 'cloth': (1792, 1472, 2048, 1664),
}
def uvmap(region, u, v, pad=3):
    x0, y0, x1, y1 = R[region]
    return np.array([(x0 + pad + u * (x1 - x0 - 2 * pad)) / S, (y0 + pad + v * (y1 - y0 - 2 * pad)) / S])
SW = ['cushion', 'paper', 'leather', 'herb', 'pepper', 'brass']
def sw(name, u, v):
    i = SW.index(name); cx, cy = i % 8, i // 8
    x0, y0 = R['swatch'][0] + cx * 64, R['swatch'][1] + cy * 64
    return np.array([(x0 + 4 + u * 56) / S, (y0 + 4 + v * 56) / S])

TRIS = []
CUR = ['ATLAS']
def tri(p, uv, tex=None): TRIS.append((np.array(p, float), np.array(uv, float), tex or CUR[0]))
def quad(p00, p10, p11, p01, uv00, uv10, uv11, uv01, tex=None):
    tri([p00, p10, p11], [uv00, uv10, uv11], tex); tri([p00, p11, p01], [uv00, uv11, uv01], tex)

# ---------------- helpers for vanilla (repeating) textures ----------------
def vquad(a, b, c, dd, tex, tile, swap=False, off=None):
    """Quad with world-scale repeating UVs (tile = game units per texture repeat)."""
    e1, e2 = b - a, dd - a
    l1, l2 = np.linalg.norm(e1), np.linalg.norm(e2)
    if swap: l1, l2 = l2, l1
    o = rng.uniform(0, 1, 2) if off is None else off
    if not swap:
        uv = [(0, 0), (l1 / tile, 0), (l1 / tile, l2 / tile), (0, l2 / tile)]
    else:
        uv = [(0, 0), (0, l1 / tile), (l2 / tile, l1 / tile), (l2 / tile, 0)]
    uv = [np.array(x) + o for x in uv]
    quad(a, b, c, dd, *uv, tex=tex)
def vbox(c, size, tex, tile, yaw=0.0, top=None):
    hx, hy, hz = np.array(size) / 2
    cy, sy = math.cos(yaw), math.sin(yaw)
    Rm = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    P = lambda x, y, z: np.asarray(c) + Rm @ np.array([x, y, z])
    faces = [
        ('top', [P(-hx, hy, hz), P(hx, hy, hz), P(hx, hy, -hz), P(-hx, hy, -hz)]),
        ('bottom', [P(-hx, -hy, -hz), P(hx, -hy, -hz), P(hx, -hy, hz), P(-hx, -hy, hz)]),
        ('front', [P(-hx, -hy, hz), P(hx, -hy, hz), P(hx, hy, hz), P(-hx, hy, hz)]),
        ('back', [P(hx, -hy, -hz), P(-hx, -hy, -hz), P(-hx, hy, -hz), P(hx, hy, -hz)]),
        ('left', [P(-hx, -hy, -hz), P(-hx, -hy, hz), P(-hx, hy, hz), P(-hx, hy, -hz)]),
        ('right', [P(hx, -hy, hz), P(hx, -hy, -hz), P(hx, hy, -hz), P(hx, hy, hz)]),
    ]
    for name, (a, b, c_, dd) in faces:
        e1, e2 = np.linalg.norm(b - a), np.linalg.norm(dd - a)
        t = top if (name == 'top' and top) else tex
        # grain runs along the longer edge of each face
        vquad(a, b, c_, dd, t, tile, swap=e2 > e1)
def vtube(path, radius, tex, tile, sides=7, along_v=False, cap=True):
    """Tube with world-scale repeating texture: length along u (or v), around the other way."""
    path = [np.asarray(p, float) for p in path]
    n = len(path)
    lengths = np.r_[0, np.cumsum([np.linalg.norm(path[i + 1] - path[i]) for i in range(n - 1)])]
    total = lengths[-1] or 1
    rings = []
    for i in range(n):
        dd = path[min(i + 1, n - 1)] - path[max(i - 1, 0)]; dd /= np.linalg.norm(dd)
        up = np.array([0, 1, 0]) if abs(dd[1]) < 0.9 else np.array([1, 0, 0])
        a = np.cross(dd, up); a /= np.linalg.norm(a); b = np.cross(dd, a)
        r = radius(lengths[i] / total) if callable(radius) else radius
        rings.append([path[i] + r * (math.cos(2 * math.pi * k / sides) * a + math.sin(2 * math.pi * k / sides) * b) for k in range(sides + 1)])
    for i in range(n - 1):
        for k in range(sides):
            l0, l1 = lengths[i] / tile, lengths[i + 1] / tile
            a0, a1 = k / sides, (k + 1) / sides
            if along_v: uv = [(a0, l0), (a1, l0), (a1, l1), (a0, l1)]
            else: uv = [(l0, a0), (l0, a1), (l1, a1), (l1, a0)]
            quad(rings[i][k], rings[i][k + 1], rings[i + 1][k + 1], rings[i + 1][k], *[np.array(x) for x in uv], tex=tex)
    if cap:
        for e in (0, n - 1):
            for k in range(sides):
                tri([path[e], rings[e][k], rings[e][k + 1]], [np.array((0.5, 0.5))] * 3, tex)

WOOD_WALL, WOOD_TOP, SHELF, PILLAR, BEAM, ROPE = 'H_woodenwall02', 'H_wood03', 'H_shelf', 'H_hashira', 'H_wood03', 'H_rope'

# ======================= room shell (ours) =======================
def roof_pt(sgn, t, s):
    z = ZB + t * L; x = sgn * s * W / 2; y = Hr + (Hw - Hr) * s
    sag = 14.0 * math.sin(math.pi * s) * (0.5 + 0.5 * math.sin(math.pi * t))
    nx, ny = sgn * (Hr - Hw), W / 2; ln = math.hypot(nx, ny)
    return np.array([x - nx / ln * sag, y - ny / ln * sag, z])
for sgn, reg in ((-1, 'roofL'), (1, 'roofR')):
    for i in range(14):
        for j in range(8):
            t0, t1, s0, s1 = i / 14, (i + 1) / 14, j / 8, (j + 1) / 8
            quad(roof_pt(sgn, t0, s0), roof_pt(sgn, t1, s0), roof_pt(sgn, t1, s1), roof_pt(sgn, t0, s1),
                 uvmap(reg, t0, s0), uvmap(reg, t1, s0), uvmap(reg, t1, s1), uvmap(reg, t0, s1))
for sgn in (-1, 1):
    for i in range(14):
        for j in range(4):
            t0, t1, s0, s1 = i / 14, (i + 1) / 14, j / 4, (j + 1) / 4
            P = [V3(sgn * W / 2, Hw * (1 - s), ZB + t * L) for t, s in ((t0, s0), (t1, s0), (t1, s1), (t0, s1))]
            quad(*P, *[uvmap('wall', t, s) for t, s in ((t0, s0), (t1, s0), (t1, s1), (t0, s1))])
gable_uv = lambda x, y: uvmap('gable', 0.5 + x / W, 1 - y / Hr)
def gable_poly(pts, z):
    P = [V3(x, y, z) for x, y in pts]; c = sum(P) / len(P)
    for a, b in zip(P, P[1:] + P[:1]):
        tri([c, a, b], [gable_uv(c[0], c[1]), gable_uv(a[0], a[1]), gable_uv(b[0], b[1])])
gable_poly([(-W / 2, 0), (W / 2, 0), (W / 2, Hw), (0, Hr), (-W / 2, Hw)], ZB)
gable_poly([(-W / 2, 0), (-d, 0), (-d, DOOR_H), (-W / 2, Hw)], ZF)
gable_poly([(d, 0), (W / 2, 0), (W / 2, Hw), (d, DOOR_H)], ZF)
gable_poly([(-W / 2, Hw), (-d, DOOR_H), (d, DOOR_H), (W / 2, Hw), (0, Hr)], ZF)
quad(V3(-d, 0, ZF + 30), V3(d, 0, ZF + 30), V3(d, DOOR_H, ZF + 30), V3(-d, DOOR_H, ZF + 30),
     uvmap('outside', 0, 1), uvmap('outside', 1, 1), uvmap('outside', 1, 0), uvmap('outside', 0, 0))
def drape(sgn):
    cols, rows = 10, 10
    for i in range(cols):
        for j in range(rows):
            def pt(ci, rj):
                f = ci / cols; g = rj / rows
                gap = 14 + 46 * g ** 1.6
                return V3(sgn * (gap + (d + 18 - gap) * f), (DOOR_H + 12) * (1 - g), ZF - 6 - 4 * math.sin(f * cols * math.pi * 0.9 + sgn))
            quad(pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), pt(i, j + 1),
                 *[uvmap('curtain', a / cols, b / rows) for a, b in ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))])
drape(-1); drape(1)
vtube([V3(-d - 24, DOOR_H + 16, ZF - 8), V3(d + 24, DOOR_H + 16, ZF - 8)], 3.0, BEAM, 60, sides=6)
def window(c, right, w_, h_):
    up = V3(0, 1, 0)
    P = [c - right * w_ / 2 - up * h_ / 2, c + right * w_ / 2 - up * h_ / 2, c + right * w_ / 2 + up * h_ / 2, c - right * w_ / 2 + up * h_ / 2]
    quad(*P, uvmap('pane', 0, 1), uvmap('pane', 1, 1), uvmap('pane', 1, 0), uvmap('pane', 0, 0))
    for a, b in zip(P, P[1:] + P[:1]):
        vtube([a, b], 3.4, BEAM, 60, sides=6)
    vtube([P[0], P[2]], 0.9, ROPE, 30, sides=4, cap=False); vtube([P[1], P[3]], 0.9, ROPE, 30, sides=4, cap=False)
for sgn in (-1, 1):
    for z in (-200.0, 200.0):
        window(V3(sgn * (W / 2 - 1.5), 130, z), V3(0, 0, sgn), 130, 92)
window(V3(0, 300, ZB + 1.5), V3(1, 0, 0), 120, 90)

# ======================= floor and rugs (vanilla textures) =======================
vquad(V3(-W / 2, 0, ZF), V3(W / 2, 0, ZF), V3(W / 2, 0, ZB), V3(-W / 2, 0, ZB), 'H_field02', 180, off=np.zeros(2))
# Kakariko woven rugs end to end down the aisle (each shows the whole rug once)
for z0, z1 in ((ZF - 30, ZF - 360), (ZF - 370, ZB + 250)):
    quad(V3(-90, 0.8, z0), V3(90, 0.8, z0), V3(90, 0.8, z1), V3(-90, 0.8, z1),
         np.array([0, 1.0]), np.array([0, 0.0]), np.array([1, 0.0]), np.array([1, 1.0]), tex='U_cloth_002_2')
# the doctor's round rug in front of the counter
quad(V3(-130, 0.9, ZB + 245), V3(130, 0.9, ZB + 245), V3(130, 0.9, ZB + 225 - 260 + 20), V3(-130, 0.9, ZB + 225 - 260 + 20),
     np.array([0.0, 0.5]), np.array([0.5, 0.5]), np.array([0.5, 0.0]), np.array([0.0, 0.0]), tex='D_jutan')

# ======================= structure (vanilla pillar wood) =======================
vtube([V3(0, 0, ZB + 55), V3(0, Hr + 4, ZB + 55)], 8.0, PILLAR, 120, sides=8, along_v=True)
for sx in (-1, 1):
    foot = V3(sx * (DOOR_W / 2 + 45), 0, ZF - 40); top = V3(0, Hr - 2, ZF - 40); dd = top - foot
    vtube([foot, top + dd / np.linalg.norm(dd) * 14], 7.5, PILLAR, 120, sides=8, along_v=True)
vtube([V3(0, Hr - 6, ZB + 4), V3(0, Hr - 6, ZF - 4)], 6.5, PILLAR, 120, sides=8)
vtube([V3(10 * math.cos(a), Hr - 8 + 6 * math.sin(a), ZF - 40 + 8 * math.sin(a)) for a in np.linspace(0, 4 * math.pi, 15)], 1.4, ROPE, 30, sides=4, cap=False)

# ======================= counter (Ordon house wood) =======================
CZ = ZB + 165
vbox(V3(0, 46, CZ), (380, 92, 54), WOOD_WALL, 90)
vbox(V3(0, 95, CZ - 2), (404, 6, 70), WOOD_TOP, 120)
for x in (-190, 190):
    vbox(V3(x, 46, CZ + 28), (10, 92, 6), WOOD_TOP, 120)
box_paper = lambda c, s, yaw: None
# ledger (ours: leather cover, paper)
def sbox(c, size, cell, yaw=0.0):
    hx, hy, hz = np.array(size) / 2; cy, sy = math.cos(yaw), math.sin(yaw)
    Rm = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]); P = lambda x, y, z: np.asarray(c) + Rm @ np.array([x, y, z])
    for f in ([P(-hx, hy, hz), P(hx, hy, hz), P(hx, hy, -hz), P(-hx, hy, -hz)], [P(-hx, -hy, hz), P(hx, -hy, hz), P(hx, hy, hz), P(-hx, hy, hz)],
              [P(hx, -hy, -hz), P(-hx, -hy, -hz), P(-hx, hy, -hz), P(hx, hy, -hz)], [P(-hx, -hy, -hz), P(-hx, -hy, hz), P(-hx, hy, hz), P(-hx, hy, -hz)],
              [P(hx, -hy, hz), P(hx, -hy, -hz), P(hx, hy, -hz), P(hx, hy, hz)]):
        quad(*f, sw(cell, 0, 1), sw(cell, 1, 1), sw(cell, 1, 0), sw(cell, 0, 0))
sbox(V3(-120, 100.5, CZ + 4), (34, 5, 26), 'leather', 0.2)
sbox(V3(-120, 103.4, CZ + 4), (30, 0.8, 23), 'paper', 0.2)

# ======================= side shelves (vanilla shelving wood) =======================
SHELF_LEVELS = (8, 80, 152, 224)
def shelf_unit(x, cz, side):
    dpt, ln = 50, 230
    for zz in (cz - ln / 2 + 4, cz + ln / 2 - 4):
        for xx in (x - side * 4, x - side * (dpt - 4)):
            vbox(V3(xx, 132, zz), (7, 264, 7), SHELF, 60)
    for yv in SHELF_LEVELS + (262,):
        vbox(V3(x - side * dpt / 2, yv, cz), (dpt, 5, ln), SHELF, 60)
    vbox(V3(x - side * 1, 132, cz), (2, 264, ln), WOOD_WALL, 90)
    return [(x - side * dpt / 2, yv + 2.5, cz, side) for yv in SHELF_LEVELS] + [(x - side * dpt / 2, 264.5, cz, side)]
SHELVES = []
for side in (-1, 1):
    for cz in (-170.0, 110.0):
        SHELVES.append(shelf_unit(side * (W / 2 - 6), cz, side))

# ======================= display cushions (on vanilla tables, placed as props) =======================
def lathe(center, profile, cell, sides=12):
    for k in range(sides):
        a0, a1 = 2 * math.pi * k / sides, 2 * math.pi * (k + 1) / sides
        for (y0, r0), (y1, r1) in zip(profile[:-1], profile[1:]):
            p = [center + V3(r0 * math.cos(a0), y0, r0 * math.sin(a0)), center + V3(r0 * math.cos(a1), y0, r0 * math.sin(a1)),
                 center + V3(r1 * math.cos(a1), y1, r1 * math.sin(a1)), center + V3(r1 * math.cos(a0), y1, r1 * math.sin(a0))]
            vy0 = y0 / profile[-1][0]; vy1 = y1 / profile[-1][0]
            quad(*p, sw(cell, k / sides, 1 - vy0), sw(cell, (k + 1) / sides, 1 - vy0), sw(cell, (k + 1) / sides, 1 - vy1), sw(cell, k / sides, 1 - vy1))
TABLE_SCALE = 0.62                     # HTable is 125 x 104 x 120; this makes a waist-high stand
TABLE_TOP = 104 * TABLE_SCALE
STANDS = []
PROPS = []   # (archive, model, pos, yaw_deg, scale)
for x in (-130.0, 130.0):
    for z in (-120.0, 60.0, 240.0):
        PROPS.append(('HTable', 'ytable.bmd', V3(x, 0, z), 0.0, TABLE_SCALE))
        lathe(V3(x, TABLE_TOP + 0.5, z), [(0, 18), (3, 19), (6, 15), (7, 0)], 'cushion')
        STANDS.append(V3(x, TABLE_TOP + 7.5, z))

# ======================= props from the game =======================
# hanging lanterns: the game's own lantern (the one the passers carry)
LAMPS = []
for z in (-330.0, 0.0, 330.0):
    top = V3(0, Hr - 12, z); c = V3(0, 330, z)
    vtube([top, c + V3(0, 30, 0)], 0.9, ROPE, 30, sides=4, cap=False)
    PROPS.append(('Obj_kntr', 'ob_kantera.bmd', c, 0.0, 1.6))
    LAMPS.append(c)
# counter: candlestick and a lamp pot
PROPS.append(('HShokudai', 'h_shokudai.bmd', V3(150, 98, CZ - 6), 0.0, 0.42))
PROPS.append(('PPolamp', 'p_polamp.bmd', V3(-40, 98, CZ - 4), 0.0, 0.32))
# by the door: the chair he never sits on, and a bench for customers
PROPS.append(('HChair', 'ychair.bmd', V3(250, 0, ZF - 85), -35.0, 0.85))
PROPS.append(('Nagaisu', 'ynagaisu.bmd', V3(-W / 2 + 60, 0, ZF - 120), 90.0, 0.8))
# barrels and crates on the floor between and around the shelves
floor_props = [
    ('HBarrel', 'ytaru.bmd', V3(-W / 2 + 45, 0, -20), 0.0, 0.55),
    ('J_taru00', 'j_taru_00.bmd', V3(-W / 2 + 40, 0, 255), 20.0, 0.62),
    ('Kkiba_00', 'j_hako_00.bmd', V3(W / 2 - 45, 0, -20), 12.0, 0.45),
    ('Kkiba_00', 'j_hako_00.bmd', V3(W / 2 - 42, 68, -14), -8.0, 0.36),
    ('Obj_kbox', 'k_skiba_00.bmd', V3(W / 2 - 60, 0, 290), 35.0, 0.32),
    ('HBarrel', 'ytaru.bmd', V3(-W / 2 + 160, 0, ZB + 50), 0.0, 0.5),
    ('Kkiba_00', 'j_hako_00.bmd', V3(W / 2 - 150, 0, ZB + 45), 5.0, 0.42),
    ('O_tuboB', 'o_tubob_lv8.bmd', V3(-W / 2 + 120, 0, ZF - 40), 0.0, 0.7),
    ('D_aotubo0', 'd_aotubo00.bmd', V3(W / 2 - 45, 0, ZF - 160), 0.0, -70),
]
PROPS += floor_props
# pots on the shelves (scaled to sit between the boards); Wind Waker curios get some spots
pots = [('J_tubo_00', 'j_tubo_00.bmd', -46), ('J_tubo_01', 'j_tubo_01.bmd', -44), ('K_tubo02', 'k_tubo02.bmd', -42),
        ('O_tuboS', 'o_tubos_lv8.bmd', -40), ('uri_kago', 'baby_kago.bmd', -36), ('D_aotubo0', 'd_aotubo00.bmd', -44),
        ('Obj_otubo', 'x_oiltubo_00.bmd', -40)]
WW_SPOTS = []
for ui, unit in enumerate(SHELVES):
    for li, (sx, sy, sz, side) in enumerate(unit):
        if li == 4:
            PROPS.append(('Kkiba_00', 'j_hako_00.bmd', V3(sx, sy, sz - 60), 10.0 * side, -42))
            PROPS.append(('HBarrel', 'ytaru.bmd', V3(sx, sy, sz + 55), 0.0, -44))
            continue
        zs = np.linspace(sz - 85, sz + 85, 5)
        for k, zz in enumerate(zs):
            if (li == 1 and k in (1, 3)) or (li == 2 and k == 2) or (li == 0 and k == 4 and ui % 2 == 0):
                WW_SPOTS.append((V3(sx, sy, zz), side)); continue
            arc, mdl, sc = pots[(ui * 3 + li * 2 + k) % len(pots)]
            if li == 0 and k % 2 == 0:
                PROPS.append(('Kkiba_00', 'j_hako_00.bmd', V3(sx, sy, zz), 15.0 * k, -40)); continue
            PROPS.append((arc, mdl, V3(sx, sy, zz), 40.0 * k, sc))

# ======================= our extras: spoils bag post, herbs =======================
POST = V3(W / 2 - 70, 0, ZB + 80)
vtube([POST, POST + V3(0, 200, 0)], 5.5, PILLAR, 120, sides=7, along_v=True)
# a wrought-iron hook on the post: a plate nailed to the wood, an arm reaching out toward the room
# and curling up at the end, so the spoils bag's strap has something real to hang from
out = np.array([-0.75, 0, 0.66]); out /= np.linalg.norm(out)     # toward the middle of the room
side = np.cross(out, [0, 1, 0])
plate_c = POST + V3(0, 172, 0) + out * 5.6
pp = [plate_c + side * sx * 4.2 + V3(0, sy * 8, 0) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
quad(*pp, *[np.array(x) for x in ((0, 0), (1, 0), (1, 1), (0, 1))], tex='V_tetu_01')
for sy in (-5.5, 5.5):                                              # two nail heads
    vtube([plate_c + V3(0, sy, 0), plate_c + V3(0, sy, 0) + out * 1.4], 1.3, 'V_tetu_01', 20, sides=6)
arm = [plate_c + V3(0, 3, 0), plate_c + out * 12 + V3(0, 2, 0), plate_c + out * 22 + V3(0, -1, 0),
       plate_c + out * 27 + V3(0, -4, 0), plate_c + out * 29.5 + V3(0, -1, 0), plate_c + out * 30 + V3(0, 4, 0),
       plate_c + out * 28.5 + V3(0, 7, 0)]
vtube(arm, lambda f: 2.2 - 0.9 * f, 'V_tetu_01', 20, sides=6)
HOOK = plate_c + out * 27 + V3(0, -4, 0)                             # bottom of the curl, where the strap sits
for z, kind in ((-150.0, 'herb'), (150.0, 'pepper'), (420.0, 'herb')):
    top = V3(40 if kind == 'herb' else -40, Hr - 70, z)
    vtube([V3(top[0], Hr - 12 - abs(top[0]) * (Hr - Hw) / (W / 2), z), top], 0.6, ROPE, 30, sides=4, cap=False)
    if kind == 'pepper':
        for m in range(8):
            lathe(top - V3(0, 5 + m * 6, 0), [(0, 0.5), (1, 2.4), (5, 2.2), (6, 0.5)], 'pepper', sides=6)
    else:
        for m in range(9):
            a = math.radians(-40 + m * 10)
            vtube([top, top + V3(math.sin(a) * 14, -26 * math.cos(a), math.cos(a * 2) * 3)], 1.2, ROPE, 30, sides=4, cap=False)
            lathe(top + V3(math.sin(a) * 12, -24 * math.cos(a), math.cos(a * 2) * 3) - V3(0, 6, 0), [(0, 0.5), (4, 3), (8, 0.5)], 'herb', sides=5)

LAYOUT = dict(HOOK_DIR=out, WW_SPOTS=WW_SPOTS, STANDS=STANDS, POST=POST, HOOK=HOOK, CZ=CZ, W=W, L=L, Hw=Hw, Hr=Hr, ZF=ZF, ZB=ZB, LAMPS=LAMPS)
pickle.dump((TRIS, PROPS, LAYOUT), open(f'{OUT}/interior2.pkl', 'wb'))
print('interior triangles:', len(TRIS), 'props:', len(PROPS))

# ======================= our atlas =======================
exec(open(os.path.join(HERE, 'paint_lib.py')).read()[:open(os.path.join(HERE, 'paint_lib.py')).read().index('stake_ts =')])
TEAL = np.array([38, 112, 116], np.float32)
def inner_canvas(name, world_of_u, seed, dirt=False):
    ys, xs, h, w = region_px(name)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    weave = WEAVE(h, w, seed, period=4.0); mott = fbm(h, w, seed + 1)
    world = world_of_u(xx[0] / w)
    idx = np.floor((world + STRIPE / 2) / STRIPE); teal = (idx % 2 == 0)[None, :]
    frac = (world + STRIPE / 2) / STRIPE - idx; seam = (np.minimum(frac, 1 - frac) * STRIPE)[None, :]
    img = np.where(teal[..., None], np.array([46, 104, 104], np.float32), np.array([236, 214, 168], np.float32))
    img = img * (weave * (0.92 + 0.12 * (mott - 0.5)))[..., None]
    img *= (1 - 0.35 * np.exp(-(seam / 1.4) ** 2))[..., None]
    if dirt: img *= (1 - 0.35 * np.clip((yy / h - 0.8) / 0.2, 0, 1) ** 1.3)[..., None]
    A[ys, xs] = img
inner_canvas('roofL', lambda u: ZB + u * L, 501); inner_canvas('roofR', lambda u: ZB + u * L, 502)
inner_canvas('wall', lambda u: ZB + u * L, 503, dirt=True); inner_canvas('gable', lambda u: (u - 0.5) * W, 504, dirt=True)
for name in ('roofL', 'roofR'):
    ys, xs, h, w = region_px(name); A[ys, xs] *= (0.72 + 0.28 * (np.arange(h) / h))[:, None, None]
ys, xs, h, w = region_px('curtain')
yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
fold = 0.5 + 0.5 * np.sin(xx / w * 9 * math.pi)
img = np.array([92, 52, 96], np.float32) * (0.55 + 0.5 * fold ** 0.8)[..., None] * duck_weave(h, w, 601, period=2.0)[..., None]
band = (yy > h * 0.88) & (yy < h * 0.96)
A[ys, xs] = np.where(band[..., None], np.array([150, 118, 50], np.float32) * (0.7 + 0.3 * fold[..., None]), img)
SWC = {'cushion': (112, 54, 104), 'paper': (232, 220, 186), 'leather': (110, 64, 40), 'herb': (96, 126, 66), 'pepper': (190, 42, 32), 'brass': (196, 158, 74)}
for i, name in enumerate(SW):
    cx, cy = i % 8, i // 8
    yy, xx = np.mgrid[0:64, 0:64].astype(np.float32)
    shade = 0.8 + 0.3 * np.exp(-((xx - 22) / 26) ** 2 - ((yy - 22) / 26) ** 2)
    if name == 'cushion': shade *= duck_weave(64, 64, 711, period=2.0)
    A[R['swatch'][1] + cy * 64:R['swatch'][1] + cy * 64 + 64, R['swatch'][0] + cx * 64:R['swatch'][0] + cx * 64 + 64] = np.array(SWC[name], np.float32) * shade[..., None]
A_base = A.copy()
def finish(day):
    T = A_base.copy()
    ys, xs, h, w = region_px('pane'); yy, xx = np.mgrid[0:h, 0:w].astype(np.float32); weave = WEAVE(h, w, 801, period=3.0)
    T[ys, xs] = (np.array([255, 226, 160], np.float32) if day else np.array([40, 46, 70], np.float32)) * weave[..., None]
    ys, xs, h, w = region_px('outside'); yy, xx = np.mgrid[0:h, 0:w].astype(np.float32); v = yy / h
    if day: T[ys, xs] = np.where((v > 0.62)[..., None], np.array([118, 150, 72], np.float32), np.array([170, 200, 226], np.float32) * (1 - 0.2 * v[..., None]))
    else: T[ys, xs] = np.where((v > 0.62)[..., None], np.array([22, 32, 30], np.float32), np.array([26, 34, 64], np.float32) * (1 - 0.3 * v[..., None]))
    if not day:
        for name in ('roofL', 'roofR', 'wall', 'gable'):
            ys, xs, h, w = region_px(name); T[ys, xs] *= np.array([0.42, 0.36, 0.30], np.float32)
        for name in ('curtain', 'swatch'):
            ys, xs, h, w = region_px(name); T[ys, xs] *= np.array([0.78, 0.68, 0.55], np.float32)
    return np.clip(T, 0, 255).astype(np.uint8)
for tag, day in (('day', True), ('night', False)):
    cv2.imwrite(f'{OUT}/interior2_{tag}_HD_2048.png', cv2.cvtColor(finish(day), cv2.COLOR_RGB2BGR))
print('textures written')
