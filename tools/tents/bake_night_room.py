"""Re-bake the night copy of Beedle's shop seen through the tent windows (PsrBdRm beedle_room_n.bmd):
lit by its three lanterns. The day room's textures are the colours; the walls and roof (one baked atlas)
get warm light falling off from each lantern texel by texel; the furniture, whose textures are shared,
gets the light averaged where it stands; the lanterns' own glass glows.
Usage: python3 -I bake_night_room.py PsrBdRm.arc OUT_DIR"""
import sys, os, struct, numpy as np
sys.path[:0] = ['/home/claude/w/anim']
from bmd import BMD
from rarc import rarc
from multibmd import write_multi
src, out = sys.argv[1:3]
a = rarc(open(src, 'rb').read())
D = BMD(a['bmdr/beedle_room.bmd']); N = BMD(a['bmdr/beedle_room_n.bmd'])
for b in (D, N): b.jworld = [b.world(i) for i in range(len(b.jloc))]; b.read_shp()
assert len(D.tris) == len(N.tris)
LANTERNS = np.array([[0, 235, z] for z in (-200, 0, 200)], float)   # flame centres, room space
WARM = np.array([1.0, 0.70, 0.40]); AMB = np.array([0.22, 0.24, 0.32])
def light(P, nrm=None):
    """light reaching points P (n,3): night ambient plus the lanterns' warm glow"""
    g = np.zeros(len(P))
    for c in LANTERNS:
        d = c - P; r = np.linalg.norm(d, axis=1) + 1e-6
        f = 1.9 / (1 + (r / 95.0) ** 2)
        if nrm is not None: f *= np.clip(np.abs(np.sum(d / r[:, None] * nrm, 1)) * 0.7 + 0.3, 0, 1)
        g += f
    return AMB[None] + g[:, None] * WARM[None]
SHELL = {0, 43}; LAMP = 12
tex = [t.astype(float) for t in D.tex]
new = []
for mi, m in enumerate(D.mats):
    T = tex[m[0]].copy(); h, w = T.shape[:2]
    tris = [(np.asarray(p, float), np.asarray(uv, float)) for k, p, uv in D.tris if k == mi]
    if mi == LAMP:
        L = np.clip(T[..., :3] * 1.25 * np.array([1.25, 1.05, 0.75]) + 25, 0, 255)
        gold = (T[..., 0] > 120) & (T[..., 0] > T[..., 2] + 40)          # the glass panels
        L[gold] = np.clip(L[gold] * 1.5 + np.array([60, 45, 10]), 0, 255)
        T[..., :3] = L
    elif mi in SHELL:
        acc = np.zeros((h, w, 3)); cnt = np.zeros((h, w))
        for P, uv in tris:
            n = np.cross(P[1] - P[0], P[2] - P[0]); n /= np.linalg.norm(n) + 1e-9
            px = uv[:, 0] * w; py = uv[:, 1] * h
            x0, x1 = int(max(np.floor(px.min()) - 1, 0)), int(min(np.ceil(px.max()) + 1, w - 1))
            y0, y1 = int(max(np.floor(py.min()) - 1, 0)), int(min(np.ceil(py.max()) + 1, h - 1))
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            dn = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2])
            if abs(dn) < 1e-9: continue
            w0 = ((py[1] - py[2]) * (gx - px[2]) + (px[2] - px[1]) * (gy - py[2])) / dn
            w1 = ((py[2] - py[0]) * (gx - px[2]) + (px[0] - px[2]) * (gy - py[2])) / dn
            w2 = 1 - w0 - w1
            ins = (w0 > -0.05) & (w1 > -0.05) & (w2 > -0.05)
            if not ins.any(): continue
            Q = (w0[ins, None] * P[0] + w1[ins, None] * P[1] + w2[ins, None] * P[2])
            lit = light(Q, np.broadcast_to(n, Q.shape))
            yy = (gy[ins] - 0.5).astype(int); xx = (gx[ins] - 0.5).astype(int)
            np.add.at(acc, (yy, xx), lit); np.add.at(cnt, (yy, xx), 1)
        fill = acc[cnt > 0].sum(0) / cnt[cnt > 0].sum() if (cnt > 0).any() else AMB
        Lm = np.where(cnt[..., None] > 0, acc / np.maximum(cnt, 1)[..., None], fill)
        T[..., :3] = np.clip(T[..., :3] * Lm, 0, 255)
    elif tris:
        C = np.array([p.mean(0) for p, _ in tris])
        T[..., :3] = np.clip(T[..., :3] * light(C).mean(0) * 1.25, 0, 255)
    else:
        T[..., :3] *= AMB.mean()
    new.append(T.astype(np.uint8))
groups = []
for mi, m in enumerate(D.mats):
    tris = [(np.asarray(p, float), np.asarray(uv, float)) for k, p, uv in N.tris if k == mi]
    if not tris: continue
    t = new[mi]
    groups.append((t, bool(t[..., 3].min() < 128), tris, getattr(D.tex[m[0]], 'wrap', (1, 1))))
os.makedirs(out, exist_ok=True)
print(write_multi(os.path.join(out, 'beedle_room_n.bmd'), groups, jointname='beedle_room_n', cull=0))
