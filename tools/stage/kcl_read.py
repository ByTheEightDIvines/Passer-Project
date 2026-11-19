"""Read a TP KCL back into triangles (A, B, C) in the winding kcl.write_kcl takes (normal = cross(B-A, C-A))."""
import struct, numpy as np

def read_kcl(d):
    o_pos, o_nrm, o_pri, o_blk = struct.unpack('>4I', d[:16])
    P = lambda i: np.array(struct.unpack('>3f', d[o_pos + 12 * i:o_pos + 12 * i + 12]))
    N = lambda i: np.array(struct.unpack('>3f', d[o_nrm + 12 * i:o_nrm + 12 * i + 12]))
    n = (o_blk - o_pri) // 16
    tris = []
    for t in range(1, n):
        if o_pri + 16 * t + 16 > o_blk: break
        h, pi, fn, e1, e2, e3, a = struct.unpack('>f6H', d[o_pri + 16 * t:o_pri + 16 * t + 16])
        A, nn, E1, E2, E3 = P(pi), N(fn), N(e1), N(e2), N(e3)
        ca = np.cross(E1, nn); cb = np.cross(E2, nn)
        B = A + cb * (h / np.dot(cb, E3))
        C = A + ca * (h / np.dot(ca, E3))
        if np.dot(np.cross(B - A, C - A), nn) < 0: B, C = C, B
        tris.append((A, B, C))
    return tris
