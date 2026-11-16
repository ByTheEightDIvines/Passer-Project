"""Collision for Beedle's shop room (game units): floor, canvas walls and roof, the doorway open, and
boxes for the counter, shelves, tables, pillar and the props standing on the floor."""
import sys, os, pickle, math, numpy as np
sys.path[:0] = ['/home/claude/anim', os.path.dirname(os.path.abspath(__file__))]
from kcl import write_kcl, ground_at
from rarc import rarc
from bmd import BMD
W, L, Hw, Hr = 800.0, 1000.0, 230.0, 540.0
ZF, ZB = L / 2, -L / 2
DOOR_W, DOOR_H = 170.0, 260.0
CZ = ZB + 165
V = lambda x, y, z: np.array([x, y, z], float)
T = []
FACING = [V(0, 150, 0)]      # faces are turned to face this point (the room's middle; boxes: away from their centre)
AWAY = [False]
def orient(a, b, c):
    n = np.cross(b - a, c - a); m = (a + b + c) / 3
    toward = (FACING[0] - m) @ n > 0
    return (a, b, c) if toward != AWAY[0] else (a, c, b)
def quad(a, b, c, d):
    T.append(orient(a, b, c)); T.append(orient(a, c, d))
def box(c, size, yaw=0.0, top=True):
    hx, hy, hz = np.array(size) / 2; cy_, sy_ = math.cos(yaw), math.sin(yaw)
    R = np.array([[cy_, 0, sy_], [0, 1, 0], [-sy_, 0, cy_]])
    P = lambda x, y, z: np.asarray(c, float) + R @ np.array([x, y, z])
    FACING[0] = np.asarray(c, float); AWAY[0] = True
    if top: quad(P(-hx, hy, hz), P(hx, hy, hz), P(hx, hy, -hz), P(-hx, hy, -hz))      # up
    quad(P(-hx, -hy, hz), P(hx, -hy, hz), P(hx, hy, hz), P(-hx, hy, hz))              # +z
    quad(P(hx, -hy, -hz), P(-hx, -hy, -hz), P(-hx, hy, -hz), P(hx, hy, -hz))          # -z
    quad(P(-hx, -hy, -hz), P(-hx, -hy, hz), P(-hx, hy, hz), P(-hx, hy, -hz))          # -x
    quad(P(hx, -hy, hz), P(hx, -hy, -hz), P(hx, hy, -hz), P(hx, hy, hz))              # +x
# floor (up)
quad(V(-W / 2, 0, ZF), V(W / 2, 0, ZF), V(W / 2, 0, ZB), V(-W / 2, 0, ZB))
# side walls, facing in
quad(V(-W / 2, 0, ZB), V(-W / 2, 0, ZF), V(-W / 2, Hw, ZF), V(-W / 2, Hw, ZB))
quad(V(W / 2, 0, ZF), V(W / 2, 0, ZB), V(W / 2, Hw, ZB), V(W / 2, Hw, ZF))
# roof slopes, facing in (down)
quad(V(-W / 2, Hw, ZF), V(0, Hr, ZF), V(0, Hr, ZB), V(-W / 2, Hw, ZB))
quad(V(W / 2, Hw, ZB), V(0, Hr, ZB), V(0, Hr, ZF), V(W / 2, Hw, ZF))
# back gable, facing in (+z)
T.append(orient(V(-W / 2, 0, ZB), V(W / 2, 0, ZB), V(W / 2, Hw, ZB))); T.append(orient(V(-W / 2, 0, ZB), V(W / 2, Hw, ZB), V(-W / 2, Hw, ZB)))
T.append(orient(V(-W / 2, Hw, ZB), V(W / 2, Hw, ZB), V(0, Hr, ZB)))
# front gable around the doorway, facing in (-z)
d = DOOR_W / 2
for s in (-1, 1):
    a, b, c, e = V(s * W / 2, 0, ZF), V(s * d, 0, ZF), V(s * d, DOOR_H, ZF), V(s * W / 2, Hw, ZF)
    pts = [a, b, c, e] if s > 0 else [b, a, e, c]
    T.append(orient(pts[0], pts[2], pts[1])); T.append(orient(pts[0], pts[3], pts[2]))
