"""A skinned J3D model (BMD) writer: a character on a game skeleton, so the game's own animations move it.

write_skinned(path, skel, groups):
  skel   = dict(names, parent, jnt_entries (raw 0x40-byte JNT1 entries from a game body, for the flags,
           scale and rotation of each joint), world_pos (each joint's bind position in model space))
  groups = list of (rgba HxWx4 uint8, alpha_cutout bool, tris, wrap=(1,1));
           tris = list of corners-triples, corner = (pos(3) model space, nrm(3), uv(2), {joint_index: weight})
Layout (as the game's own bodies): vertices on one joint are stored in that joint's space (DRW rigid),
blended ones in model space (DRW envelope, EVP1 with every joint's inverse bind matrix). One shape per
material, packets of at most 10 matrices, PNMTXIDX per vertex. Materials and textures from the template
the rigid writers use (multibmd.py), so they light like the rest of our gear.
"""
import struct, sys, math
import numpy as np
sys.path[:0] = ['/home/claude/w/anim']
from bmdwrite import pad, strtab, sections, template, encode_cmpr, encode_cmpr_rgba

MAX_INFL = 4


def joint_local(entry):
    sc = struct.unpack_from('>3f', entry, 4)
    r = [x * math.pi / 32768 for x in struct.unpack_from('>3h', entry, 0x10)]
    cx, sx = math.cos(r[0]), math.sin(r[0]); cy, sy = math.cos(r[1]), math.sin(r[1]); cz, sz = math.cos(r[2]), math.sin(r[2])
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]]); Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx @ np.diag(sc)


def bind_worlds(skel):
    """Each joint's bind matrix: the template's rotations, our positions."""
    n = len(skel['names']); W = [None] * n; L = [None] * n
    for i in range(n):
        R = joint_local(skel['jnt_entries'][i]); p = skel['parent'][i]
        Rw = R if p < 0 else W[p][:3, :3] @ R
        M = np.eye(4); M[:3, :3] = Rw; M[:3, 3] = skel['world_pos'][i]; W[i] = M
        if p < 0:
            t = np.array(skel['world_pos'][i], float)
        else:
            t = np.linalg.inv(W[p][:3, :3]) @ (np.array(skel['world_pos'][i]) - W[p][:3, 3])
        L[i] = t
    return W, L


