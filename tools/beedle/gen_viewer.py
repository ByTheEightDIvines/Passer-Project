"""Viewer data for Beedle: his game models (read back from the BMDs we write) posed by the game's own townsfolk
animations frame by frame, textures as PNG data. Usage: python3 -I gen_viewer.py OUT_DIR"""
import sys, json, base64, io
sys.path[:0] = ['/home/claude/w/anim']
import numpy as np, rarc
from bmd import BMD
from bck import BCK, srt_matrix
from PIL import Image
out = sys.argv[1]
U = '/mnt/user-data/uploads/Desktop/New Modding/Rom Dump/files/res/Object/'
def arc(p):
    d = open(p, 'rb').read(); return rarc.rarc(rarc.yaz0(d) if d[:4] == b'Yaz0' else d)
MG = arc(U + 'Mgeneral.arc')
def bck(name): return BCK([v for k, v in MG.items() if k.endswith(name)][0])
def frames(path, anim, step):
    base = BMD(open(path, 'rb').read()); t = bck(anim) if anim else None
    n = t.dur if t is not None else 1
    res = []
    for f in range(0, max(1, n), step):
        b = BMD(open(path, 'rb').read())
        if t is not None:
            bind = [m.copy() for m in b.jloc]; L = []
            for j in range(len(b.jloc)):
                m = srt_matrix(*t.sample(j, f))
                if j > 0: m[:3, 3] = bind[j][:3, 3]
                L.append(m)
            b.jloc = L
        b.jworld = [b.world(i) for i in range(len(b.jloc))]; b.read_shp()
        res.append(b)
    return res
def png(a, size):
    im = Image.fromarray(np.asarray(a)[..., :4].astype(np.uint8)).resize((size, size)); bio = io.BytesIO(); im.save(bio, 'PNG')
    return 'data:image/png;base64,' + base64.b64encode(bio.getvalue()).decode()
models = {}
for key, path in (('field', '/home/claude/beedle/beedle.bmd'), ('tent', '/home/claude/beedle/beedle_tent.bmd')):
    b0 = BMD(open(path, 'rb').read())
    mats = sorted(set(m for m, _, _ in b0.tris))
    tex = [png(b0.tex[b0.mats[m][0]], 256) for m in mats]
    uvs = {m: np.array([uv for mm, _, uv in b0.tris if mm == m]).ravel().round(4).tolist() for m in mats}
    anims = {}
    for nm, anim, step in (('Standing', 'm_wait_a.bck', 3), ('Walking', 'm_walk_a.bck', 2), ('Running', 'm_run_a.bck', 2), ('Sitting', 'm_sitwait_a.bck', 4)):
        fr = frames(path, anim, step)
        anims[nm] = [{str(m): base64.b64encode(np.array([p for mm, p, _ in b.tris if mm == m], np.float32).ravel().tobytes()).decode() for m in mats} for b in fr]
    models[key] = dict(mats=[str(m) for m in mats], tex=tex, uvs={str(k): v for k, v in uvs.items()}, anims=anims)
json.dump(models, open(out + '/view.json', 'w'))
print({k: {a: len(v) for a, v in m['anims'].items()} for k, m in models.items()})
