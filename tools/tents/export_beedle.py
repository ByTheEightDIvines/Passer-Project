"""Rebuild Beedle's tent inside PsrTent.arc: the wider tent, its collision, and the baked room
(beedle_room.bmd by day, beedle_room_n.bmd by night). The other tents are carried over unchanged.
Usage: python3 -I export_beedle.py SHIPPED_PsrTent.arc ROOM_DIR OUT_DIR"""
import sys, os, pickle, numpy as np, cv2
sys.path[:0] = ['/home/claude/anim', os.path.dirname(os.path.abspath(__file__))]
from bmdwrite import write_rigid_bmd
from rarcwrite import write_rarc
from rarc import rarc
from bmd import BMD
from dzb import write_dzb
_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'export_tents.py')).read()
_ns = {'np': np, 'math': __import__('math')}; exec(_src[_src.index('V = lambda'):_src.index('TENTS = {')], _ns)
class ET: wall_tent_col = staticmethod(_ns['wall_tent_col'])
SRC, ROOM, OUT = sys.argv[1:4]; os.makedirs(OUT, exist_ok=True)
HERE = os.path.dirname(os.path.abspath(__file__))
old = rarc(open(SRC, 'rb').read())
files = {k.split('/', 1)[1] if k.count('/') > 1 else k: v for k, v in old.items()}
files = {k: v for k, v in files.items() if 'beedle' not in k}
print('carried over:', sorted(files))
def put(name, tris, tex, **kw):
    p = os.path.join(OUT, name + '.bmd')
    write_rigid_bmd(p, tris, tex, texname=name, jointname=name, matname=name + '_m', **kw)
    files['bmdr/%s.bmd' % name] = open(p, 'rb').read()
    print(name, len(tris), 'tris,', len(BMD(files['bmdr/%s.bmd' % name]).tris), 'read back,', len(files['bmdr/%s.bmd' % name]) // 1024, 'KB')
rgb = lambda p, n: cv2.resize(cv2.cvtColor(cv2.imread(p), cv2.COLOR_BGR2RGB), (n, n), interpolation=cv2.INTER_AREA)
T = pickle.load(open(os.path.join(HERE, 'beedle/beedle_tent_tris.pkl'), 'rb'))
put('beedle_tent', [(np.asarray(p, float), np.asarray(uv, float)) for p, uv in T], rgb(os.path.join(HERE, 'beedle/beedle_tent_HD_2048.png'), 512))
# the room seen from outside (through the doorway and the windows): the shop's own model, the same one
# you walk around in, so it looks right from any angle. Its canvas walls and roof are left out (the tent's
# own are there) and so is everything in front of the curtain line; game units -> tent units, pulled in
# 4% so it sits inside the canvas, and lifted a little so the field's grass never pokes through the rugs.
sys.path[:0] = ['/home/claude/room']
import scene, hashlib
from multibmd import write_multi
Lw, Lhw, Lhr, LZB, LZF = scene.LAY['W'], scene.LAY['Hw'], scene.LAY['Hr'], scene.LAY['ZB'], scene.LAY['ZF']
ZVg = 500.0 - 70.0 * 1.6                      # the curtain line, in shop units
zc = (-500.0 + ZVg) / 2
LIFT = 5.0                                    # tent units
def shell(P):
    c = P.mean(0)
    if abs(abs(c[0]) - Lw / 2) < 6 and np.ptp(P[:, 0]) < 3: return True
    if (abs(c[2] - LZB) < 6 or abs(c[2] - LZF) < 6) and np.ptp(P[:, 2]) < 3: return True
    roofy = Lhr - (Lhr - Lhw) * abs(c[0]) / (Lw / 2)
    return c[1] > Lhw - 5 and abs(c[1] - roofy) < 8
def fit(P):
    P = np.asarray(P, float) / 1.6
    return np.c_[P[:, 0] * 0.96, P[:, 1] * 0.96 + LIFT, zc / 1.6 + (P[:, 2] - zc / 1.6) * 0.96]
for nm, night in (('beedle_room', False), ('beedle_room_n', True)):
    S, TX = scene.build(night, clear_floor=False)
    groups = {}; order = []
    for ti, P, UV in S:
        P = np.asarray(P, float)
        if shell(P) or P[:, 2].mean() > ZVg - 4: continue
        tex = (np.clip(TX[ti], 0, 1) * 255).astype(np.uint8)
        if tex.shape[2] == 3: tex = np.dstack([tex, np.full(tex.shape[:2], 255, np.uint8)])
        k = hashlib.md5(tex.tobytes()).hexdigest()
        if k not in groups: groups[k] = (tex, bool(tex[..., 3].min() < 128), []); order.append(k)
        Q = fit(P)
        if (Q[:, 1] > 345.0 - 200.0 * np.abs(Q[:, 0]) / 250.0 - 3.0).any(): continue   # under the tent's own roof only
        groups[k][2].append((Q, np.asarray(UV, float)))
    G = [groups[k] for k in order]
    path = os.path.join(OUT, nm + '.bmd')
    write_multi(path, G, jointname=nm, cull=0)
    files['bmdr/%s.bmd' % nm] = open(path, 'rb').read()
    print(nm, sum(len(g[2]) for g in G), 'tris', len(G), 'materials', len(files['bmdr/%s.bmd' % nm]) // 1024, 'KB')
col = ET.wall_tent_col(500, 625, 145, 345, 18, 14, 94, vest=(108, 70), shoulder=(86, 190))
files['dzb/beedle_tent.dzb'] = write_dzb([tuple(map(tuple, t)) for t in col], name='beedle_tent')
print('collision', len(col), 'tris')
write_rarc(os.path.join(OUT, 'PsrTent.arc'), 'PsrTent', files)
print('PsrTent.arc', sorted(rarc(open(os.path.join(OUT, 'PsrTent.arc'), 'rb').read())), os.path.getsize(os.path.join(OUT, 'PsrTent.arc')) // 1024, 'KB')
