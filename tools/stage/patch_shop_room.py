"""Beedle's shop room (R_PSR00 R00_00.arc), patched in place of a full rebuild:
  - four display tables, not six: the middle pair (x +-130, z 60 in the shop's frame) comes out of the
    shop model (model5.bmd: the table and its cushion, 192 triangles each) and out of the collision.
    The four left are the corners of the aisle, which the game's four-ware shop (dShopSystem_c::
    chooseItem4) walks as one row: near left, far left, far right, near right (tent_beedle.cpp kSlot).
  - the doorway: the field outside is eased down to meet the floor (door_ramp.py), and the collision's
    floor just outside the door follows it.
Everything else in the archive is kept byte for byte.
Usage: python3 -I patch_shop_room.py IN_R00_00.arc OUT_R00_00.arc"""
import sys, os, math, subprocess, tempfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = ['/home/claude/w/anim', HERE]
from rarc import rarc
from rarcwrite import write_rarc
from bmd import BMD
from drop_tris import drop_tris
from kcl import write_kcl, ground_at
from kcl_read import read_kcl

src, dst = sys.argv[1:3]
TENT = np.array([-72921.4, -1551.7, 5115.4]); YAW = 9935
a = YAW / 65536 * 2 * math.pi; c, s = math.cos(a), math.sin(a)
def local(P):
    d = np.asarray(P, float) - TENT
    return np.stack([d[..., 0] * c - d[..., 2] * s, d[..., 1], d[..., 0] * s + d[..., 2] * c], -1)
def place(x, y, z): return TENT + np.array([x * c + z * s, y, -x * s + z * c])
GONE = [(-130.0, 60.0), (130.0, 60.0)]   # the middle tables
def at_gone_table(L, half=48.0, top=90.0):
    m = L.mean(0)
    return any(abs(m[0] - x) < half and abs(m[2] - z) < half for x, z in GONE) and 1.5 < m[1] < top

arc = rarc(open(src, 'rb').read())
files = dict(arc)
tmp = tempfile.mkdtemp()

# ---- the doorway: the field eased down to the floor ----
open(f'{tmp}/model.bmd', 'wb').write(files['bmdr/model.bmd'])
subprocess.run([sys.executable, '-I', os.path.join(HERE, 'door_ramp.py'), f'{tmp}/model.bmd', f'{tmp}/model_ramp.bmd',
                *map(str, TENT), str(YAW), '398', '498'], check=True)
files['bmdr/model.bmd'] = open(f'{tmp}/model_ramp.bmd', 'rb').read()
field = BMD(files['bmdr/model.bmd']); field.jworld = [field.world(i) for i in range(len(field.jloc))]; field.read_shp()
FL = local(np.array([t[1] for t in field.tris])); FL = FL[(FL.mean(1)[:, 2] > 400) & (FL.mean(1)[:, 2] < 900)]
def field_y(x, z):   # the field's ground (shop frame) under (x, z)
    for A, B, C in FL:
        v0, v1, v2 = C[[0, 2]] - A[[0, 2]], B[[0, 2]] - A[[0, 2]], np.array([x, z]) - A[[0, 2]]
        d = v0[0] * v1[1] - v1[0] * v0[1]
        if abs(d) < 1e-9: continue
        u = (v2[0] * v1[1] - v1[0] * v2[1]) / d; v = (v0[0] * v2[1] - v2[0] * v0[1]) / d
        if u >= -1e-6 and v >= -1e-6 and u + v <= 1 + 1e-6: return A[1] + u * (C[1] - A[1]) + v * (B[1] - A[1])
    raise SystemExit(f'no field ground at {x}, {z}')

# ---- the shop model: the middle tables out (only its display lists change: no texture is re-encoded) ----
b = BMD(files['bmdr/model5.bmd']); assert len(b.jloc) == 1 and np.allclose(b.world(0), np.eye(4)), 'one joint, at the origin'
files['bmdr/model5.bmd'], dropped = drop_tris(files['bmdr/model5.bmd'], lambda si, P: at_gone_table(local(P)))
assert dropped == 2 * 192, dropped
print('shop model: dropped', dropped, 'triangles (two tables and their cushions)')

# ---- the collision: the middle tables' boxes out, the floor outside the door on the field ----
T = read_kcl(files['kcl/room.kcl'])
out = []; gone = 0; lifted = 0
for t in T:
    L = local(np.array(t))
    if at_gone_table(L, half=42.0, top=80.0):   # (a box: top at 72, four sides from the floor up)
        gone += 1; continue
    if np.all(np.abs(L[:, 1]) < 0.01) and L[:, 2].min() >= 499.9:   # the floor just outside the doorway
        L = L.copy()
        for k in range(3): L[k, 1] = max(0.0, field_y(L[k, 0], L[k, 2]) + 0.5) if L[k, 2] > 500.1 else 0.0
        t = tuple(place(*p) for p in L); lifted += 1
    out.append(t)
print('collision: dropped', gone, 'table faces; floor outside the door:', lifted, 'faces on the field')
assert gone == 2 * 10, gone
data, n = write_kcl(out)
files['kcl/room.kcl'] = data
for nm, (x, z) in {'aisle': (0, 0), 'where a table was': (130, 60), 'a table': (130, -120),
                   'doorway': (0, 470), 'outside the door': (0, 560)}.items():
    g = ground_at(data, *place(x, 300, z)); print(f'  ground {nm:18s} {None if g is None else round(g - TENT[1], 1)}')

write_rarc(dst, 'R00_00', files)
back = rarc(open(dst, 'rb').read())
assert list(back.keys()) == list(arc.keys()), (list(back.keys()), list(arc.keys()))
for k in ('dzr/room.dzr', 'plc/room.plc'): assert back[k] == arc[k]
print(dst, os.path.getsize(src), '->', os.path.getsize(dst), 'bytes')
