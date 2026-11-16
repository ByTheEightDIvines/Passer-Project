"""Low-poly camp tent (A-frame) from the game's own textures, with two door flaps that swing shut.
Writes OUT/camp_tent_tris.pkl (+ colours), OUT/camp_tent_atlas.png, OUT/camp_flap_tris.pkl, OUT/camp_flap.png
and the hinge (apex and ground corner of the left door edge) in OUT/camp_hinge.pkl.
Tent space: door toward +z, ground at y=0; drawn at scale 1.3 in game."""
import sys, os, math, pickle, numpy as np
from PIL import Image
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True)
LIB = pickle.load(open(sys.argv[2] if len(sys.argv) > 2 else '/home/claude/texall.pkl', 'rb'))
W, L, H = 236.0, 250.0, 196.0          # base width, length, ridge height (same footprint as before)
ZF, ZB = L / 2, -L / 2
OV = 9.0                               # the canvas runs a little past the gables
D, DH = 58.0, 150.0                    # doorway: half-width at the ground, height
A = 512
atlas = np.zeros((A, A, 3), np.uint8)
def tex(n):
    t = LIB[n][0].astype(np.float32); a = t[..., 3:4] / 255 if t.shape[2] > 3 else 1
    return t[..., :3] * a + np.array([70, 55, 40]) * (1 - a)      # bare texels onto a dark backing
REG = {}
def region(name, x0, y0, x1, y1, img, tiles=(1, 1)):
    w, h = x1 - x0, y1 - y0
    t = np.tile(img, (tiles[1], tiles[0], 1))
    atlas[y0:y1, x0:x1] = np.asarray(Image.fromarray(t.astype(np.uint8)).resize((w, h), Image.LANCZOS))
    REG[name] = (x0, y0, x1, y1)
BL = LIB['U_cloth_002'][0].astype(np.float32)          # the Kakariko blanket: weave, patterned border, fringe
WEAVE = BL[58:190, 18:110, :3]
WEAVE = np.concatenate([WEAVE, WEAVE[::-1]], 0); WEAVE = np.concatenate([WEAVE, WEAVE[:, ::-1]], 1)   # mirror-tiled: no seams
BAND = BL[196:240, 24:63, :3]                            # one repeat of the border: pattern, then the stripes
FRINGE = BL[238:256, 24:63]                              # RGBA
region('canvas', 0, 0, 256, 512, WEAVE, (1, 2))
BIND = BL[222:240, 24:63, :3]                            # the stripes under the pattern: plain binding
region('bind', 352, 128, 384, 256, np.rot90(BIND, -1))   # u across the binding, v along it
region('band', 480, 0, 512, 512, np.rot90(np.tile(BAND, (1, 6, 1)), -1))   # runs down the atlas: u across, v along
region('pole', 256, 0, 320, 512, tex('U_hasira_04]'))
region('rope', 320, 0, 352, 256, tex('S_Rope'), (1, 2))
region('stake', 352, 0, 480, 128, tex('D_KikoGate_B'))
def uv(name, u, v):
    x0, y0, x1, y1 = REG[name]
    return ((x0 + 0.5 + (x1 - x0 - 1) * min(max(u, 0), 1)) / A, (y0 + 0.5 + (y1 - y0 - 1) * min(max(v, 0), 1)) / A)
TRIS, COLS = [], []
def tri(p, t, c=(1, 1, 1)):
    TRIS.append((np.array(p, float), np.array(t, float))); COLS.append(np.array([list(c) + [1]] * 3, float))
def quad(p, t, c=(1, 1, 1)):
    tri([p[0], p[1], p[2]], [t[0], t[1], t[2]], c); tri([p[0], p[2], p[3]], [t[0], t[2], t[3]], c)
V = lambda x, y, z: np.array([x, y, z], float)
SL = math.hypot(W / 2, H)
BW = 24.0                              # border width on the cloth
BS = 1 - BW / SL                       # where the hem border starts down the slope
# ---- roof slopes: 2 x 2 grid, a soft sag between the poles, the hem flared out ----
def slope(sgn, t, s):          # t along the length 0..1 (back to front), s down the slope 0..1
    z = ZB + t * L
    sag = 5.0 * math.sin(math.pi * t) * math.sin(math.pi * s)
    flare = 0.0
    x, y = W / 2 * s, H * (1 - s)
    n = np.array([H, W / 2]) / SL                                   # outward normal of the slope (x, y)
    return V(sgn * (x - n[0] * sag + flare), y - n[1] * sag, z)
for sgn in (-1, 1):
    SR = [0.0, 0.5, BS, 1.0]
    for i in range(2):
        for j in range(3):
            t0, t1, s0, s1 = i / 2, (i + 1) / 2, SR[j], SR[j + 1]
            P = [slope(sgn, t0, s0), slope(sgn, t1, s0), slope(sgn, t1, s1), slope(sgn, t0, s1)]
            if j == 2:     # border: pattern above, stripes at the hem; repeats along the length
                U = [uv('band', 1, 0), uv('band', 1, 1), uv('band', 0, 1), uv('band', 0, 0)]   # one run per half length
            else:
                U = [uv('canvas', t0, s0 / BS), uv('canvas', t1, s0 / BS), uv('canvas', t1, s1 / BS), uv('canvas', t0, s1 / BS)]
            shade = (0.92, 0.9, 0.86) if sgn < 0 else (1, 1, 1)
            quad(P, U, shade)
            quad([p + np.array([-sgn * 1.5, -1.5, 0]) for p in P], U, (0.42, 0.38, 0.33))   # inside face, in shadow
