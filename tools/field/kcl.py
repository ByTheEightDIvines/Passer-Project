"""KCL collision reader (TP room.kcl) + PLC polygon attributes."""
import struct
import numpy as np


def read_kcl(d):
    pos_o, nrm_o, prism_o, block_o = struct.unpack_from('>4I', d, 0)
    thick = struct.unpack_from('>f', d, 0x10)[0]
    # arrays: positions run from pos_o to nrm_o, normals from nrm_o to prism_o+0x10
    npos = (nrm_o - pos_o) // 12
    P = np.frombuffer(d[pos_o:pos_o + npos * 12], '>f4').astype(float).reshape(-1, 3)
    first_prism = prism_o + 0x10
    nnrm = (first_prism - nrm_o) // 12
    N = np.frombuffer(d[nrm_o:nrm_o + nnrm * 12], '>f4').astype(float).reshape(-1, 3)
    nprism = (block_o - first_prism) // 0x10
    pr = np.frombuffer(d[first_prism:first_prism + nprism * 0x10],
                       dtype=[('h', '>f4'), ('p', '>u2'), ('fn', '>u2'), ('e1', '>u2'), ('e2', '>u2'),
                              ('e3', '>u2'), ('attr', '>u2')])
    h = pr['h'].astype(float)
    A = P[pr['p']]
    fn = N[pr['fn']]; e1 = N[pr['e1']]; e2 = N[pr['e2']]; e3 = N[pr['e3']]
    ca = np.cross(e1, fn); cb = np.cross(e2, fn)
    with np.errstate(divide='ignore', invalid='ignore'):
        B = A + cb * (h / (cb * e3).sum(1))[:, None]
        C = A + ca * (h / (ca * e3).sum(1))[:, None]
    tris = np.stack([A, B, C], 1)
    ok = np.isfinite(tris).all((1, 2))
    return tris[ok], fn[ok], pr['attr'][ok].astype(int)


def read_plc(d):
    assert d[:4] == b'SPLC', d[:4]
    sz, n = struct.unpack_from('>HH', d, 4)
    out = []
    for i in range(n):
        c = struct.unpack_from('>5I', d, 8 + i * sz)
        g = lambda v, s, b: (v >> s) & ((1 << b) - 1)
        out.append(dict(exit=g(c[0], 0, 6), polycol=g(c[0], 6, 8), obj_through=g(c[0], 14, 1),
                        link_through=g(c[0], 16, 1), horse_no=g(c[0], 21, 1), spl=g(c[0], 24, 4),
                        wall=g(c[1], 8, 4), att0=g(c[1], 12, 4), att1=g(c[1], 16, 3), ground=g(c[1], 19, 5),
                        wtr=g(c[4], 8, 1), snd=g(c[4], 11, 8), raw=c))
    return out
