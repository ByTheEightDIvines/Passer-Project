"""Assemble the interior preview: room + Beedle's pack (from his model) + Wind Waker curios and the
spoils bag (from itemmdl.arc), then render perspective views by day and night."""
import sys, os, math, pickle, numpy as np, xml.etree.ElementTree as ET
from PIL import Image
sys.path.insert(0, '/home/claude/ptb/Passer_Travellers_Build/tools'); sys.path.insert(0, '/home/claude/work/tent')
from rarc import rarc
from bmd import BMD
from persp import render
D = sys.argv[1]; views = sys.argv[2].split(',') if len(sys.argv) > 2 else ['door', 'counter', 'shelf', 'over']
TRIS, PROPS, LAY = pickle.load(open(f'{D}/interior2.pkl', 'rb'))
def load_tex(p): return np.array(Image.open(p).convert('RGBA')).astype(float) / 255
TEX = [None]                               # slot 0: interior atlas (day/night swapped in)
LIB = pickle.load(open('/home/claude/stages/texlib.pkl', 'rb'))
vidx = {}
def vtex(name):
    if name not in vidx:
        vidx[name] = len(TEX)
        if name == 'IRON': TEX.append(np.ones((2, 2, 4)) * np.array([0.24, 0.23, 0.22, 1]))
        else: TEX.append(LIB[name][0].astype(float) / 255)
    return vidx[name]
SCENE = [((0 if t[2] == 'ATLAS' else vtex(t[2])), t[0], t[1]) for t in TRIS]

def rot_y(a):
    c, s = math.cos(a), math.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])

# ---- Beedle's pack from his DAE ----
NS = '{http://www.collada.org/2005/11/COLLADASchema}'
BD = '/home/claude/beedle_tp/Beedle_Twilight_Princess'
r = ET.parse(f'{BD}/Beedle.dae').getroot()
imgs = {i.get('id'): i.find(NS + 'init_from').text for i in r.iter(NS + 'image')}
eff = {}
for e in r.iter(NS + 'effect'):
    surf = {}
    for np_ in e.iter(NS + 'newparam'):
        s_ = np_.find(NS + 'surface'); sm = np_.find(NS + 'sampler2D')
        if s_ is not None: surf[np_.get('sid')] = s_.find(NS + 'init_from').text
        if sm is not None: surf[np_.get('sid')] = surf.get(sm.find(NS + 'source').text)
    t = e.find('.//' + NS + 'diffuse/' + NS + 'texture')
    if t is not None:
        k = t.get('texture'); eff[e.get('id')] = imgs.get(surf.get(k, k), surf.get(k, k))
mat = {m.get('id'): eff.get(m.find(NS + 'instance_effect').get('url')[1:]) for m in r.iter(NS + 'material')}
geos = {g.get('id'): g for g in r.iter(NS + 'geometry')}
pack = []
texidx = {}
for c in r.iter(NS + 'controller'):
    cid = c.get('id')
    if '_Layer_' in cid or not any(k in cid for k in ('Base_1__', 'Flyer__', 'Plant__', 'Lantern_Lamp__')): continue
    sk = c.find(NS + 'skin'); g = geos[sk.get('source')[1:]]
    B = np.array([float(x) for x in sk.find(NS + 'bind_shape_matrix').text.split()]).reshape(4, 4)
    mesh = g.find(NS + 'mesh')
    src = {s.get('id'): np.array([float(x) for x in s.find(NS + 'float_array').text.split()]) for s in mesh.findall(NS + 'source')}
    vin = mesh.find(NS + 'vertices'); vmap = {vin.get('id'): vin.find(NS + 'input').get('source')[1:]}
    for tr in mesh.findall(NS + 'polylist') + mesh.findall(NS + 'triangles'):
        ins = tr.findall(NS + 'input'); stride = max(int(i.get('offset')) + 1 for i in ins)
        p = np.array(tr.find(NS + 'p').text.split(), int).reshape(-1, stride)
        vc = tr.find(NS + 'vcount')
        if vc is not None:
            vc = np.array(vc.text.split(), int); st = np.r_[0, np.cumsum(vc)[:-1]]; idx = []
            for a, n in zip(st, vc):
                for j in range(1, n - 1): idx += [a, a + j, a + j + 1]
            p = p[idx]
        pos = uv = None
        for i in ins:
            sid = i.get('source')[1:]; off = int(i.get('offset'))
            if i.get('semantic') == 'VERTEX': pos = src[vmap[sid]].reshape(-1, 3)[p[:, off]]
            if i.get('semantic') == 'TEXCOORD' and uv is None: uv = src[sid].reshape(-1, 2)[p[:, off]]
        pos = (np.c_[pos, np.ones(len(pos))] @ B.T)[:, :3]; pos = np.c_[pos[:, 0], pos[:, 2], -pos[:, 1]]
        uv = uv.copy(); uv[:, 1] = 1 - uv[:, 1]
        tf = mat.get(tr.get('material')) or mat.get(tr.get('material') + '-material')
        tf = os.path.basename(tf)
        if tf not in texidx:
            texidx[tf] = len(TEX); TEX.append(load_tex(f'{BD}/{tf}'))
        for k in range(0, len(pos), 3):
            pack.append((texidx[tf], pos[k:k + 3], uv[k:k + 3]))
