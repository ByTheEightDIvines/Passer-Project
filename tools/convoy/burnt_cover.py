"""The royal cover, burnt: the red canvas mostly gone (the top first), ragged scraps with scorched edges
left lower down, and the bare hoops it hung on, charred. One model, one 512x512 atlas:
canvas (alpha-cut) left half, the cover's trim texture top right, charred hoop wood bottom right."""
import sys, pickle, numpy as np
sys.path[:0] = ['/home/claude/w/anim', '/home/claude/passer-project/tools/convoy']
from bmd import BMD
from bmdwrite import write_rigid_bmd
from scipy import ndimage
from PIL import Image
import burn

def build(cover_bmd, wood_rgba, out_path, seed=11):
    m = BMD(cover_bmd); m.jworld = [np.eye(4)]; m.read_shp()
    canvas = [(np.asarray(p, float), np.asarray(u, float)) for mi, p, u in m.tris if m.mats[mi][0] == 0]
    trim = [(np.asarray(p, float), np.asarray(u, float)) for mi, p, u in m.tris if m.mats[mi][0] == 1]
    return build_from(canvas, trim, np.asarray(m.tex[0], np.uint8), np.asarray(m.tex[1], np.uint8), wood_rgba, out_path, seed)

def build_from(canvas, trim, ctex, trimtex, wood_rgba, out_path, seed=11, burn_more=0.0):
    """canvas/trim: [(pos 3x3, uv 3x2)]; canvas uvs over ctex (0..1), trim uvs over trimtex (0..1)."""
    th, tw = ctex.shape[:2]
    # per texel: the height on the wagon (rasterise the canvas triangles in UV space)
    Y = np.full((th, tw), np.nan)
    for P, U in canvas:
        px = U * [tw, th]
        lo = np.floor(px.min(0)).astype(int); hi = np.ceil(px.max(0)).astype(int)
        xs, ys = np.meshgrid(np.arange(max(lo[0], 0), min(hi[0], tw)), np.arange(max(lo[1], 0), min(hi[1], th)))
        if xs.size == 0: continue
        q = np.stack([xs + 0.5, ys + 0.5], -1).reshape(-1, 2)
        a, b, c = px; M = np.array([b - a, c - a]).T
        if abs(np.linalg.det(M)) < 1e-9: continue
        l = np.linalg.solve(M, (q - a).T).T; w = np.c_[1 - l.sum(1), l]
        ok = (w >= -0.02).all(1); yy = (w[ok] @ P[:, 1])
        Y[ys.reshape(-1)[ok], xs.reshape(-1)[ok]] = yy
    Y = np.where(np.isnan(Y), np.nanmean(Y), Y)
    hn = (Y - Y.min()) / max(np.ptp(Y), 1)                                     # 0 at the hem .. 1 at the top
    n = burn.noise(th, tw, seed, base=3); n2 = burn.noise(th, tw, seed + 5, base=10)
    keep = (n * 0.75 + n2 * 0.25) > 0.30 + burn_more + hn * 0.55                            # the higher, the more is gone
    keep = ndimage.binary_opening(keep, iterations=2)                           # no single-pixel threads
    lab, k = ndimage.label(keep); sizes = ndimage.sum(keep, lab, range(1, k + 1))
    keep = np.isin(lab, 1 + np.where(sizes > 120)[0])                           # only scraps worth the name
    dist = ndimage.distance_transform_edt(keep)                                 # texels in from a burnt edge
    cloth = burn.char(ctex, seed, strength=0.35).astype(float)                  # the surviving cloth: scorched
    edge = np.clip(1 - dist / 9.0, 0, 1)[..., None]                             # blackened toward every hole
    rim = np.clip(1 - dist / 2.5, 0, 1)[..., None]                              # the very edge: ember-brown char
    cloth[..., :3] = cloth[..., :3] * (1 - edge * 0.85) + np.array([18, 12, 9]) * edge * 0.85
    cloth[..., :3] = cloth[..., :3] * (1 - rim * 0.5) + np.array([70, 34, 16]) * rim * 0.5
    cloth[..., 3] = np.where(keep, 255, 0)
    atlas = np.zeros((512, 512, 4), np.uint8)
    atlas[:, :256] = np.asarray(Image.fromarray(cloth.astype(np.uint8)).resize((256, 512), Image.NEAREST))   # (any canvas size, stretched)
    t1 = burn.char(np.asarray(trimtex, np.uint8), seed + 1)
    atlas[:256, 256:] = np.asarray(Image.fromarray(t1).convert('RGBA').resize((256, 256)))
    wd = burn.char(np.asarray(Image.fromarray(wood_rgba).convert('RGBA').resize((256, 256))), seed + 2, 1.3)
    atlas[256:, 256:] = wd; atlas[256:, 256:, 3] = 255; atlas[:256, 256:, 3] = 255
    tris = []
    for P, U in canvas: tris.append((P, U * [0.5, 1.0]))
    for P, U in trim: tris.append((P, np.clip(U, 0, 1) * 0.5 + [0.5, 0.0]))
    # the hoops: the canvas's cross-section at a few places along it, swept as a square bar
    allP = np.concatenate([P for P, U in canvas]); z0, z1 = allP[:, 2].min(), allP[:, 2].max()
    for z in np.linspace(z0 + 25, z1 - 25, 5):
        segs = []
        for P, U in canvas:
            d = P[:, 2] - z; pts = []
            for i in range(3):
                j = (i + 1) % 3
                if (d[i] < 0) != (d[j] < 0):
                    t = d[i] / (d[i] - d[j]); pts.append(P[i] + (P[j] - P[i]) * t)
            if len(pts) == 2: segs += pts
        if len(segs) < 4: continue
        S = np.array(segs)[:, :2]                                               # the arch's top edge, binned across
        xs = np.linspace(S[:, 0].min(), S[:, 0].max(), 15); prof = []
        for i in range(len(xs)):
            near = np.abs(S[:, 0] - xs[i]) <= (xs[1] - xs[0]) * 0.75
            if near.any(): prof.append((xs[i], S[near, 1].max()))
        S = np.array(prof)
        if len(S) < 4: continue
        S[1:-1, 1] = np.convolve(S[:, 1], [0.25, 0.5, 0.25], 'valid')            # smooth: no spikes
        inset = 4.0; r = 4.5
        for i in range(len(S) - 1):
            a, b = S[i], S[i + 1]; dirv = (b - a) / max(np.hypot(*(b - a)), 1e-6)
            nrm = np.array([-dirv[1], dirv[0]])
            if nrm[1] < 0: nrm = -nrm                                           # point it inward/up out of the cloth
            a3 = lambda p, o, dz: np.array([p[0] - nrm[0] * (inset + o), p[1] - nrm[1] * (inset + o), z + dz])
            ring = lambda p: [a3(p, -r, -r), a3(p, r, -r), a3(p, r, r), a3(p, -r, r)]
            ra, rb = ring(a), ring(b); v0 = 0.5 + (i % 8) / 16.0
            for k in range(4):
                p0, p1, p2, p3 = ra[k], ra[(k + 1) % 4], rb[(k + 1) % 4], rb[k]
                u0, u1 = 0.5 + 0.05 + 0.1 * k, 0.5 + 0.12 + 0.1 * k
                tris.append((np.array([p0, p1, p2]), np.array([[u0, v0], [u1, v0], [u1, v0 + 0.06]])))
                tris.append((np.array([p0, p2, p3]), np.array([[u0, v0], [u1, v0 + 0.06], [u0, v0 + 0.06]])))
    write_rigid_bmd(out_path, tris, atlas, texname='psr_cvbrn', jointname='root', matname='psr_cvbrn', alpha=True)
    return atlas, keep