# ---- gables ----
def guv(x, y): return uv('canvas', 0.5 + x / W, 1 - y / H)
HB = BW * H / SL                       # the border's height on the gables, matching the slopes
RUN = L / 2                            # cloth length one border run covers (as on the slopes)
def bstrip(x0, x1, y_top_x0, y_top_x1, z, c=(1, 1, 1)):
    """border along the bottom of a gable, from x0 to x1 (top corners at the given x), pattern up, stripes down"""
    v0, v1 = 0.0, abs(x1 - x0) / RUN     # x0 is the corner: the pattern starts there, as on the slope
    quad([V(x0, 0, z), V(x1, 0, z), V(y_top_x1, HB, z), V(y_top_x0, HB, z)],
         [uv('band', 0, v0), uv('band', 0, v1), uv('band', 1, v1), uv('band', 1, v0)], c)
ex = W / 2 * (1 - HB / H)              # gable edge at the border's top
BWD = 5.0                              # binding width
def bind(p, q, inward, z, out=TRIS, w=BWD):
    """binding along the edge p->q (x, y) on a face at depth z, reaching `inward` (unit x, y) onto the face"""
    p, q, n = np.array(p, float), np.array(q, float), np.array(inward, float)
    P = [V(*p, z), V(*q, z), V(*(q + n * w), z), V(*(p + n * w), z)]
    U = [uv('bind', 0, 0), uv('bind', 0, 1), uv('bind', 1, 1), uv('bind', 1, 0)]
    if out is TRIS: quad(P, U)
    else:
        out.append((np.array(P[:3]), np.array(U[:3]))); out.append((np.array([P[0], P[2], P[3]]), np.array([U[0], U[2], U[3]])))
def unit(v): v = np.array(v, float); return v / np.linalg.norm(v)
def gable_bindings(z, door):
    for sgn in (-1, 1):   # the slanted outer edges, from the top of the border to the ridge
        bind((sgn * ex, HB), (0, H), unit((-sgn * H, -W / 2)), z)
        if door:          # the cut edges of the doorway
            bind((sgn * D * (1 - HB / DH), HB), (0, DH), unit((sgn * DH, D)), z)
for sgn in (-1, 1):
    bstrip(sgn * W / 2, 0, sgn * ex, 0, ZB, (0.85, 0.82, 0.78))
tri([V(0, H, ZB), V(-ex, HB, ZB), V(ex, HB, ZB)], [guv(0, H), guv(-ex, HB), guv(ex, HB)], (0.85, 0.82, 0.78))
tri([V(0, H, ZB + 1.5), V(W / 2, 0, ZB + 1.5), V(-W / 2, 0, ZB + 1.5)], [guv(0, H), guv(W / 2, 0), guv(-W / 2, 0)], (0.4, 0.36, 0.31))
for sgn in (-1, 1):            # front, around the doorway
    dx = D * (1 - HB / DH)             # door edge at the border's top
    bstrip(sgn * W / 2, sgn * D, sgn * ex, sgn * dx, ZF)
    up = [V(sgn * ex, HB, ZF), V(sgn * dx, HB, ZF), V(0, DH, ZF), V(0, H, ZF)]
    quad(up, [guv(p[0], p[1]) for p in up])
    pts = [V(sgn * W / 2, 0, ZF), V(sgn * D, 0, ZF), V(0, DH, ZF), V(0, H, ZF)]
    quad([p - np.array([0, 0, 1.5]) for p in pts[::-1]], [guv(p[0], p[1]) for p in pts[::-1]], (0.4, 0.36, 0.31))
# ---- groundsheet ----
quad([V(-W / 2 + 8, 0.6, ZB + 4), V(W / 2 - 8, 0.6, ZB + 4), V(W / 2 - 8, 0.6, ZF - 2), V(-W / 2 + 8, 0.6, ZF - 2)],
     [uv('canvas', 0, 0), uv('canvas', 1, 0), uv('canvas', 1, 1), uv('canvas', 0, 1)], (0.3, 0.27, 0.23))
# ---- poles: a ridge pole, crossed poles along the doorway, one upright at the back ----
def rod(a, b, r, reg, sides=5, tile=1.0, c=(1, 1, 1)):
    a, b = np.asarray(a, float), np.asarray(b, float); d = b - a; ln = np.linalg.norm(d); d /= ln
    e = np.cross(d, [0, 1, 0] if abs(d[1]) < 0.9 else [1, 0, 0]); e /= np.linalg.norm(e); f = np.cross(d, e)
    ring = lambda p: [p + r * (math.cos(2 * math.pi * k / sides) * e + math.sin(2 * math.pi * k / sides) * f) for k in range(sides + 1)]
    A_, B_ = ring(a), ring(b)
    for k in range(sides):
        quad([A_[k], A_[k + 1], B_[k + 1], B_[k]], [uv(reg, k / sides, 0), uv(reg, (k + 1) / sides, 0), uv(reg, (k + 1) / sides, tile), uv(reg, k / sides, tile)], c)