T.append(orient(V(-W / 2, Hw, ZF), V(W / 2, Hw, ZF), V(0, Hr, ZF)))
T.append(orient(V(-d, DOOR_H, ZF), V(-W / 2, Hw, ZF), V(d, DOOR_H, ZF)))
T.append(orient(V(d, DOOR_H, ZF), V(-W / 2, Hw, ZF), V(W / 2, Hw, ZF)))
FACING[0] = V(0, 100, ZF + 45)
# outside the doorway: a stop a little way out (the mod takes Link back to the field before it)
quad(V(-d - 10, 0, ZF + 90), V(d + 10, 0, ZF + 90), V(d + 10, DOOR_H, ZF + 90), V(-d - 10, DOOR_H, ZF + 90))
quad(V(-d - 10, 0, ZF + 90), V(-d - 10, 0, ZF), V(d + 10, 0, ZF), V(d + 10, 0, ZF + 90))   # its floor (up)
quad(V(-d, 0, ZF), V(-d, 0, ZF + 90), V(-d, DOOR_H, ZF + 90), V(-d, DOOR_H, ZF))           # sides
quad(V(d, 0, ZF + 90), V(d, 0, ZF), V(d, DOOR_H, ZF), V(d, DOOR_H, ZF + 90))
SHELL_END = len(T)
# the counter (Beedle sits on it), the centre pillar, the shelf units, the tables
box(V(0, 49, CZ - 2), (404, 98, 70))
box(V(0, Hr / 2, ZB + 55), (18, Hr, 18), top=False)
for s in (-1, 1):
    for cz in (-170.0, 110.0):
        box(V(s * (W / 2 - 6) - s * 25, 132, cz), (50, 264, 230))
for x in (-130.0, 130.0):
    for z in (-120.0, 60.0, 240.0):
        box(V(x, 36, z), (78, 72, 75))
# props standing on the floor (barrels, crates, pots): their footprint from the game's own models
_, PROPS, _ = pickle.load(open('/home/claude/room/interior2.pkl', 'rb'))
arcs = {}
for arc, model, pos, yaw, sc in PROPS:
    if arc in ('HTable', 'Obj_kntr', 'HShokudai', 'PPolamp') or pos[1] > 20: continue
    if arc not in arcs: arcs[arc] = rarc(open(f'/mnt/user-data/uploads/Object/{arc}.arc', 'rb').read())
    b = BMD(arcs[arc][[k for k in arcs[arc] if k.endswith('/' + model)][0]]); b.jworld = [b.world(i) for i in range(len(b.jloc))]; b.read_shp()
    Pm = np.concatenate([np.asarray(t[1], float) for t in b.tris])
    if sc < 0: sc = -sc / (Pm[:, 1].max() - Pm[:, 1].min())
    mn, mx = Pm.min(0) * sc, Pm.max(0) * sc
    box(np.asarray(pos, float) + V((mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2) * np.array([1, 1, 1]),
        (mx - mn) * 0.9, math.radians(yaw))
data, n = write_kcl(T)
out = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/shopstage'
open(f'{out}/room.kcl', 'wb').write(data)
print('collision triangles', n, 'kcl', len(data) // 1024, 'KB')
# checks: standing spots
for nm, (x, y, z) in {'doorway': (0, 300, ZF - 60), 'aisle': (0, 300, 0), 'on the counter': (0, 300, CZ),
                      'on a table': (130, 300, 60), 'beside a table': (40, 300, 60), 'on a shelf unit': (370, 400, 110)}.items():
    print(f'  ground {nm:16s} {ground_at(data, x, y, z)}')
