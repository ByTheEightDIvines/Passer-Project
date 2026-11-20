"""Edit a rigid single-shape BMD (pos/nrm/tex0, float, index16): append positions and normals and rewrite
each packet's display list as one triangle list. Everything else is copied byte for byte."""
import struct, numpy as np
def align(n, a=0x20): return (n + a - 1) & ~(a - 1)
def secs(d):
    n = struct.unpack('>I', d[12:16])[0]; o = 0x20; out = []
    for _ in range(n):
        tag = d[o:o+4]; sz = struct.unpack('>I', d[o+4:o+8])[0]; out.append([tag, d[o:o+sz]]); o += sz
    return out
def packets(shp):
    cnt = struct.unpack('>H', shp[8:10])[0]; assert cnt == 1
    sh_off, idx_off, unk, desc_off, mtxidx_off, dl_off, mtxdata_off, pkt_off = struct.unpack('>8I', shp[12:44])
    mtxtype, _, npkt, attroff, firstmtx, firstpkt = struct.unpack('>BBHHHH', shp[sh_off:sh_off+10])
    p = desc_off + attroff; desc = []
    while True:
        a, t = struct.unpack('>II', shp[p:p+8]); p += 8
        if a == 0xFF: break
        desc.append((a, t))
    assert all(t == 3 for a, t in desc), desc
    out = []
    for k in range(npkt):
        size, off = struct.unpack('>II', shp[pkt_off + (firstpkt + k) * 8: pkt_off + (firstpkt + k) * 8 + 8])
        q = dl_off + off; end = q + size; tris = []
        while q < end:
            op = shp[q]
            if op == 0: break
            n = struct.unpack('>H', shp[q+1:q+3])[0]; q += 3; vs = []
            for _ in range(n):
                v = {}
                for a, t in desc: v[a] = struct.unpack('>H', shp[q:q+2])[0]; q += 2
                vs.append(v)
            if op == 0x90: tris += [(vs[j], vs[j+1], vs[j+2]) for j in range(0, n - 2, 3)]
            elif op == 0x98: tris += [((vs[j], vs[j+1], vs[j+2]) if j % 2 == 0 else (vs[j+1], vs[j], vs[j+2])) for j in range(n - 2)]
            elif op == 0xA0: tris += [(vs[0], vs[j], vs[j+1]) for j in range(1, n - 1)]
            else: raise ValueError('op %x' % op)
        out.append(tris)
    return desc, out
def arrays(vtx):
    offs = list(struct.unpack('>13I', vtx[12:64]))
    used = sorted((o, i) for i, o in enumerate(offs) if o)
    blocks = {}
    for k, (o, i) in enumerate(used):
        end = used[k+1][0] if k + 1 < len(used) else len(vtx)
        blocks[i] = vtx[o:end]
    return offs, blocks, used[0][0]
def rebuild(d, add_pos, add_nrm, new_packets):
    S = secs(d); T = {s[0]: s for s in S}
    vtx = T[b'VTX1'][1]; shp = T[b'SHP1'][1]
    desc, old_packets = packets(shp)
    offs, blocks, first = arrays(vtx)
    # true counts (the arrays end in padding): the largest index any packet uses, plus one
    npos = max(v[9] for p in old_packets for t in p for v in t) + 1
    nnrm = max(v[10] for p in old_packets for t in p for v in t) + 1
    posb = blocks[0][:npos * 12] + b''.join(struct.pack('>3f', *p) for p in add_pos)
    nrmb = blocks[1][:nnrm * 12] + b''.join(struct.pack('>3f', *n) for n in add_nrm)
    nb = bytearray(vtx[:first]); newoffs = list(offs)
    for i in sorted(blocks):
        data = posb if i == 0 else nrmb if i == 1 else blocks[i]
        nb += b'\0' * (align(len(nb)) - len(nb)); newoffs[i] = len(nb); nb += data
    nb += b'\0' * (align(len(nb)) - len(nb))
    struct.pack_into('>13I', nb, 12, *newoffs); struct.pack_into('>I', nb, 4, len(nb))
    T[b'VTX1'][1] = bytes(nb)
    # SHP1: the display lists, one triangle list per packet
    sh_off, idx_off, unk, desc_off, mtxidx_off, dl_off, mtxdata_off, pkt_off = struct.unpack('>8I', shp[12:44])
    mtxtype, _, npkt, attroff, firstmtx, firstpkt = struct.unpack('>BBHHHH', shp[sh_off:sh_off+10])
    assert len(new_packets) == npkt
    dls = bytearray(); locs = []
    for tris in new_packets:
        start = len(dls); n = len(tris) * 3; assert n < 0x10000
        dls += bytes([0x90]) + struct.pack('>H', n)
        for t in tris:
            for v in t:
                for a, _ in desc: dls += struct.pack('>H', v[a])
        dls += b'\0' * (align(len(dls)) - len(dls))
        locs.append((len(dls) - start, start))
    # sub-blocks of SHP1 in file order, the DL block replaced, the packet table rewritten
    names = ['sh', 'idx', 'unk', 'desc', 'mtxidx', 'dl', 'mtxdata', 'pkt']
    vals = [sh_off, idx_off, unk, desc_off, mtxidx_off, dl_off, mtxdata_off, pkt_off]
    order = sorted((v, k) for k, v in enumerate(vals) if v)
    parts = {}
    for j, (v, k) in enumerate(order):
        end = order[j+1][0] if j + 1 < len(order) else len(shp)
        parts[k] = bytearray(shp[v:end])
    parts[5] = dls
    pk = parts[7]
    for k, (size, off) in enumerate(locs): struct.pack_into('>II', pk, (firstpkt + k) * 8, size, off)
    ns = bytearray(shp[:order[0][0]]); newvals = list(vals)
    for v, k in order:
        if k == 5: ns += b'\0' * (align(len(ns)) - len(ns))
        newvals[k] = len(ns); ns += parts[k]
    ns += b'\0' * (align(len(ns)) - len(ns))
    struct.pack_into('>8I', ns, 12, *newvals); struct.pack_into('>I', ns, 4, len(ns))
    T[b'SHP1'][1] = bytes(ns)
    # INF1's vertex count: every vertex the display lists send
    inf = bytearray(T[b'INF1'][1]); struct.pack_into('>I', inf, 0x10, sum(len(t) for t in new_packets) * 3); T[b'INF1'][1] = bytes(inf)
    out = bytearray(d[:0x20])
    for tag, data in S: out += data
    struct.pack_into('>I', out, 8, len(out))
    return bytes(out), npos, nnrm
