"""Beedle's shading: rebuild a skinned BMD (one written by skinbmd.py) with proper normals on the named
materials: each face's normal turned out from the middle of its piece, then smoothed across neighbours
(hard edges kept past CREASE degrees). The other materials keep their normals. Everything else (joints,
weights, UVs, textures, material order) comes from the model itself and goes back through skinbmd.write_skinned.
Usage: python3 -I fix_normals.py IN.bmd OUT.bmd MAT[:CREASE][,...]   (Beedle field: 2:180,3,4:180,5; tent: 2:180,3)"""
import sys, os, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = ['/home/claude/w/anim', HERE]
from bmd import BMD, joint_names, u16, u32
from skinbmd import write_skinned

CREASE = 70.0

def corners(b):
    """per material: list of triangles, corner = (pos, nrm, uv, {joint: weight}) in model space"""
    d = b.d; o = b.sec['JNT1']; eo = o + u32(d, o + 0xC); n = len(b.jloc)
    b.jworld = [b.world(i) for i in range(n)]
    out = {}
    so_ = b.sec['SHP1']; ns = u16(d, so_ + 8)
    so, _, _, ao, mto, po, mdo, plo = [u32(d, so_ + 0xC + 4 * i) for i in range(8)]
    o = so_
    for si in range(ns):
        e = o + so + si * 0x28
        npk = u16(d, e + 2); aoff = u16(d, e + 4); fmd = u16(d, e + 6); fpk = u16(d, e + 8)
        attrs = []; p = o + ao + aoff
        while True:
            a, t = u32(d, p), u32(d, p + 4); p += 8
            if a == 0xFF: break
            attrs.append((a, t))
        mats = []
        for pk in range(npk):
            md = o + mdo + (fmd + pk) * 8; cnt = u16(d, md + 2); fi = u32(d, md + 4)
            lst = [u16(d, o + mto + 2 * (fi + k)) for k in range(cnt)]
            if mats: lst = [mats[k] if v == 0xFFFF and k < len(mats) else v for k, v in enumerate(lst)]
            mats = lst
            ploc = o + plo + (fpk + pk) * 8; psz, poff = u32(d, ploc), u32(d, ploc + 4)
            q = o + po + poff; qe = q + psz
            while q < qe:
                op = d[q]
                if op == 0: break
                c = u16(d, q + 1); q += 3; vs = []
                for _ in range(c):
                    v = {}
                    for a, t in attrs:
                        if t in (1, 2): val = d[q]; q += 1
                        elif t == 3: val = u16(d, q); q += 2
                        else: val = None
                        v[a] = val
                    vs.append(v)
                if op == 0x90: idx = [(i, i + 1, i + 2) for i in range(0, c - 2, 3)]
                elif op == 0x98: idx = [((i, i + 1, i + 2) if i % 2 == 0 else (i + 1, i, i + 2)) for i in range(c - 2)]
                elif op == 0xA0: idx = [(0, i + 1, i + 2) for i in range(c - 2)]
                else: idx = []
                cs = []
                for v in vs:
                    mi = mats[v[0] // 3] if 0 in v else mats[0]
                    w, i = b.drw[mi]
                    if w:
                        wts = {j: wt for j, wt in b.env[i]}
                        M = sum(wt * (b.jworld[j] @ b.ibm[j]) for j, wt in b.env[i])
                    else:
                        wts = {i: 1.0}; M = b.jworld[i]
                    pos = (M @ np.append(b.arr[9][v[9]], 1.0))[:3]
                    nr = M[:3, :3] @ np.asarray(b.arr[10][v[10]], float) if 10 in v else np.array([0, 1.0, 0])
                    nr = nr / max(np.linalg.norm(nr), 1e-9)
                    uv = np.asarray(b.arr[13][v[13]], float) if 13 in v else np.zeros(2)
                    cs.append((pos, nr, uv, wts))
                for a_, b_, c_ in idx:
                    out.setdefault(b.shapemat.get(si, 0), []).append([cs[a_], cs[b_], cs[c_]])
    return out

def renormal(tris, crease=CREASE):
    P = np.array([[c[0] for c in t] for t in tris])                 # T,3,3
    fn = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0]); ln = np.linalg.norm(fn, axis=1); ok = ln > 1e-9
    fn[ok] /= ln[ok, None]
    key = lambda p: tuple(np.round(p, 2))
    # pieces: triangles joined by shared corners
    parent = list(range(len(tris)))
    def find(x):
        while parent[x] != x: parent[x] = parent[parent[x]]; x = parent[x]
        return x
    first = {}
    for t in range(len(tris)):
        for c in range(3):
            k = key(P[t, c])
            if k in first: parent[find(t)] = find(first[k])
            else: first[k] = t
    comp = np.array([find(t) for t in range(len(tris))])
    for cid in set(comp):
        sel = comp == cid; cen = P[sel].reshape(-1, 3).mean(0)
        fc = P[sel].mean(1)
        flip = np.einsum('ij,ij->i', fn[sel], fc - cen) < 0
        f = fn[sel]; f[flip] *= -1; fn[sel] = f
    # corner angles for weighting
    def ang(a, b, c):
        u, v = b - a, c - a
        return np.arccos(np.clip(np.einsum('ij,ij->i', u, v) / np.maximum(np.linalg.norm(u, axis=1) * np.linalg.norm(v, axis=1), 1e-12), -1, 1))
    A = np.stack([ang(P[:, 0], P[:, 1], P[:, 2]), ang(P[:, 1], P[:, 2], P[:, 0]), ang(P[:, 2], P[:, 0], P[:, 1])], 1)
    at = {}
    for t in range(len(tris)):
        for c in range(3): at.setdefault((comp[t],) + key(P[t, c]), []).append((t, c))
    cosc = np.cos(np.radians(crease)); new = []
    for t in range(len(tris)):
        tri = []
        for c in range(3):
            acc = np.zeros(3)
            for (u, cc) in at[(comp[t],) + key(P[t, c])]:
                if fn[u] @ fn[t] >= cosc: acc += fn[u] * A[u, cc]
            nrm = acc / max(np.linalg.norm(acc), 1e-9) if np.linalg.norm(acc) > 1e-9 else fn[t]
            p, _, uv, w = tris[t][c]; tri.append((p, nrm, uv, w))
        new.append(tri)
    return new