rod(V(0, H + 3, ZB - OV - 8), V(0, H + 3, ZF + OV + 8), 3.2, 'pole', tile=1)
for sgn in (-1, 1):
    rod(V(sgn * (D + 8), 0, ZF - 6), V(-sgn * 10, H + 16, ZF - 6), 3.0, 'pole', tile=0.6)
rod(V(0, 0, ZB + 6), V(0, H + 12, ZB + 6), 3.0, 'pole', tile=0.6)
# ---- guy ropes and stakes ----
def stake(p):
    rod(p - V(0, 3, 0), p + V(0, 12, 0), 1.6, 'stake', sides=4, c=(0.8, 0.75, 0.7))
for a, b in ((V(0, H + 3, ZF + OV + 6), V(-95, 0, ZF + 70)), (V(0, H + 3, ZF + OV + 6), V(95, 0, ZF + 70)),
             (V(0, H + 3, ZB - OV - 6), V(0, 0, ZB - 85))):
    rod(a, b + V(0, 9, 0), 0.7, 'rope', sides=3, tile=6); stake(b)
for sgn in (-1, 1):            # hem pegs
    for z in (ZB + 10, 0.0, ZF - 10):
        stake(V(sgn * (W / 2 + 9), 0, z))
pickle.dump(dict(tris=TRIS, cols=COLS), open(f'{OUT}/camp_tent_tris.pkl', 'wb'))
Image.fromarray(atlas).save(f'{OUT}/camp_tent_atlas.png')
# ---- the flap (left one; the right is its mirror): the same blanket, hung from the door's slanted edge,
# its border along the whole bottom (fringe below it) and up the free edge where the two flaps meet ----
# The flap uses the tent's own atlas and the same mapping as the front gable behind it, so its weave and
# its bottom border line up with the canvas around the doorway when it's shut.
Image.fromarray(atlas).save(f'{OUT}/camp_flap.png')
FL = []
ZFL = ZF + 1.2
# The left flap: its outer edge runs up the doorway, its inner edge stands upright at the middle of the door.
# Border along the bottom, border up the inner edge (the two flaps' borders meet in the middle), mitred where
# they meet, and the upright border ends square under a stripe hem; plain weave above that up to the apex.
EW = 16.0                               # width of the upright border on each flap
YT = 104.0                              # top of the upright border
CAP = 5.0                               # the square hem across its top
apex, corner = V(0, DH + 6, ZFL), V(-D - 6, 0, ZFL)
HAP = apex[1]
XI = 0.0                                # the inner edge
xl = lambda y: corner[0] * (1 - y / HAP)
def fq(P, U):
    P = [V(x, y, ZFL) for x, y in P]
    FL.append((np.array(P[:3]), np.array(U[:3])))
    if len(P) == 4: FL.append((np.array([P[0], P[2], P[3]]), np.array([U[0], U[2], U[3]])))
def bv(x): return (W / 2 + x) / RUN    # bottom border phase, carrying on from the gable's strip
def vv(y): return y / RUN              # upright border phase
xb = XI - EW                           # inner side of the upright border
# bottom border, up to the mitre
fq([(xl(0), 0), (xb, 0), (xb, HB), (xl(HB), HB)],
   [uv('band', 0, bv(xl(0))), uv('band', 0, bv(xb)), uv('band', 1, bv(xb)), uv('band', 1, bv(xl(HB)))])
# the mitred corner: split on the diagonal from the inner corner to the outer corner
fq([(xb, 0), (XI, 0), (xb, HB)], [uv('band', 0, bv(xb)), uv('band', 0, bv(XI)), uv('band', 1, bv(xb))])
fq([(XI, 0), (XI, HB), (xb, HB)], [uv('band', 0, vv(0)), uv('band', 0, vv(HB)), uv('band', 1, vv(HB))])
# upright border: pattern inside, stripes at the inner edge, all the way up to where the flap's top edge cuts it
ys = HAP * (1 + xb / corner[0] * -1) if False else HAP * (1 - xb / corner[0])   # where the top edge crosses its inner side
bu = lambda x, y: uv('band', (XI - x) / EW, vv(y))
fq([(xb, HB), (XI, HB), (XI, HAP), (xb, ys)], [bu(xb, HB), bu(XI, HB), bu(XI, HAP), bu(xb, ys)])
# weave beside it, up to where it meets the top edge
fq([(xl(HB), HB), (xb, HB), (xb, ys)], [guv(xl(HB), HB), guv(xb, HB), guv(xb, ys)])
pickle.dump(dict(tris=FL, apex=apex, corner=corner), open(f'{OUT}/camp_flap_tris.pkl', 'wb'))
print('tent tris', len(TRIS), 'flap tris', len(FL))