P = np.concatenate([t[1] for t in pack])
mn, mx = P.min(0), P.max(0)
SC = 200.0 / (mx[1] - mn[1])   # the pack stands about 2 m tall, taller than Beedle himself
print('pack scale', SC, 'dae height', mx[1] - mn[1])
ctr = np.array([(mn[0] + mx[0]) / 2, mn[1], (mn[2] + mx[2]) / 2])
Rp = rot_y(math.radians(35))
spot = np.array([-LAY['W'] / 2 + 95, 0, LAY['ZB'] + 95])
for ti, p, uv in pack:
    SCENE.append((ti, ((p - ctr) * SC) @ Rp.T + spot, uv))

# ---- the game's own props ----
OBJ = '/mnt/user-data/uploads/New Modding/Rom Dump/files/res/Object'
_arcs = {}
def place_prop(arcname, model, pos, yaw_deg, scale):
    if arcname not in _arcs: _arcs[arcname] = rarc(open(f'{OBJ}/{arcname}.arc', 'rb').read())
    key = [k for k in _arcs[arcname] if k.endswith('/' + model)][0]
    b = BMD(_arcs[arcname][key])
    base = len(TEX)
    for t in b.tex: TEX.append(np.asarray(t, float) / 255)
    solid = {}
    Rm = rot_y(math.radians(yaw_deg))
    if scale < 0:     # negative scale = fit to this height (game units)
        P_ = np.concatenate([np.asarray(t[1], float) for t in b.tris]); scale = -scale / (P_[:, 1].max() - P_[:, 1].min())
    for mi, p, uv in b.tris:
        texi, col = b.mats[mi] if mi < len(b.mats) else (None, np.ones(4))
        if texi is None or texi >= len(b.tex):
            k2 = tuple(np.round(col, 3))
            if k2 not in solid: solid[k2] = len(TEX); TEX.append(np.ones((2, 2, 4)) * np.r_[np.asarray(col, float)[:3], 1.0])
            ti = solid[k2]
        else: ti = base + texi
        SCENE.append((ti, (np.asarray(p, float) * scale) @ Rm.T + pos, np.asarray(uv, float)))
for arcname, model, pos, yaw, sc in PROPS:
    place_prop(arcname, model, np.asarray(pos, float), yaw, sc)

