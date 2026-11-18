"""Beedle's shop model (R_PSR00 model5.bmd), cleaned after build_shop_kak.py: the three lanterns that
were baked in come out (shop_lamps.cpp draws the game's own lantern there, so its glass shows the
flame), and so do the herb bunches, the string of peppers and the rope coil hanging from the ridge.
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
print('dropped', n, 'triangles;', len(groups), 'materials')
print(write_multi(dst, groups, jointname='shop', cull=0))