def write_skinned(path, skel, groups, cull=0, maxtex=512):
    T = sections(template())
    W, Ltrans = bind_worlds(skel)
    nj = len(skel['names'])
    # ---- draw matrices: one per joint used rigidly, one per distinct blend ----
    drw = []; drw_key = {}; envs = []; env_key = {}
    def drw_of(w):
        items = sorted(((j, x) for j, x in w.items() if x > 1e-3), key=lambda t: -t[1])[:MAX_INFL]
        s = sum(x for _, x in items)
        items = [(j, x / s) for j, x in items]
        # weights quantised so equal blends share one envelope
        items = sorted(((j, round(x * 20) / 20) for j, x in items if round(x * 20) > 0), key=lambda t: t[0])
        if len(items) == 1:
            key = ('r', items[0][0])
        else:
            s = sum(x for _, x in items); items = tuple((j, x / s) for j, x in items)
            ek = tuple((j, round(x, 4)) for j, x in items)
            if ek not in env_key: env_key[ek] = len(envs); envs.append(items)
            key = ('w', env_key[ek])
        if key not in drw_key: drw_key[key] = len(drw); drw.append(key)
        return drw_key[key]
    PU, NU, UU = [], [], []; keys = [{}, {}, {}]
    def uq(store, k, v, dec):
        kk = tuple(np.round(v, dec))
        if kk not in keys[k]: keys[k][kk] = len(store); store.append(kk)
        return keys[k][kk]
    shapes = []; allp = []
    for grp in groups:
        tris = grp[2]; corners = []
        for tri in tris:
            for p, n, uv, w in tri:
                d = drw_of(w); kind, idx = drw[d]
                p = np.asarray(p, float); n = np.asarray(n, float); n = n / max(np.linalg.norm(n), 1e-9)
                if kind == 'r':   # in its joint's own space
                    Mi = np.linalg.inv(W[idx]); pl = (Mi @ np.append(p, 1))[:3]; nl = Mi[:3, :3] @ n
                    nl = nl / max(np.linalg.norm(nl), 1e-9)
                else:
                    pl, nl = p, n
                corners.append((d, uq(PU, 0, pl, 3), uq(NU, 1, nl, 3), uq(UU, 2, uv, 4)))
                allp.append(p)
        shapes.append(corners)
    A = np.array(allp); mn, mx = A.min(0), A.max(0); rad = float(np.linalg.norm(A, axis=1).max())
    # ---- VTX1 ----
    fmt = b''.join(struct.pack('>IIIBxxx', a, c, t, 0) for a, c, t in ((9, 1, 4), (10, 0, 4), (13, 1, 4), (0xFF, 1, 0)))
    body = bytearray(b'VTX1' + b'\0' * 4 + struct.pack('>I', 0x40) + b'\0' * 52) + fmt
    body = bytearray(pad(bytes(body))); offs = [0] * 13
    def add(arr):
        nonlocal body
        o = len(body); a = np.asarray(arr, float); body += struct.pack('>%df' % a.size, *a.ravel()); body = bytearray(pad(bytes(body))); return o
    offs[0] = add(PU); offs[1] = add(NU); offs[5] = add(UU)
    struct.pack_into('>13I', body, 0xC, *offs); struct.pack_into('>I', body, 4, len(body)); VTX1 = bytes(body)
    # ---- EVP1 ----
    ev = bytearray(b'EVP1' + b'\0' * 4 + struct.pack('>HH', len(envs), 0xFFFF) + b'\0' * 16)
    o_cnt = len(ev); ev += bytes(len(e) for e in envs); ev = bytearray(pad(bytes(ev), 4))
    o_idx = len(ev); ev += b''.join(struct.pack('>H', j) for e in envs for j, _ in e); ev = bytearray(pad(bytes(ev), 4))
    o_w = len(ev); ev += b''.join(struct.pack('>f', x) for e in envs for _, x in e); ev = bytearray(pad(bytes(ev), 4))
    o_m = len(ev)
    for i in range(nj):
        ev += struct.pack('>12f', *np.linalg.inv(W[i])[:3, :].ravel())
    ev = bytearray(pad(bytes(ev)))
    struct.pack_into('>4I', ev, 0xC, o_cnt, o_idx, o_w, o_m) if envs else struct.pack_into('>4I', ev, 0xC, 0, 0, 0, 0)
    struct.pack_into('>I', ev, 4, len(ev)); EVP1 = bytes(ev)
    # ---- DRW1 ----
    dr = bytearray(b'DRW1' + b'\0' * 4 + struct.pack('>HH', len(drw), 0xFFFF) + b'\0' * 8)
    o_f = len(dr); dr += bytes(1 if k == 'w' else 0 for k, _ in drw); dr = bytearray(pad(bytes(dr), 2))
    o_d = len(dr); dr += b''.join(struct.pack('>H', i) for _, i in drw); dr = bytearray(pad(bytes(dr)))
    struct.pack_into('>2I', dr, 0xC, o_f, o_d); struct.pack_into('>I', dr, 4, len(dr)); DRW1 = bytes(dr)
    # ---- SHP1: per material, packets of <= 10 draw matrices ----
    sh_e = b''; prims = b''; plocs = b''; mdat = b''; mtab = []; npk_total = 0
    for si, corners in enumerate(shapes):
        P = np.array([allp[0]]) if not corners else None
        tris = [corners[i:i + 3] for i in range(0, len(corners), 3)]
        packets = []; cur = []; curset = []
        for t in tris:
            need = [c[0] for c in t if c[0] not in curset]
            need = list(dict.fromkeys(need))
            if len(curset) + len(need) > 10:
                packets.append((curset, cur)); cur = []; curset = []
                need = list(dict.fromkeys(c[0] for c in t))
            curset = curset + need; cur.append(t)
        if cur: packets.append((curset, cur))
        sp = np.array([allp[0]])
        first_pk = npk_total
        for mats, ts in packets:
            dl = bytearray(struct.pack('>BH', 0x90, 3 * len(ts)))
            for t in ts:
                for d, pi, ni, ui in t:
                    dl += struct.pack('>BHHH', mats.index(d) * 3, pi, ni, ui)
            dl = bytes(pad(bytes(dl)))
            mdat += struct.pack('>HHI', mats[0], len(mats), len(mtab)); mtab += mats
            plocs += struct.pack('>II', len(dl), len(prims)); prims += dl; npk_total += 1
        sh_e += struct.pack('>BxHHHHH', 3, len(packets), 0, first_pk, first_pk, 0xFFFF) + struct.pack('>7f', rad, *mn, *mx)
    sh = bytearray(b'SHP1' + b'\0' * 4 + struct.pack('>HH', len(shapes), 0xFFFF) + b'\0' * 32)
    o1 = len(sh); sh += sh_e; sh = bytearray(pad(bytes(sh), 4))
    o2 = len(sh); sh += b''.join(struct.pack('>H', i) for i in range(len(shapes))); sh = bytearray(pad(bytes(sh), 4))
    o3 = len(sh); sh += struct.pack('>10I', 0, 1, 9, 3, 10, 3, 13, 3, 0xFF, 0); sh = bytearray(pad(bytes(sh), 4))
    o4 = len(sh); sh += b''.join(struct.pack('>H', i) for i in mtab); sh = bytearray(pad(bytes(sh), 32))
    o5 = len(sh); sh += prims
    o6 = len(sh); sh += mdat
    o7 = len(sh); sh += plocs; sh = bytearray(pad(bytes(sh)))
    struct.pack_into('>8I', sh, 0xC, o1, o2, 0, o3, o4, o5, o6, o7); struct.pack_into('>I', sh, 4, len(sh)); SHP1 = bytes(sh)
    # ---- INF1: the joint tree, the materials and shapes under the root ----
    kids = {i: [k for k in range(nj) if skel['parent'][k] == i] for i in range(nj)}
    h = []
    def walk(j, top):
        h.append((0x10, j))
        if kids[j] or top:
            h.append((1, 0))
            for k in kids[j]: walk(k, False)
            if top:
                for i in range(len(shapes)): h.extend([(0x11, i), (1, 0), (0x12, i), (2, 0)])
            h.append((2, 0))
    walk(0, True); h.append((0, 0))
    inf = bytearray(b'INF1' + b'\0' * 4 + struct.pack('>HHIII', 0, 0xFFFF, npk_total, len(PU), 0x18))
    inf += b''.join(struct.pack('>HH', a, b) for a, b in h); inf = bytearray(pad(bytes(inf))); struct.pack_into('>I', inf, 4, len(inf)); INF1 = bytes(inf)
    # ---- JNT1: the template's joints, our positions and bounds ----
    jn = bytearray(b'JNT1' + b'\0' * 4 + struct.pack('>HH', nj, 0xFFFF) + b'\0' * 12)
    o_e = len(jn)
    for i in range(nj):
        e = bytearray(skel['jnt_entries'][i])
        struct.pack_into('>3f', e, 0x18, *Ltrans[i])
        struct.pack_into('>7f', e, 0x24, rad, *mn, *mx)
        jn += e
    jn = bytearray(pad(bytes(jn), 4))
    o_r = len(jn); jn += b''.join(struct.pack('>H', i) for i in range(nj)); jn = bytearray(pad(bytes(jn), 4))
    o_n = len(jn); jn += strtab(skel['names']); jn = bytearray(pad(bytes(jn)))
    struct.pack_into('>3I', jn, 0xC, o_e, o_r, o_n); struct.pack_into('>I', jn, 4, len(jn)); JNT1 = bytes(jn)
    # ---- MAT3 / TEX1 (as multibmd.py: one texture per material, alpha test where asked) ----
    m = bytes(T[b'MAT3']); mo = [struct.unpack('>I', m[0xC + 4 * i:0x10 + 4 * i])[0] for i in range(30)]
    order = sorted(set(k for k in range(30) if mo[k] != 0), key=lambda k: mo[k])
    chunk = {k: m[mo[k]:(mo[order[order.index(k) + 1]] if order.index(k) + 1 < len(order) else len(m))] for k in order}
    base = bytearray(chunk[0][:0x14C]); nm = len(groups)
    acmp = bytearray(chunk[24]); a_idx = len(acmp) // 8; acmp += bytes([6, 128, 0, 7, 255, 0xff, 0xff, 0xff])
    zc = bytearray(chunk[27]); z_idx = len(zc); zc += b'\0'
    ents = b''
    for i, grp in enumerate(groups):
        e = bytearray(base); struct.pack_into('>H', e, 0x84, i); e[1] = 0
        if grp[1]: struct.pack_into('>H', e, 0x146, a_idx); e[5] = z_idx
        ents += bytes(e)
    chunk[0] = ents; chunk[1] = b''.join(struct.pack('>H', i) for i in range(nm))
    chunk[2] = strtab([f'm{i}' for i in range(nm)]); chunk[15] = b''.join(struct.pack('>H', i) for i in range(nm))
    cl = bytearray(chunk[4]); struct.pack_into('>I', cl, 0, cull); chunk[4] = bytes(cl)
    chunk[24] = bytes(acmp); chunk[27] = bytes(zc)
    mm = bytearray(m[:mo[order[0]]]); struct.pack_into('>H', mm, 8, nm); newoff = [0] * 30
    for k in order:
        mm = bytearray(pad(bytes(mm), 4)); newoff[k] = len(mm); mm += chunk[k]
    mm = bytearray(pad(bytes(mm))); struct.pack_into('>30I', mm, 0xC, *newoff); struct.pack_into('>I', mm, 4, len(mm)); MAT3 = bytes(mm)
    from PIL import Image
    hdr0 = bytes(T[b'TEX1'][struct.unpack('>I', T[b'TEX1'][0xC:0x10])[0]:][:0x20])
    hdrs = b''; datas = []; cur = 0x20 * nm
    for i, grp in enumerate(groups):
        im = np.asarray(grp[0], np.uint8); wrap = grp[3] if len(grp) > 3 else (1, 1)
        h_, w_ = im.shape[:2]
        nh = max(8, min(maxtex, 1 << max(3, round(math.log2(h_))))); nw = max(8, min(maxtex, 1 << max(3, round(math.log2(w_)))))
        if (nh, nw) != (h_, w_): im = np.array(Image.fromarray(im).resize((nw, nh), Image.LANCZOS)); h_, w_ = nh, nw
        data = encode_cmpr_rgba(im) if grp[1] else encode_cmpr(im[..., :3])
        hh = bytearray(hdr0); hh[0] = 0x0E; struct.pack_into('>HH', hh, 2, w_, h_); hh[6] = wrap[0]; hh[7] = wrap[1]
        struct.pack_into('>I', hh, 0x1C, cur - 0x20 * i); hdrs += bytes(hh); datas.append(data); cur += len(data)
    tex = bytearray(b'TEX1' + b'\0' * 4 + struct.pack('>HH', nm, 0xFFFF) + struct.pack('>II', 0x20, 0) + b'\0' * 12)
    tex += hdrs + b''.join(datas); o_tn = len(tex); tex += strtab([f't{i}' for i in range(nm)]); tex = bytearray(pad(bytes(tex)))
    struct.pack_into('>I', tex, 0x10, o_tn); struct.pack_into('>I', tex, 4, len(tex)); TEX1 = bytes(tex)
    out = bytearray(template()[:32])
    for sec in (INF1, VTX1, EVP1, DRW1, JNT1, SHP1, MAT3, TEX1): out += sec
    struct.pack_into('>I', out, 8, len(out)); struct.pack_into('>I', out, 12, 8)
    open(path, 'wb').write(out)
    return dict(bytes=len(out), verts=len(PU), drw=len(drw), envs=len(envs), packets=npk_total)
