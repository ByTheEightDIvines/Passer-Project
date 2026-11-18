"""Burnt-out wagon models (PsrWagon.arc additions) and the viewer data.
royal: royal_body/harness/wheels/cargo charred in place; royal_cover_burnt rebuilt (burnt_cover.py)."""
import sys, pickle, json, base64, io, numpy as np
sys.path[:0] = ['/home/claude/w/anim', '/home/claude/passer-project/tools/convoy']
from rarc import rarc, yaz0
from bmd import BMD, joint_names
from PIL import Image
import burn, burnt_cover

def arc(p):
    d = open(p, 'rb').read(); return rarc(yaz0(d) if d[:4] == b'Yaz0' else d)

ROYAL = pickle.load(open('/home/claude/gc/royal_bmds.pkl', 'rb'))
def royal_burnt():
    out = {}
    for i, k in enumerate(['royal_body', 'royal_harness', 'royal_f_l_wheel', 'royal_f_r_wheel',
                           'royal_b_l_wheel', 'royal_b_r_wheel', 'royal_cargo', 'trav_cargo']):
        src = ROYAL['bmdr/%s.bmd' % k]
        out[k + '_burnt'] = burn.patch(src, lambda j, t, s=i: burn.char(t, 100 + s * 13 + j))
    wood = np.asarray(BMD(ROYAL['bmdr/royal_harness.bmd']).tex[0], np.uint8)
    burnt_cover.build(ROYAL['bmdr/royal_cover.bmd'], wood, '/tmp/burn/royal_cover_burnt.bmd')
    out['royal_cover_burnt'] = open('/tmp/burn/royal_cover_burnt.bmd', 'rb').read()
    return out

def posed(b, base=np.eye(4)):
    b.jworld = [base @ b.world(i) for i in range(len(b.jloc))]; return b

def mesh(data, base):
    """(positions, uvs, tex index) per material, in world space, and the textures as PNG data URIs"""
    m = BMD(data)
    if len(m.jloc) == 1: m.jworld = [base]
    else: posed(m, base)
    m.read_shp()
    groups = {}
    for mi, pp, uv in m.tris:
        ti = m.mats[mi][0] if mi < len(m.mats) else 0
        g = groups.setdefault(ti or 0, ([], []))
        g[0].extend(np.asarray(pp, float).reshape(-1).round(1).tolist()); g[1].extend(np.asarray(uv, float).reshape(-1).round(4).tolist())
    tex = []
    for t in m.tex:
        a = np.asarray(t, np.uint8); buf = io.BytesIO(); Image.fromarray(a).save(buf, 'PNG')
        tex.append('data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode())
    wrap = [list(getattr(t, 'wrap', (1, 1))) for t in m.tex]
    return dict(groups=[dict(t=k, p=v[0], uv=v[1]) for k, v in groups.items()], tex=tex, wrap=wrap)

if __name__ == '__main__':
    A = arc('/mnt/user-data/uploads/Object/Coach.arc')
    harn = posed(BMD(A['bmdr/harness.bmd'])); bs = harn.jworld[joint_names(harn).index('bs_loc')]
    T = np.eye(4); T[:3, 3] = bs[:3, 3]
    coach = posed(BMD(A['bmdr/coach.bmd']), T)
    frames = {'royal_body': T, 'royal_cover': T, 'royal_cargo': T, 'royal_harness': np.eye(4),
              'royal_f_l_wheel': harn.jworld[2], 'royal_f_r_wheel': harn.jworld[3],
              'royal_b_l_wheel': coach.jworld[1], 'royal_b_r_wheel': coach.jworld[2]}
    burnt = royal_burnt()
    pickle.dump(burnt, open('/tmp/burn/burnt_bmds.pkl', 'wb'))
    view = {'original': [], 'burnt': []}
    for k, F in frames.items():
        view['original'].append(mesh(ROYAL['bmdr/%s.bmd' % k], F))
        view['burnt'].append(mesh(burnt[k + '_burnt'], F))
    json.dump(view, open('/tmp/burn/view.json', 'w'))
    print({k: len(v) for k, v in burnt.items()})
