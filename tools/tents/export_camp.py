"""Swap the new small camp tent and its door flap into PsrTent.arc.
Usage: python3 -I export_camp.py IN_PsrTent.arc CAMP_DIR OUT_DIR"""
import sys, os, pickle, numpy as np
from PIL import Image
sys.path[:0] = ['/home/claude/anim']
from bmdwrite import write_rigid_bmd
from rarcwrite import write_rarc
from rarc import rarc
from bmd import BMD
SRC, CAMP, OUT = sys.argv[1:4]; os.makedirs(OUT, exist_ok=True)
old = rarc(open(SRC, 'rb').read())
files = {k.split('/', 1)[1] if k.count('/') > 1 else k: v for k, v in old.items()}
atlas = np.array(Image.open(os.path.join(CAMP, 'camp_tent_atlas.png')).convert('RGB'))
T = pickle.load(open(os.path.join(CAMP, 'camp_tent_tris.pkl'), 'rb'))
F = pickle.load(open(os.path.join(CAMP, 'camp_flap_tris.pkl'), 'rb'))
import normals as _nrm
# Normals, set out explicitly: the canvas is two layers (its outside and, just behind it, its inside),
# which a guess from the shape alone gets backwards. Outside faces point away from the middle of the
# tent, inside faces toward it, the groundsheet up; poles, ropes and stakes are smoothed round their
# own middle as before. The flaps point out of the doorway (the tent actor turns them as they swing).
A_ = 512.0
def region_of(uv):
    u = float(np.mean(uv[:, 0])) * A_; v = float(np.mean(uv[:, 1])) * A_
    if u < 256 or u >= 480: return 'cloth'
    if 352 <= u < 384 and v >= 128: return 'cloth'      # the stripe binding
    return 'rod'
def tent_normals(tris, cols):
    out = [None] * len(tris)
    rods = [i for i, (P, UV) in enumerate(tris) if region_of(np.asarray(UV)) == 'rod']
    if rods:
        sm = _nrm.smooth(np.array([np.asarray(tris[i][0], float) for i in rods]), crease=75.0)
        for k, i in enumerate(rods): out[i] = np.asarray(sm[k], float)
    for i, (P, UV) in enumerate(tris):
        if out[i] is not None: continue
        P = np.asarray(P, float); c = P.mean(0)
        n = np.cross(P[1] - P[0], P[2] - P[0]); n /= max(np.linalg.norm(n), 1e-9)
        shade = float(np.asarray(cols[i])[0, 0]) if cols is not None else 1.0
        if np.ptp(P[:, 1]) < 0.01 and c[1] < 2.0:                 # the groundsheet
            n = np.array([0.0, 1.0, 0.0])
        else:
            mid = np.array([0.0, 60.0, np.clip(c[2], -105.0, 105.0)])
            away = (c - mid) @ n > 0
            inside = shade < 0.5
            if away == inside: n = -n
        out[i] = np.array([n, n, n])
    return out
def put(name, tris, **kw):
    p = os.path.join(OUT, name + '.bmd')
    write_rigid_bmd(p, [(np.asarray(a, float), np.asarray(b, float)) for a, b in tris], atlas, texname=name, jointname=name, matname=name + '_m', **kw)
    files['bmdr/%s.bmd' % name] = open(p, 'rb').read()
    print(name, len(tris), 'tris,', len(BMD(files['bmdr/%s.bmd' % name]).tris), 'read back,', len(files['bmdr/%s.bmd' % name]) // 1024, 'KB')
NT = tent_normals(T['tris'], T['cols'])
NF = [np.array([[0.0, 0.0, 1.0]] * 3) for _ in F['tris']]
put('a_tent', T['tris'], colors=[np.asarray(c, float) for c in T['cols']], normals=NT)
put('camp_flap', F['tris'], normals=NF)
# lit from inside (drawn unlit at night): the cloth glows lamp-warm, thicker at the border; the wood and
# rope outside it stay dark against it
day = atlas
lit = day.astype(np.float32)
def regmul(x0, y0, x1, y1, k):
    lit[y0:y1, x0:x1] = np.clip(lit[y0:y1, x0:x1] * np.array(k, np.float32), 0, 255)
regmul(0, 0, 256, 512, [1.45, 1.05, 0.58])      # the weave
regmul(480, 0, 512, 512, [0.95, 0.62, 0.32])     # the border (denser cloth)
regmul(256, 0, 352, 512, [0.22, 0.18, 0.15])     # poles, rope
regmul(352, 0, 480, 128, [0.22, 0.18, 0.15])     # stakes
atlas = lit.astype(np.uint8)
Image.fromarray(atlas).save(os.path.join(OUT, 'camp_tent_lit.png'))
cols = []
for c in T['cols']:
    k = float(np.asarray(c)[0, 0])
    cols.append(np.ones((3, 4)) * (0.55 if k < 0.35 else 1.0))   # inside: the groundsheet a bit darker
put('a_tent_n', T['tris'], colors=cols, normals=NT)
put('camp_flap_n', F['tris'], normals=NF)
atlas = day
# collision: the door is open now (they crawl in)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dzb import write_dzb
V = lambda x, y, z: np.array([x, y, z], float)
def tri_o(a, b, c, out):
    n = np.cross(b - a, c - a); return (a, b, c) if n @ out >= 0 else (a, c, b)
W, L, H, D, DH = 236.0, 250.0, 196.0, 58.0, 150.0
zf, zb = L / 2, -L / 2
col = []
for sgn in (-1, 1):
    o = V(sgn * H, W / 2, 0)
    col += [tri_o(V(0, H, zb), V(0, H, zf), V(sgn * W / 2, 0, zf), o), tri_o(V(0, H, zb), V(sgn * W / 2, 0, zf), V(sgn * W / 2, 0, zb), o)]
    # front, beside the doorway
    col += [tri_o(V(sgn * W / 2, 0, zf), V(sgn * D, 0, zf), V(0, DH, zf), V(0, 0, 1)), tri_o(V(sgn * W / 2, 0, zf), V(0, DH, zf), V(0, H, zf), V(0, 0, 1))]
col += [tri_o(V(-W / 2, 0, zb), V(W / 2, 0, zb), V(0, H, zb), V(0, 0, -1))]
files['dzb/a_tent.dzb'] = write_dzb([tuple(map(tuple, t)) for t in col], name='a_tent')
print('collision', len(col), 'tris (door open)')
write_rarc(os.path.join(OUT, 'PsrTent.arc'), 'PsrTent', files)
print(sorted(rarc(open(os.path.join(OUT, 'PsrTent.arc'), 'rb').read())), os.path.getsize(os.path.join(OUT, 'PsrTent.arc')) // 1024, 'KB')
