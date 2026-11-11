"""Export the three tents for the game: BMD models, DZB collision, packed into PsrTent.arc.
Usage: python3 -I export_tents.py OUT_DIR"""
import sys, os, math, pickle, numpy as np, cv2
sys.path.insert(0, '/home/claude/ptb/Passer_Travellers_Build/tools'); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bmdwrite import write_rigid_bmd
from rarcwrite import write_rarc
from rarc import rarc
from bmd import BMD
from dzb import write_dzb, read_dzb
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True)
HERE = os.path.dirname(os.path.abspath(__file__))
V = lambda x, y, z: np.array([x, y, z], float)

def oriented(a, b, c, outward):
    n = np.cross(b - a, c - a)
    return (a, b, c) if n @ outward >= 0 else (a, c, b)
def quad_tris(a, b, c, d, outward, both=False):
    out = [oriented(a, b, c, outward), oriented(a, c, d, outward)]
    if both: out += [oriented(a, b, c, -outward), oriented(a, c, d, -outward)]
    return out
def poly_tris(pts, outward, both=False):
    out = []
    for i in range(1, len(pts) - 1):
        out.append(oriented(pts[0], pts[i], pts[i + 1], outward))
        if both: out.append(oriented(pts[0], pts[i], pts[i + 1], -outward))
    return out

# ---------------- collision shapes ----------------
def a_tent_col():
    W, L, H = 236.0, 250.0, 196.0; zf, zb = L / 2, -L / 2
    t = []
    for s in (-1, 1):
        t += quad_tris(V(0, H, zb), V(0, H, zf), V(s * W / 2, 0, zf), V(s * W / 2, 0, zb), V(s * H, W / 2, 0))
    t += poly_tris([V(-W / 2, 0, zb), V(W / 2, 0, zb), V(0, H, zb)], V(0, 0, -1))
    t += poly_tris([V(-W / 2, 0, zf), V(W / 2, 0, zf), V(0, H, zf)], V(0, 0, 1))      # door kept shut for collision
    return t

def wall_tent_col(Wt, Lt, Hw, Hr, eave, ends, door, vest=None):
    zf, zb = Lt / 2, -Lt / 2; ey = Hw + 4
    t = []
    for s in (-1, 1):   # roof (both faces: a ceiling from inside)
        t += quad_tris(V(0, Hr, zb - ends), V(0, Hr, zf + ends), V(s * (Wt / 2 + eave), ey, zf + ends), V(s * (Wt / 2 + eave), ey, zb - ends),
                       V(s * (Hr - ey), Wt / 2 + eave, 0), both=True)
        t += quad_tris(V(s * Wt / 2, 0, zb), V(s * Wt / 2, 0, zf), V(s * Wt / 2, Hw, zf), V(s * Wt / 2, Hw, zb), V(s, 0, 0), both=True)
    t += poly_tris([V(-Wt / 2, 0, zb), V(Wt / 2, 0, zb), V(Wt / 2, Hw, zb), V(0, Hr, zb), V(-Wt / 2, Hw, zb)], V(0, 0, -1), both=True)
    t += poly_tris([V(-Wt / 2, 0, zf), V(-door, 0, zf), V(0, Hr, zf), V(-Wt / 2, Hw, zf)], V(0, 0, 1), both=True)
    t += poly_tris([V(door, 0, zf), V(Wt / 2, 0, zf), V(Wt / 2, Hw, zf), V(0, Hr, zf)], V(0, 0, 1), both=True)
    if vest:   # Beedle's entry: side walls and the curtain (the curtain is where the room load will trigger)
        vx, vd = vest; zv = zf - vd
        roof_y = lambda x: Hr - (Hr - ey) * abs(x) / (Wt / 2 + eave) - 4
        for s in (-1, 1):
            t += quad_tris(V(s * vx, 0, zf), V(s * vx, 0, zv), V(s * vx, roof_y(vx), zv), V(s * vx, roof_y(vx), zf), V(-s, 0, 0), both=True)
        t += poly_tris([V(-vx, 0, zv), V(vx, 0, zv), V(vx, roof_y(vx), zv), V(0, roof_y(0), zv), V(-vx, roof_y(vx), zv)], V(0, 0, 1), both=True)
    return t

TENTS = {
    'a_tent': dict(tris='cream/a_tent_tris.pkl', tex='cream/tent_canvas_HD_1024.png', size=512, col=a_tent_col()),
    'wall_tent': dict(tris='wall/wall_tent_tris.pkl', tex='wall/wall_tent_HD_2048.png', size=512,
                      col=wall_tent_col(380, 480, 125, 300, 16, 12, 72)),
    'beedle_tent': dict(tris='beedle/beedle_tent_tris.pkl', tex='beedle/beedle_tent_HD_2048.png', size=512,
                        col=wall_tent_col(440, 560, 145, 345, 18, 14, 82, vest=(96, 70))),
    'beedle_glow': dict(tris='beedle/beedle_tent_glow_tris.pkl', tex='beedle/beedle_tent_glow_512.png', size=256, col=None),
}
files = {}
for name, t in TENTS.items():
    tris = pickle.load(open(os.path.join(HERE, t['tris']), 'rb'))
    tex = cv2.cvtColor(cv2.imread(os.path.join(HERE, t['tex'])), cv2.COLOR_BGR2RGB)
    tex = cv2.resize(tex, (t['size'], t['size']), interpolation=cv2.INTER_AREA)
    path = os.path.join(OUT, f'{name}.bmd')
    write_rigid_bmd(path, [(np.asarray(p, float), np.asarray(uv, float)) for p, uv in tris], tex, texname=name, jointname=name, matname=f'{name}_m')
    files[f'bmdr/{name}.bmd'] = open(path, 'rb').read()
    # read it back to make sure it parses and matches
    b = BMD(files[f'bmdr/{name}.bmd'])
    print(f'{name}: {len(tris)} tris in, {len(b.tris)} read back, bmd {len(files[f"bmdr/{name}.bmd"]) // 1024} KB')
    if t['col']:
        dz = write_dzb([tuple(map(tuple, tr)) for tr in t['col']], name=name)
        files[f'dzb/{name}.dzb'] = dz
        Vv, Tt = read_dzb(dz)
        print(f'   collision: {len(Tt)} tris, {len(Vv)} verts, dzb {len(dz)} bytes')
write_rarc(os.path.join(OUT, 'PsrTent.arc'), 'PsrTent', files)
r = rarc(open(os.path.join(OUT, 'PsrTent.arc'), 'rb').read())
print('PsrTent.arc:', sorted(r), os.path.getsize(os.path.join(OUT, 'PsrTent.arc')) // 1024, 'KB')
pickle.dump({k: v['col'] for k, v in TENTS.items() if v['col']}, open(os.path.join(OUT, 'collision.pkl'), 'wb'))
