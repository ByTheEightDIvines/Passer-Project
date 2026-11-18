"""Beedle's game models from the Blender export (export_blend.py): on the men's 19-joint skeleton (MAN_a's
joint frames, Beedle's own bone positions), so the townsfolk animations move him.
Usage: python3 -I build_beedle.py beedle.pkl MAN_a.arc OUT.bmd [eye: open|...]"""
import sys, pickle, struct
sys.path[:0] = ['/home/claude/w/anim', '/home/claude/passer-project/tools/beedle']
import numpy as np
import rarc
from bmd import BMD, joint_names, u32
from skinbmd import write_skinned

src, man_arc, out = sys.argv[1:4]
D = pickle.load(open(src, 'rb'))
d = open(man_arc, 'rb').read(); r = rarc.rarc(rarc.yaz0(d) if d[:4] == b'Yaz0' else d)
man = BMD(r[[k for k in r if k.endswith('.bmd')][0]])
names = joint_names(man); n = len(names)
o = man.sec['JNT1']; eo = o + u32(man.d, o + 0xC)
entries = [bytes(man.d[eo + i * 0x40: eo + (i + 1) * 0x40]) for i in range(n)]
world_pos = [D['bones'][nm] for nm in names]
skel = dict(names=names, parent=man.parent, jnt_entries=entries, world_pos=world_pos)
J = {nm: i for i, nm in enumerate(names)}
groups = []; order = []
bymat = {}
for obj, tris in D['meshes']:
    for mat, corners in tris:
        bymat.setdefault(mat, []).append([(p, nrm, uv, {J[k]: v for k, v in w.items()}) for p, nrm, uv, w in corners])
for mat, tris in bymat.items():
    name, img = D['images'].get(mat, (None, np.full((8, 8, 4), 200, np.uint8)))
    if mat == 'SC_eye' and 'bs_eye.png' in D['eyes']: img = D['eyes']['bs_eye.png']
    alpha = bool((img[..., 3] < 128).any())
    groups.append((img, alpha, tris)); order.append((mat, name, img.shape, alpha, len(tris)))
info = write_skinned(out, skel, groups)
print(info)
for x in order: print('  ', x)