# ---- Wind Waker curios + spoils bag from itemmdl.arc ----
arc = rarc(open('/home/claude/itemmdl/itemmdl.arc', 'rb').read())
def add_bmd(name, place, size, yaw=0.0, pitch=0.0, anchor='bottom', hang=False, loop_along=None):
    b = BMD(arc[f'bdlm/{name}.bdl'])
    if hang:
        # rotate the model so its highest point (the strap) sits straight above its centre of mass,
        # the way something hanging from a single point settles
        P0 = np.concatenate([np.asarray(t[1], float) for t in b.tris])
        top = P0[P0[:, 1].argmax()]; com = P0.mean(0)
        a = (top - com) / np.linalg.norm(top - com); bvec = np.array([0, 1.0, 0])
        v = np.cross(a, bvec); cth = a @ bvec; K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
        Rh = np.eye(3) + K + K @ K / (1 + cth)
        b.tris = [(mi, np.asarray(p, float) @ Rh.T, uv) for mi, p, uv in b.tris]
        if loop_along is not None:
            # the strap loop is the top part of the model; its flattest axis is the loop's normal.
            # Turn the bag so that normal runs along the hook arm, so the arm passes through the loop.
            P1 = np.concatenate([np.asarray(t[1], float) for t in b.tris])
            ytop = P1[:, 1].max(); h = ytop - P1[:, 1].min()
            Q = P1[P1[:, 1] > ytop - 0.3 * h]
            _, _, vt = np.linalg.svd(Q - Q.mean(0)); n = vt[2]
            t_ = np.asarray(loop_along, float)
            yaw = math.atan2(t_[0], t_[2]) - math.atan2(n[0], n[2]) + math.pi   # face toward the room
    base = len(TEX)
    for t in b.tex: TEX.append(np.asarray(t, float) / 255)
    solid = {}
    P = np.concatenate([np.asarray(t[1], float) for t in b.tris])
    mn, mx = P.min(0), P.max(0)
    s = size / (mx - mn).max()
    c = np.array([(mn[0] + mx[0]) / 2, mn[1] if anchor == 'bottom' else mx[1], (mn[2] + mx[2]) / 2])
    if hang:   # hang from the strap's highest point itself, not the middle of the bounding box
        c = P[P[:, 1].argmax()].copy()
    cp, sp = math.cos(pitch), math.sin(pitch)
    Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    Rm = rot_y(yaw) @ Rx
    for mi, p, uv in b.tris:
        texi, col = b.mats[mi] if mi < len(b.mats) else (None, np.ones(4))
        if texi is None or texi >= len(b.tex):
            key = tuple(np.round(col, 3))
            if key not in solid:
                solid[key] = len(TEX); TEX.append(np.ones((2, 2, 4)) * np.r_[np.asarray(col, float)[:3], 1.0])
            ti = solid[key]
        else:
            ti = base + texi
        q = ((np.asarray(p, float) - c) * s) @ Rm.T
        if pitch: q[:, 1] -= min(0, (((mn - c) * s) @ Rm.T)[1])
        SCENE.append((ti, q + place, np.asarray(uv, float)))
# spoils bag hanging from the hook by its strap
add_bmd('vbage', LAY['HOOK'] + np.array([0, 2.6, 0]), 46, anchor='top', hang=True, loop_along=LAY['HOOK_DIR'])
spots = LAY['WW_SPOTS']
order = [0, 3, 6, 9, 1, 4, 7, 10, 2, 5]
items = [('vtele', 30, 0.0), ('vboxn', 24, 0.0), ('vtact', 34, 0.0), ('vchin', 22, 0.0), ('vbagf', 22, 0.0),
         ('vhamm', 40, 0.0), ('vrope', 26, 0.0), ('vleaf', 32, 0.0), ('vho', 28, 0.0), ('vbagh', 26, 0.0)]
for (name, size, _), si in zip(items, order):
    p, side = spots[si % len(spots)]
    add_bmd(name, p + np.array([0, 0.5, 0]), size, yaw=math.radians(90 if side < 0 else -90))

CAMS = {
    'door':    (np.array([0, 165, LAY['ZF'] - 45]), np.array([0, 110, LAY['ZB'] + 120]), 72),
    'counter': (np.array([-260, 200, LAY['ZB'] + 110]), np.array([150, 80, 220]), 72),
    'shelf':   (np.array([60, 150, -60]), np.array([LAY['W'] / 2, 105, -175]), 62),
    'over':    (np.array([0, 520, 420]), np.array([0, 0, -170]), 74),
    'corner':  (np.array([60, 170, LAY['ZB'] + 380]), np.array([-LAY['W'] / 2 + 95, 100, LAY['ZB'] + 95]), 60),
    'glimpse': (np.array([-25, 150, LAY['ZF'] - 35]), np.array([125, 112, -60]), 62),
    'hook':    (np.array([LAY['W'] / 2 - 68, 185, LAY['ZB'] + 175]), np.array([LAY['W'] / 2 - 82, 155, LAY['ZB'] + 92]), 42),
    'hook2':   (np.array([LAY['W'] / 2 - 175, 190, LAY['ZB'] + 175]), np.array([LAY['W'] / 2 - 80, 150, LAY['ZB'] + 85]), 40),
}
for tag in ('day', 'night'):
    TEX[0] = load_tex(f'{D}/interior2_{tag}_HD_2048.png')
    for v in views:
        if tag == 'night' and v not in ('door', 'glimpse'): continue
        eye, tgt, fov = CAMS[v]
        Wv, Hv = (360, 1000) if v == 'glimpse' else (900, 560)
        im = render(SCENE, TEX, eye, tgt, fov=fov, W=Wv, H=Hv, ss=2)
        im.save(f'{D}/view_{v}_{tag}.png'); print('rendered', v, tag)
