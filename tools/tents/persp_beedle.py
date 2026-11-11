"""Perspective views of Beedle's tent (as a player sees it), day and night, including the stage props."""
import sys, math, pickle, numpy as np
sys.path.insert(0, '/home/claude/ptb/Passer_Travellers_Build/tools'); sys.path.insert(0, '/home/claude/work/tent')
from PIL import Image
from rarc import rarc
from bmd import BMD
from persp import render
d = sys.argv[1]; out = sys.argv[2]
body = pickle.load(open(f'{d}/beedle_tent_tris.pkl', 'rb')); glow = pickle.load(open(f'{d}/beedle_tent_glow_tris.pkl', 'rb'))
STAGE = pickle.load(open(f'{d}/beedle_stage_props.pkl', 'rb'))
tb = np.array(Image.open(f'{d}/beedle_tent_HD_2048.png').convert('RGBA')).astype(float) / 255
tg = np.array(Image.open(f'{d}/beedle_tent_glow_512.png').convert('RGBA')).astype(float) / 255
OBJ = '/mnt/user-data/uploads/New Modding/Rom Dump/files/res/Object'
props = []
for arcname, model, pos, yaw, sc in STAGE:
    r = rarc(open(f'{OBJ}/{arcname}.arc', 'rb').read()); key = [k for k in r if k.endswith('/' + model)][0]
    b = BMD(r[key]); c_, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw)); Rm = np.array([[c_, 0, s_], [0, 1, 0], [-s_, 0, c_]])
    texs = [np.asarray(t, float) / 255 for t in b.tex]; tris = []
    for mi, p, uv in b.tris:
        ti, col = b.mats[mi] if mi < len(b.mats) else (None, np.ones(4))
        if ti is None or ti >= len(texs): texs.append(np.ones((2, 2, 4)) * np.r_[np.asarray(col)[:3], 1]); ti = len(texs) - 1
        tris.append((ti, (np.asarray(p, float) * sc) @ Rm.T + np.asarray(pos), np.asarray(uv, float)))
    props.append((arcname, pos, texs, tris))
def scene(night):
    T = tb.copy()
    if night: T[..., :3] *= np.array([0.30, 0.33, 0.45])
    tex = [T, tg]; sc = [(0, p, uv) for p, uv in body] + ([(1, p, uv) for p, uv in glow] if night else [])
    for arcname, pos, texs, tris in props:
        base = len(tex); behind = pos[2] < 210; lamp = arcname == 'Obj_kntr'
        for t in texs:
            t = t.copy()
            if lamp and night: t[..., :3] = np.clip(t[..., :3] * np.array([1.9, 1.6, 1.1]) + 0.15, 0, 1)
            elif night: t[..., :3] *= (np.array([1.1, 0.9, 0.66]) if behind else np.array([0.30, 0.33, 0.45]))
            elif behind: t[..., :3] *= np.array([0.62, 0.55, 0.46])
            tex.append(t)
        sc += [(base + ti, p, uv) for ti, p, uv in tris]
    return sc, tex
CAMS = [((0, 170, 630), (0, 150, 150), 50), ((-230, 170, 560), (0, 140, 150), 50), ((160, 150, 450), (-10, 130, 120), 55)]
ims = []
for night in (False, True):
    sc, tex = scene(night)
    for eye, tgt, fov in CAMS:
        ims.append(render(sc, tex, np.array(eye, float), np.array(tgt, float), fov=fov, W=520, H=600, ss=2,
                          bg=(26, 30, 48) if night else (150, 170, 190)))
o = Image.new('RGB', (520 * 3, 600 * 2))
for i, im in enumerate(ims): o.paste(im, ((i % 3) * 520, (i // 3) * 600))
o.save(out)
