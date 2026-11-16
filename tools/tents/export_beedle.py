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
# the room box, game units -> tent units, pulled in 4% so it sits inside the canvas
B = pickle.load(open(os.path.join(ROOM, 'room_box.pkl'), 'rb'))
zc = (-500.0 + B['ZV']) / 2
def fit(P):
    P = np.asarray(P, float) / 1.6
    return np.c_[P[:, 0] * 0.96, P[:, 1] * 0.96, zc / 1.6 + (P[:, 2] - zc / 1.6) * 0.96]
box, nrm = [], []
for P, UV in B['tris']:
    P = fit(P); n = np.cross(P[1] - P[0], P[2] - P[0]); n /= np.linalg.norm(n)   # faces into the room
    box.append((P[[0, 2, 1]], np.asarray(UV, float)[[0, 2, 1]]))                   # the game's front faces wind the other way
    nrm.append(np.array([n, n, n]))
for nm, img in (('beedle_room', 'room_day.png'), ('beedle_room_n', 'room_night.png')):
    put(nm, box, rgb(os.path.join(ROOM, img), 1024), normals=nrm, cull=2)
col = ET.wall_tent_col(500, 625, 145, 345, 18, 14, 94, vest=(108, 70), shoulder=(86, 190))
files['dzb/beedle_tent.dzb'] = write_dzb([tuple(map(tuple, t)) for t in col], name='beedle_tent')
print('collision', len(col), 'tris')
write_rarc(os.path.join(OUT, 'PsrTent.arc'), 'PsrTent', files)
print('PsrTent.arc', sorted(rarc(open(os.path.join(OUT, 'PsrTent.arc'), 'rb').read())), os.path.getsize(os.path.join(OUT, 'PsrTent.arc')) // 1024, 'KB')
