import sys; sys.argv=sys.argv[:1]; sys.path.insert(0,'/home/claude/anim')
from fist import *
from PIL import ImageDraw
class S: pass
def view(b, rig, G, extra_pts=(), size=300, half=18, wpn=None):
    P_all = []
    j = rig.idx['handR']; inv = np.linalg.inv(rig.rest_world[j])
    tris = [(mi, (P @ inv[:3, :3].T) + inv[:3, 3], uv) for (mi, P, uv), js in zip(b.tris, b.trij) if all(x in (j, rig.idx.get('fingerR', -9)) for x in js)]
    a = G[:3, 2]; u = G[:3, 0]; v = G[:3, 1]
    out = []
    for lab, R in [('looking down the grip', np.stack([u, v, -a])), ('side', np.stack([a, v, u])), ('top', np.stack([u, a, v]))]:
        s = S(); s.tex = b.tex; s.mats = b.mats
        c = G[:3, 3]
        s.tris = [(mi, (P - c) @ R.T, uv) for mi, P, uv in tris]
        if wpn is not None and lab != 'looking down the grip':
            s.tex = list(b.tex) + list(wpn.tex); off = len(b.mats)
            s.mats = list(b.mats) + [((t + len(b.tex)) if t is not None else None, cc) for t, cc in wpn.mats]
            s.tris += [(mi + off, ((P @ G[:3, :3].T + G[:3, 3]) - c) @ R.T, uv) for mi, P, uv in wpn.tris]
        im = render(s, 0, W=size, H=size, pad=0, bounds=(np.array([-half, -half, -300.]), np.array([half, half, 300.])))
        d = ImageDraw.Draw(im); sc = size / (2 * half)
        def px(q): return (q[0] + half) * sc, size - (q[1] + half) * sc
        o = px(np.zeros(3)); d.ellipse([o[0] - 4, o[1] - 4, o[0] + 4, o[1] + 4], outline=(60, 255, 60), width=2)
        for q, col in extra_pts:
            p = px((q - c) @ R.T); d.ellipse([p[0] - 4, p[1] - 4, p[0] + 4, p[1] + 4], outline=col, width=2)
        out.append((im, lab))
    return out
