"""Bake the room onto the inside of a plain box (floor, back wall, side walls, roof panels), as seen
from the doorway at Link's eye height. Writes room_box.pkl (tris in world units, with UVs into the
1024 atlas) and room_day.png / room_night.png."""
import sys, math, pickle, numpy as np
from PIL import Image
sys.path[:0] = ['/home/claude/room', '/home/claude/passer-project/tools/tents']
import scene
from persp import render, look_at
L = scene.LAY
X, ZB, ZF, Hw, Hr = L['W'] / 2, L['ZB'], L['ZF'], L['Hw'], L['Hr']
ZV = ZF - 70.0 * 1.6            # the curtain line (the doorway's shallow entry stays real, in front of it)
E = np.array([0.0, 165.0, ZF - 20.0])
A = 1024
# face: atlas rect (x0,y0,x1,y1) and P(u,v) -> world point; v=0 is the top of the region
def lerp(a, b, t): return a + (b - a) * t
FACES = {
    'floor': ((0, 0, 512, 512), lambda u, v: np.stack([lerp(-X, X, u), np.full_like(u, 0.6), lerp(ZB, ZV, v)], -1)),
    'back':  ((512, 0, 1024, 352), lambda u, v: np.stack([lerp(-X, X, u), lerp(Hr, 0.0, v), np.full_like(u, ZB + 0.6)], -1)),
    'sideL': ((512, 352, 1024, 480), lambda u, v: np.stack([np.full_like(u, -X + 0.6), lerp(Hw, 0.0, v), lerp(ZB, ZV, u)], -1)),
    'sideR': ((512, 480, 1024, 608), lambda u, v: np.stack([np.full_like(u, X - 0.6), lerp(Hw, 0.0, v), lerp(ZV, ZB, u)], -1)),
    # roof panels: eave (v=1) up to the ridge (v=0), along the length (u)
    'roofL': ((0, 512, 512, 768), lambda u, v: np.stack([lerp(0.0, -X, v), lerp(Hr, Hw, v) - 0.6, lerp(ZB, ZV, u)], -1)),
    'roofR': ((0, 768, 512, 1024), lambda u, v: np.stack([lerp(0.0, X, v), lerp(Hr, Hw, v) - 0.6, lerp(ZV, ZB, u)], -1)),
    'front': ((512, 608, 1024, 960), lambda u, v: np.stack([lerp(X, -X, u), lerp(Hr, 0.0, v), np.full_like(u, ZV - 0.6)], -1)),
}
# where each face is seen from: the doorway, except the floor (straight down, so the rugs hold from any
# angle) and the doorway end of the box (from the back of the room, looking toward the door)
EYES = {'floor': np.array([0.0, 1600.0, (ZB + ZV) / 2 + 350.0]), 'front': np.array([0.0, 165.0, ZB + 40.0])}
PENTA = {'back', 'front'}   # gable ends: cut to the tent's cross-section
def bake(night):
    S, T = scene.build(night)
    S_floor = [t for t in S if np.asarray(t[1])[:, 1].min() < Hw]   # the floor bake looks down: no roof, no hanging lamps
    atlas = np.zeros((A, A, 3), np.uint8)
    for name, ((x0, y0, x1, y1), Pf) in FACES.items():
        w, h = x1 - x0, y1 - y0
        u, v = np.meshgrid((np.arange(w) + 0.5) / w, (np.arange(h) + 0.5) / h)
        P = Pf(u, v)
        c = P.reshape(-1, 3).mean(0)
        eye = EYES.get(name, E)
        V = look_at(eye, c)
        Pc = np.c_[P.reshape(-1, 3), np.ones(P.size // 3)] @ V.T
        zc = -Pc[:, 2]
        ang = np.degrees(np.arctan(np.max(np.abs(Pc[:, :2]) / np.maximum(zc[:, None], 1e-3))))
        fov = min(160.0, 2 * ang + 4)
        N = 1024
        img = np.asarray(render(S_floor if name == 'floor' else S, T, eye=eye, target=c, fov=fov, W=N, H=N, ss=1), float)
        f = 1 / math.tan(math.radians(fov) / 2)
        xi = (Pc[:, 0] / zc * f) * (N / 2) + N / 2
        yi = -(Pc[:, 1] / zc * f) * (N / 2) + N / 2
        col = img[np.clip(yi.astype(int), 0, N - 1), np.clip(xi.astype(int), 0, N - 1)]
        atlas[y0:y1, x0:x1] = col.reshape(h, w, 3).astype(np.uint8)
        print(name, 'fov %.0f' % fov, flush=True)
    return atlas
def build_box():
    """The box, with holes where the tent has openings: the two windows on each side wall, the back window
    and the curtain opening. Holes are worked out in the tent model's units (export_beedle fits the box
    into the tent at 1/1.6 scale, pulled in 4% about zc), padded a little so the frames cover the cut."""
    zc = (-500.0 + ZV) / 2
    gz = lambda zf: 1.6 * (zc / 1.6 + (zf - zc / 1.6) / 0.96)     # tent model z -> box z
    gy = lambda yf: 1.6 * yf / 0.96
    gx = gy
    PAD = 3.0
    # side windows: tent model z = +-137.5, 74 wide; y 82, 54 high.  back window: x 0, y 102
    side_holes_z = [(gz(c - 37 - PAD), gz(c + 37 + PAD)) for c in (-137.5, 137.5)]
    side_y = (gy(82 - 27 - PAD), gy(82 + 27 + PAD))
    back_x = (gx(-37 - PAD), gx(37 + PAD)); back_y = (gy(102 - 27 - PAD), gy(102 + 27 + PAD))
    door_x = gx(108 + 2)          # the curtain opening, up past the top of the box's front gable
    def cells(name, uh, vh, v_lo=0.0):
        """rect face split into a grid round its holes ((u0,u1),(v0,v1) in face coords); rows from v_lo to 1"""
        (x0, y0, x1, y1), Pf = FACES[name]
        us = sorted({0.0, 1.0, *[a for h in uh for a in h]}); vs = sorted({v_lo, 1.0, *[a for h in vh for a in h]})
        out = []
        for i in range(len(us) - 1):
            for j in range(len(vs) - 1):
                um, vm = (us[i] + us[i + 1]) / 2, (vs[j] + vs[j + 1]) / 2
                if any(a < um < b and c < vm < d for (a, b), (c, d) in zip(uh, vh)): continue
                cu = [(us[i], vs[j]), (us[i + 1], vs[j]), (us[i + 1], vs[j + 1]), (us[i], vs[j + 1])]
                out += poly(name, cu)
        return out
    def poly(name, cu):   # convex polygon in face (u,v), fanned
        (x0, y0, x1, y1), Pf = FACES[name]
        pts = [Pf(np.array(a), np.array(b)) for a, b in cu]
        uvs = [((x0 + a * (x1 - x0)) / A, (y0 + b * (y1 - y0)) / A) for a, b in cu]
        return [(np.array([pts[0], pts[i], pts[i + 1]]), np.array([uvs[0], uvs[i], uvs[i + 1]])) for i in range(1, len(pts) - 1)]
    TR = []
    ev = 1 - Hw / Hr    # where the eave sits down a gable region
    vy = lambda y: 1 - y / Hr; vw = lambda y: 1 - y / Hw
    for n in ('floor', 'roofL', 'roofR'):
        TR += poly(n, [(0, 0), (1, 0), (1, 1), (0, 1)])
    uL = [((a - ZB) / (ZV - ZB), (b - ZB) / (ZV - ZB)) for a, b in side_holes_z]
    TR += cells('sideL', uL, [(vw(side_y[1]), vw(side_y[0]))] * 2)
    TR += cells('sideR', [(1 - b, 1 - a) for a, b in uL], [(vw(side_y[1]), vw(side_y[0]))] * 2)
    # back gable: the rectangle below the eave, cut round the window, then the triangle up to the ridge
    TR += cells('back', [((back_x[0] + X) / (2 * X), (back_x[1] + X) / (2 * X))], [(vy(back_y[1]), vy(back_y[0]))], v_lo=ev)
    TR += poly('back', [(0, ev), (1, ev), (0.5, 0)])
    # front gable: only the two pieces either side of the curtain opening
    g = lambda x: Hr - (Hr - Hw) * abs(x) / X
    ud = (X - door_x) / (2 * X)
    TR += poly('front', [(0, 1), (ud, 1), (ud, vy(g(door_x))), (0, ev)])
    TR += poly('front', [(1 - ud, 1), (1, 1), (1, ev), (1 - ud, vy(g(door_x)))])
    # every triangle wound to face into the room
    mid = np.array([0.0, Hw / 2, (ZB + ZV) / 2])
    fixed = []
    for P, UV in TR:
        nn = np.cross(P[1] - P[0], P[2] - P[0])
        if nn @ (mid - P.mean(0)) < 0: P, UV = P[[0, 2, 1]], UV[[0, 2, 1]]
        fixed.append((P, UV))
    return fixed
if __name__ == '__main__':
    if '--box-only' not in sys.argv:
        for night in (False, True):
            Image.fromarray(bake(night)).save('/home/claude/room/room_%s.png' % ('night' if night else 'day'))
    TR = build_box()
    pickle.dump(dict(tris=TR, faces={k: v[0] for k, v in FACES.items()}, eye=E, ZV=ZV), open('/home/claude/room/room_box.pkl', 'wb'))
    print('box tris', len(TR))
