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
def put(name, tris, **kw):
    p = os.path.join(OUT, name + '.bmd')
    write_rigid_bmd(p, [(np.asarray(a, float), np.asarray(b, float)) for a, b in tris], atlas, texname=name, jointname=name, matname=name + '_m', **kw)
    files['bmdr/%s.bmd' % name] = open(p, 'rb').read()
    print(name, len(tris), 'tris,', len(BMD(files['bmdr/%s.bmd' % name]).tris), 'read back,', len(files['bmdr/%s.bmd' % name]) // 1024, 'KB')
put('a_tent', T['tris'], colors=[np.asarray(c, float) for c in T['cols']])
put('camp_flap', F['tris'])
write_rarc(os.path.join(OUT, 'PsrTent.arc'), 'PsrTent', files)
print(sorted(rarc(open(os.path.join(OUT, 'PsrTent.arc'), 'rb').read())), os.path.getsize(os.path.join(OUT, 'PsrTent.arc')) // 1024, 'KB')
