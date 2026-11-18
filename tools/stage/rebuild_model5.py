"""Beedle's shop model (R_PSR00 model5.bmd), cleaned after build_shop_kak.py: the three lanterns that
were baked in come out (shop_lamps.cpp draws the game's own lantern there, so its glass shows the
flame), and so do the herb bunches, the string of peppers and the rope coil hanging from the ridge.
Also adds Beedle's mat on the counter (where he sits) and an earth apron round the floor's edge (the field slopes below the floor on one side).
Rebuild the shop model (R_PSR00 model5.bmd) from itself, leaving out chosen triangles.
Usage: python3 -I rebuild_model5.py IN.bmd OUT.bmd [drop]"""
import sys, math, numpy as np
sys.path[:0] = ['/home/claude/w/anim']
from bmd import BMD
from multibmd import write_multi
src, dst = sys.argv[1:3]; drop = len(sys.argv) > 3
TENT = np.array([-72921.4, -1551.7, 5115.4]); a = 9935 / 65536 * 2 * math.pi; c, s = math.cos(a), math.sin(a)
def local(P):   # inverse of build_shop_kak.place
    d = P - TENT; return np.stack([d[:, 0] * c - d[:, 2] * s, d[:, 1], d[:, 0] * s + d[:, 2] * c], 1)
b = BMD(open(src, 'rb').read()); b.jworld = [b.world(i) for i in range(len(b.jloc))]; b.read_shp()
def dropped(mi, L):
    if not drop: return False
    if mi == 12: return True                                  # the three baked lanterns: drawn by the mod now
    x, y, z = L.mean(0)
    if y < 400 or mi not in (0, 2): return False
    if abs(x + 40) < 6 and abs(z - 150) < 8: return True      # the string of peppers
    if 28 < x < 52 and (abs(z + 150) < 8 or abs(z - 420) < 8): return True   # the two bunches of herbs
    if abs(x) < 12 and 520 < y < 541 and 450 < z < 470 and mi == 2: return True   # the rope coil on the ridge
    return False
groups = []; n = 0
for mi in range(len(b.mats)):
    tris = [(np.asarray(p, float), np.asarray(uv, float)) for m, p, uv in b.tris if m == mi]
    keep = [(p, uv) for p, uv in tris if not dropped(mi, local(p))]
    n += len(tris) - len(keep)
    tex = b.tex[b.mats[mi][0]]
    if not keep: print('material', mi, 'gone'); continue
    groups.append((np.asarray(tex, np.uint8), bool(tex[..., 3].min() < 128), keep, getattr(tex, 'wrap', (1, 1))))
# The apron: the field round the tent slopes away on one side, lower than the shop's floor, and
# left a see-through strip under the walls' bottom edge and the doorway's threshold. A strip of the
# floor's own earth hangs from the floor's edge (y 1 down to -120) all the way round, just outside it.
if drop:
    def place(x, y, z):   # shop frame -> world (build_shop_kak.place)
        return TENT + np.array([x * c + z * s, y, -x * s + z * c])
    FLOOR = 17
    tex = b.tex[b.mats[FLOOR][0]]
    W2, L2, OUT, TOP, BOT = 400.0, 500.0, 3.0, 1.0, -120.0
    edges = [((-W2 - OUT, -L2 - OUT), (W2 + OUT, -L2 - OUT)), ((W2 + OUT, -L2 - OUT), (W2 + OUT, L2 + OUT)),
             ((W2 + OUT, L2 + OUT), (-W2 - OUT, L2 + OUT)), ((-W2 - OUT, L2 + OUT), (-W2 - OUT, -L2 - OUT))]
    apron = []
    for (x0, z0), (x1, z1) in edges:
        ln = math.hypot(x1 - x0, z1 - z0)
        for k in range(int(ln // 100)):
            t0, t1 = k * 100.0 / ln, min(1.0, (k + 1) * 100.0 / ln)
            ax, az = x0 + (x1 - x0) * t0, z0 + (z1 - z0) * t0
            bx, bz = x0 + (x1 - x0) * t1, z0 + (z1 - z0) * t1
            P = [place(ax, TOP, az), place(bx, TOP, bz), place(bx, BOT, bz), place(ax, BOT, az)]
            U = [(0, 0), (1, 0), (1, 1.2), (0, 1.2)]
            apron.append((np.array([P[0], P[1], P[2]]), np.array([U[0], U[1], U[2]])))
            apron.append((np.array([P[0], P[2], P[3]]), np.array([U[0], U[2], U[3]])))
    groups.append((np.asarray(tex, np.uint8), False, apron, (1, 1)))
    print('apron', len(apron), 'triangles')
    # Beedle's mat on the counter, where he sits (tent_beedle.cpp kSeat: x 55, z -370): the cloth from
    # his old counter (beedle_shop.png's cloth panel) laid on the top (98 up) and hanging over the front.
    from PIL import Image
    sheet = np.asarray(Image.open('/home/claude/beedle/shop/beedle_shop.png').convert('RGBA'))
    cloth = np.ascontiguousarray(np.asarray(Image.fromarray(sheet[512:750, 0:256]).resize((256, 256))))   # (its last rows are wood)
    X0, X1, ZB_, ZF_, TOPY, HANG = -5.0, 115.0, -374.0, -300.0, 99.5, 58.0
    mat = []
    def quad(P, U):
        P = [place(*p) for p in P]
        mat.append((np.array([P[0], P[1], P[2]]), np.array([U[0], U[1], U[2]])))
        mat.append((np.array([P[0], P[2], P[3]]), np.array([U[0], U[2], U[3]])))
    vfold = 0.62   # the top takes this much of the panel, the front drop the rest
    for k in range(4):   # in strips (shading follows the vertices)
        a, b2 = k / 4, (k + 1) / 4
        xa, xb = X0 + (X1 - X0) * a, X0 + (X1 - X0) * b2
        quad([(xa, TOPY, ZB_), (xb, TOPY, ZB_), (xb, TOPY, ZF_), (xa, TOPY, ZF_)],
             [(a, 0.0), (b2, 0.0), (b2, vfold), (a, vfold)])
        quad([(xa, TOPY, ZF_), (xb, TOPY, ZF_), (xb, TOPY - 6.0, ZF_ + 2.5), (xa, TOPY - 6.0, ZF_ + 2.5)],
             [(a, vfold), (b2, vfold), (b2, vfold + 0.05), (a, vfold + 0.05)])
        quad([(xa, TOPY - 6.0, ZF_ + 2.5), (xb, TOPY - 6.0, ZF_ + 2.5), (xb, HANG, ZF_ + 3.5), (xa, HANG, ZF_ + 3.5)],
             [(a, vfold + 0.05), (b2, vfold + 0.05), (b2, 1.0), (a, 1.0)])
    groups.append((cloth, bool(cloth[..., 3].min() < 128), mat, (1, 1)))
    print('mat', len(mat), 'triangles')
print('dropped', n, 'triangles;', len(groups), 'materials')
print(write_multi(dst, groups, jointname='shop', cull=0))
