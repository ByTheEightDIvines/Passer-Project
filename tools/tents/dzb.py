"""Minimal DZB (Twilight Princess background collision) writer.

Layout follows the vanilla object collisions (e.g. HTable/ytable.dzb): a three-group hierarchy
(root -> 'A00' -> mesh), one octree node that is a leaf holding block 0, and one block holding every
triangle. Fine for small object collision (a few hundred triangles).
Triangle normals are (B-A)x(C-A), so wind each triangle counter-clockwise seen from the side that
should be solid-facing (the side Link pushes against)."""
import struct

def _pad(b, a=4): return b + b'\0' * ((-len(b)) % a)

def write_dzb(tris, ti=(0x3FFF, 0x50FF, 0xFFFFFF00, 0x0), name='tent'):
    """tris: list of (p0, p1, p2), each p an (x, y, z). Returns the .dzb bytes."""
    verts, vidx, T = [], {}, []
    def vi(p):
        k = tuple(round(float(c), 3) for c in p)
        if k not in vidx: vidx[k] = len(verts); verts.append(k)
        return vidx[k]
    for a, b, c in tris:
        ia, ib, ic = vi(a), vi(b), vi(c)
        if len({ia, ib, ic}) == 3: T.append((ia, ib, ic))
    vtx = b''.join(struct.pack('>3f', *v) for v in verts)
    tri = b''.join(struct.pack('>5H', a, b, c, 0, 2) for a, b, c in T)
    tree = struct.pack('>10H', 0x101, 0xFFFF, 0, *([0xFFFF] * 7))          # one leaf -> block 0
    tis = struct.pack('>4I', *ti)
    blk = struct.pack('>H', 0)
    names = [name.encode() + b'\0', b'A00\0', b'mesh\0']
    off = 52
    o_v = off; off += len(_pad(vtx))
    o_t = off; off += len(_pad(tri))
    o_tree = off; off += len(_pad(tree))
    o_ti = off; off += len(_pad(tis))
    o_b = off; off += len(_pad(blk))
    o_g = off; off += 3 * 0x34
    o_n = []
    for n in names: o_n.append(off); off += len(n)
    def grp(i, parent, nxt, child, tree_idx):
        return struct.pack('>I3f3h2x3f6HI', o_n[i], 1.0, 1.0, 1.0, 0, 0, 0, 0.0, 0.0, 0.0,
                           parent, nxt, child, 0xFFFF if i < 2 else 0xFFFF, 0, tree_idx, 0xFF)
    groups = grp(0, 0xFFFF, 0xFFFF, 1, 0xFFFF) + grp(1, 0, 0xFFFF, 2, 0xFFFF) + grp(2, 1, 0xFFFF, 0xFFFF, 0)
    hdr = struct.pack('>13I', len(verts), o_v, len(T), o_t, 1, o_b, 1, o_tree, 3, o_g, 1, o_ti, 0)
    out = hdr + _pad(vtx) + _pad(tri) + _pad(tree) + _pad(tis) + _pad(blk) + groups + b''.join(names)
    return _pad(out, 32)

def read_dzb(d):
    vn, vo, tn, to = struct.unpack('>4I', d[:16])
    V = [struct.unpack('>3f', d[vo + 12 * i: vo + 12 * i + 12]) for i in range(vn)]
    T = [struct.unpack('>5H', d[to + 10 * i: to + 10 * i + 10]) for i in range(tn)]
    return V, T
