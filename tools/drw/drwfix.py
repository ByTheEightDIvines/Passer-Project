
"""Rewrite a BMD's DRW1 into the canonical J3D layout [rigid][env 0..N-1][env 0..N-1] and remap
every shape packet's matrix-index table so each packet keeps referencing the same joint/envelope.

J3D assumes: mEntryNum = DRW count - EVP count, rigid entries come first, and draw-matrix slot
(fullWgt + k) is envelope k. Non-canonical exports overflow mWEvlpImportantMtxIdx in
J3DJointTree::findImportantMtxIndex() and corrupt the joint pointer table."""
import struct, sys


def secs(d):
    o = 32; r = []
    while o < len(d):
        sz = struct.unpack('>I', d[o + 4:o + 8])[0]; r.append([d[o:o + 4], bytearray(d[o:o + sz])]); o += sz
    return r


def fix(d):
    S = secs(d); T = {t: s for t, s in S}
    w = T[b'DRW1']; n = struct.unpack('>H', w[8:10])[0]; fo, io = struct.unpack('>II', w[12:20])
    flags = list(w[fo:fo + n]); idx = list(struct.unpack(f'>{n}H', w[io:io + 2 * n]))
    nevp = struct.unpack('>H', T[b'EVP1'][8:10])[0]
    rigid_joints = []
    for k in range(n):
        if flags[k] == 0 and idx[k] not in rigid_joints:
            rigid_joints.append(idx[k])
    R = len(rigid_joints)
    newflags = [0] * R + [1] * nevp + [1] * nevp
    newidx = rigid_joints + list(range(nevp)) + list(range(nevp))
    assert len(newidx) <= 0xFFFF, 'too many draw matrices for a u16 count'

    def remap(old):
        if flags[old] == 0:
            return rigid_joints.index(idx[old])
        return R + idx[old]

    lut = [remap(k) for k in range(n)]
    # SHP1 packet matrix tables
    s = T[b'SHP1']; ns = struct.unpack('>H', s[8:10])[0]; so = struct.unpack('>8I', s[12:44])
    shp, _, _, _, mtxidx, _, mtxdata, _ = so
    seen = set(); seen_u = set(); changed = 0
    for i in range(ns):
        en = shp + i * 0x28; npk, ao, fmi = struct.unpack('>HHH', s[en + 2:en + 8])
        for k in range(npk):
            m = mtxdata + (fmi + k) * 8; u, mc, ms = struct.unpack('>HHI', s[m:m + 8])
            if u != 0xFFFF and u < n and m not in seen_u:
                seen_u.add(m); struct.pack_into('>H', s, m, lut[u])   # packet's default matrix (DRW index)
            if (ms, mc) in seen: continue          # tables can be shared between packets
            seen.add((ms, mc))
            for j in range(mc):
                p = mtxidx + (ms + j) * 2
                x = struct.unpack('>H', s[p:p + 2])[0]
                if x != 0xFFFF:
                    struct.pack_into('>H', s, p, lut[x]); changed += 1
    # rebuild DRW1
    N = len(newidx)
    body = bytearray(b'DRW1' + b'\0' * 4 + struct.pack('>H', N) + b'\xff\xff' + b'\0' * 8)
    f_off = len(body); body += bytes(newflags); body += b'\0' * ((-len(body)) % 2)
    i_off = len(body); body += struct.pack(f'>{N}H', *newidx)
    body += b'\0' * ((-len(body)) % 32)
    struct.pack_into('>I', body, 4, len(body)); struct.pack_into('>II', body, 12, f_off, i_off)
    for e in S:
        if e[0] == b'DRW1': e[1] = body
    out = bytearray(d[:32]) + b''.join(x for _, x in S)
    struct.pack_into('>I', out, 8, len(out))
    return bytes(out), dict(old=n, new=N, rigid=R, evp=nevp, remapped=changed)


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    out, info = fix(d)
    open(sys.argv[2], 'wb').write(out)
    print(info)
