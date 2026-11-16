"""Write TP room collision (KCL: prisms + a block grid of triangle lists), the layout dBgWKCol reads.
tris: list of (A, B, C) with the face normal cross(B-A, C-A) pointing to the walkable / open side."""
import struct, math, numpy as np

def write_kcl(tris, block_shift=7, attr=0):
    P = []; pidx = {}
    N = []; nidx = {}
    def ip(v):
        k = tuple(np.round(v, 4))
        if k not in pidx: pidx[k] = len(P); P.append(k)
        return pidx[k]
    def inn(v):
        k = tuple(np.round(v, 6))
        if k not in nidx: nidx[k] = len(N); N.append(k)
        return nidx[k]
    unit = lambda v: v / np.linalg.norm(v)
    prisms = []
    T = []
    for A, B, C in tris:
        A, B, C = (np.asarray(x, float) for x in (A, B, C))
        n = np.cross(B - A, C - A)
        if np.linalg.norm(n) < 1e-6: continue
        n = unit(n)
        e1 = unit(np.cross(A - C, n))      # edge C-A (the game rebuilds C from it)
        e2 = unit(np.cross(B - A, n))      # edge A-B (and B from it)
        e3 = unit(np.cross(C - B, n))      # the far edge B-C
        h = float(np.dot(B - A, e3))
        prisms.append((h, ip(A), inn(n), inn(e1), inn(e2), inn(e3), attr))
        T.append((A, B, C))
    allp = np.array([p for t in T for p in t])
    lo = np.floor(allp.min(0)) - 32.0
    ext = allp.max(0) - lo + 32.0
    bw = 1 << block_shift
    def pow2(x): return 1 << max(block_shift, int(math.ceil(math.log2(x))))
    sx, sy, sz = pow2(ext[0]), pow2(ext[1]), pow2(ext[2])
    nx, ny, nz = sx // bw, sy // bw, sz // bw
    xs, xys = int(math.log2(nx)), int(math.log2(nx * ny))
    # triangle lists per cell (a triangle goes in every cell its box, padded a little, touches)
    lists = {}
    for ti, (A, B, C) in enumerate(T):
        mn = np.minimum(np.minimum(A, B), C) - lo - 16.0
        mx = np.maximum(np.maximum(A, B), C) - lo + 16.0
        for cz in range(max(0, int(mn[2]) // bw), min(nz - 1, int(mx[2]) // bw) + 1):
            for cy in range(max(0, int(mn[1]) // bw), min(ny - 1, int(mx[1]) // bw) + 1):
                for cx in range(max(0, int(mn[0]) // bw), min(nx - 1, int(mx[0]) // bw) + 1):
                    lists.setdefault((cz << xys) | (cy << xs) | cx, []).append(ti + 1)
    ncell = nx * ny * nz
    # block data: the cell table (leaf offsets, high bit set), then the lists; each list is read from
    # one slot after where its offset points, and ends with 0
    table = bytearray(4 * ncell)
    data = bytearray(b'\0\0')           # empty list: a 0 right after the slot pointed at
    empty = 4 * ncell
    data_off = {}
    for c in range(ncell):
        lst = lists.get(c)
        if not lst: off = empty
        else:
            key = tuple(lst)
            if key not in data_off:
                data_off[key] = 4 * ncell + len(data)
                data += b'\0\0' + b''.join(struct.pack('>H', t) for t in lst) + b'\0\0'
            off = data_off[key]
        struct.pack_into('>I', table, 4 * c, 0x80000000 | (off - 2 + 2))
    # (the offset points at the slot before the first index; lists above start with a spare slot)
    block = bytes(table) + bytes(data)
    pos = b''.join(struct.pack('>3f', *p) for p in P)
    nrm = b''.join(struct.pack('>3f', *n) for n in N)
    pri = struct.pack('>f6H', 0, 0, 0, 0, 0, 0, 0) + b''.join(struct.pack('>f6H', *p) for p in prisms)
    o_pos = 0x38; o_nrm = o_pos + len(pos); o_pri = o_nrm + len(nrm); o_blk = o_pri + len(pri)
    o_blk = (o_blk + 3) & ~3
    hdr = struct.pack('>4If3f6I', o_pos, o_nrm, o_pri, o_blk, 1.0, *lo,
                      (~(sx - 1)) & 0xFFFFFFFF, (~(sy - 1)) & 0xFFFFFFFF, (~(sz - 1)) & 0xFFFFFFFF, block_shift, xs, xys)
    out = hdr + pos + nrm + pri
    out += b'\0' * (o_blk - len(out)) + block
    return out, len(prisms)

def ground_at(kcl, x, y, z):
    """The game's GroundCross, for checking: highest floor at or below (x, y, z), or None."""
    u32 = lambda o: struct.unpack('>I', kcl[o:o + 4])[0]
    o_pos, o_nrm, o_pri, o_blk = struct.unpack('>4I', kcl[:16])
    lo = np.array(struct.unpack('>3f', kcl[0x14:0x20]))
    xm, ym, zm, sh, xs, xys = struct.unpack('>6I', kcl[0x20:0x38])
    p = np.array([x, y, z]) - lo
    if p[0] < 0 or p[2] < 0 or p[0] > (~xm & 0xFFFFFFFF) or p[2] > (~zm & 0xFFFFFFFF): return None
    px, py, pz = int(p[0]), int(min(max(p[1], 0), ~ym & 0xFFFFFFFF)), int(p[2])
    best = None
    while True:
        block = o_blk; s = sh
        idx = 4 * (((pz >> s) << xys) | ((py >> s) << xs) | (px >> s))
        v = u32(block + idx)
        while not (v & 0x80000000):
            block += v; s -= 1
            idx = ((((pz >> s) & 1) << 2) | (((py >> s) & 1) << 1) | ((px >> s) & 1)) << 2
            v = u32(block + idx)
        q = block + (v & 0x7FFFFFFF)
        while True:
            q += 2; t = struct.unpack('>H', kcl[q:q + 2])[0]
            if t == 0: break
            h, pi, fn, e1, e2, e3, a = struct.unpack('>f6H', kcl[o_pri + 16 * t:o_pri + 16 * t + 16])
            V = lambda i: np.array(struct.unpack('>3f', kcl[o_nrm + 12 * i:o_nrm + 12 * i + 12]))
            n = V(fn)
            if n[1] < 0.014: continue
            A = np.array(struct.unpack('>3f', kcl[o_pos + 12 * pi:o_pos + 12 * pi + 12]))
            d = np.array([x - A[0], 0, z - A[2]]); d[1] = -(d[0] * n[0] + d[2] * n[2]) / n[1]
            if d @ V(e1) > 0.0075 or d @ V(e2) > 0.0075: continue
            f = d @ V(e3)
            if f > 0.0075 + h or f < -0.0075: continue
            gy = A[1] + d[1]
            if gy <= y + 0.01 and (best is None or gy > best): best = gy
        if best is not None or py == 0: return best
        py = max(0, py - (1 << sh))
