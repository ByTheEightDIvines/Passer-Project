import struct
import numpy as np
def sections(d):
    n = struct.unpack('>I', d[12:16])[0]; o = 0x20; out = {}
    for _ in range(n):
        tag = d[o:o+4]; sz = struct.unpack('>I', d[o+4:o+8])[0]; out[tag] = (o, sz); o += sz
    return out
def read_vtx(d, o):
    fmt_off = struct.unpack('>I', d[o+8:o+12])[0]
    offs = struct.unpack('>13I', d[o+12:o+12+52])
    fmts = {}; p = o + fmt_off
    while True:
        attr, cnt, ctype, frac = struct.unpack('>IIIB', d[p:p+13]); p += 16
        if attr == 0xFF: break
        fmts[attr] = (cnt, ctype, frac)
    # array index by attr: 9 pos ->0, 10 nrm ->1, 11 col0 ->3, 13 tex0 ->5 ...
    slot = {9: 0, 10: 1, 11: 3, 12: 4, 13: 5}
    starts = sorted(set(x for x in offs if x))
    def arr(attr, ncomp):
        s = offs[slot[attr]]
        if not s: return None
        nxt = [x for x in starts if x > s]; end = nxt[0] if nxt else struct.unpack('>I', d[o+4:o+8])[0]
        cnt, ctype, frac = fmts[attr]
        if attr == 11:
            return np.frombuffer(d[o+s:o+end], np.uint8)
        size = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4}[ctype]; dt = {0: '>u1', 1: '>i1', 2: '>u2', 3: '>i2', 4: '>f4'}[ctype]
        a = np.frombuffer(d[o+s:o+end], dt).astype(np.float64)
        if ctype != 4: a = a / (1 << frac)
        k = ncomp
        return a[:len(a)//k*k].reshape(-1, k)
    pos = arr(9, 3); nrm = arr(10, 3)
    return pos, nrm, fmts
def read_shapes(d, o):
    cnt = struct.unpack('>H', d[o+8:o+10])[0]
    sh_off, idx_off, _, desc_off, mtxidx_off, dl_off, mtxdata_off, pkt_off = struct.unpack('>8I', d[o+12:o+44])
    tris = []
    for i in range(cnt):
        s = o + sh_off + i * 0x28
        mtxtype, _, npkt, attroff, firstmtx, firstpkt = struct.unpack('>BBHHHH', d[s:s+10])
        # vertex descriptor
        p = o + desc_off + attroff; desc = []
        while True:
            a, t = struct.unpack('>II', d[p:p+8]); p += 8
            if a == 0xFF: break
            desc.append((a, t))
        for k in range(npkt):
            pk = o + pkt_off + (firstpkt + k) * 8
            size, off = struct.unpack('>II', d[pk:pk+8])
            q = o + dl_off + off; end = q + size
            while q < end:
                op = d[q]
                if op == 0: break
                n = struct.unpack('>H', d[q+1:q+3])[0]; q += 3
                verts = []
                for _ in range(n):
                    v = {}
                    for a, t in desc:
                        if t == 1: val = d[q]; q += 1
                        elif t == 2: val = d[q]; q += 1
                        elif t == 3: val = struct.unpack('>H', d[q:q+2])[0]; q += 2
                        else: val = 0
                        v[a] = val
                    verts.append(v)
                if op == 0x90:
                    for j in range(0, n - 2, 3): tris.append((verts[j], verts[j+1], verts[j+2]))
                elif op == 0x98:
                    for j in range(n - 2):
                        tris.append((verts[j], verts[j+1], verts[j+2]) if j % 2 == 0 else (verts[j+1], verts[j], verts[j+2]))
                elif op == 0xA0:
                    for j in range(1, n - 1): tris.append((verts[0], verts[j], verts[j+1]))
    return tris
def load(path):
    d = open(path, 'rb').read(); s = sections(d)
    pos, nrm, fmts = read_vtx(d, s[b'VTX1'][0])
    tris = read_shapes(d, s[b'SHP1'][0])
    return pos, nrm, tris