def rebuild(data, fix_mats, out_path):
    b = BMD(data); b.read_tex() if not getattr(b, 'tex', None) else None
    per = corners(b)
    names = joint_names(b); n = len(names)
    d = b.d; o = b.sec['JNT1']; eo = o + u32(d, o + 0xC)
    entries = [bytes(d[eo + i * 0x40: eo + (i + 1) * 0x40]) for i in range(n)]
    skel = dict(names=names, parent=b.parent, jnt_entries=entries, world_pos=[b.jworld[i][:3, 3] for i in range(n)])
    groups = []
    for mi in sorted(per):
        tris = per[mi]
        if mi in fix_mats: tris = renormal(tris, fix_mats[mi])
        ti = b.mats[mi][0]; img = np.asarray(b.tex[ti], np.uint8)
        alpha = bool((img[..., 3] < 128).any())
        wrap = tuple(getattr(b.tex[ti], 'wrap', (1, 1)))
        groups.append((img, alpha, [[(c[0], c[1], c[2], c[3]) for c in t] for t in tris], wrap))
    return write_skinned(out_path, skel, groups)

if __name__ == '__main__':
    # MAT or MAT:CREASE (cloth: 180, every edge smooth; metal plates keep the default hard edges)
    src, dst = sys.argv[1], sys.argv[2]
    mats = {int(x.split(':')[0]): float(x.split(':')[1]) if ':' in x else CREASE for x in sys.argv[3].split(',')}
    print(rebuild(open(src, 'rb').read(), mats, dst))
