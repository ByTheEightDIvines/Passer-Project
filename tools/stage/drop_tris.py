"""Drop triangles from a static (single-packet, unskinned) J3D model by rewriting only its SHP1 display
lists: every other section (vertices, materials, textures) stays byte for byte, so no texture is re-encoded.
drop(shape_index, positions 3x3 world) -> True to drop. Used by patch_shop_room.py."""
import struct, numpy as np

def _sections(d):
    out = {}; o = 32
    for _ in range(struct.unpack('>I', d[12:16])[0]):
        tag, size = d[o:o + 4], struct.unpack('>I', d[o + 4:o + 8])[0]; out[tag] = (o, size); o += size
    return out

def _pad(b, n): return b + b'\0' * (-len(b) % n)

def drop_tris(d, drop):
    S = _sections(d)
    vo, vs = S[b'VTX1']; v = d[vo:vo + vs]
    fo = struct.unpack_from('>I', v, 8)[0]; voffs = struct.unpack_from('>13I', v, 0xC)
    fmts = {}; p = fo
    while True:
        a, ct, dt, fr = struct.unpack_from('>IIIB', v, p); p += 16
        if a == 0xFF: break
        fmts[a] = (ct, dt, fr)
    assert fmts[9] == (1, 4, 0), fmts[9]   # positions: xyz floats
    used = sorted(o for o in voffs if o)
    st = voffs[0]; nxt = [o for o in used if o > st]
    pos = np.frombuffer(v[st:nxt[0] if nxt else len(v)], '>f4'); pos = pos[:len(pos) // 3 * 3].reshape(-1, 3)
    so_, ss = S[b'SHP1']; s = d[so_:so_ + ss]
    nsh = struct.unpack_from('>H', s, 8)[0]; so = list(struct.unpack_from('>8I', s, 0xC))
    npk_total = (len(s) - so[7]) // 8
    locs = [struct.unpack_from('>II', s, so[7] + 8 * k) for k in range(npk_total)]
    new_dl = {}; dropped = 0
    for i in range(nsh):
        e = s[so[0] + 0x28 * i:so[0] + 0x28 * (i + 1)]
        mtype, npk, attr_off, _, first = e[0], *struct.unpack_from('>HHHH', e, 2)
        attrs = []; o = so[3] + attr_off
        while True:
            at, ty = struct.unpack_from('>II', s, o); o += 8
            if at == 0xFF: break
            attrs.append((at, ty))
        assert npk == 1 and mtype == 0 and all(ty in (1, 3) for _, ty in attrs) and attrs[0][0] != 0, (i, attrs)
        fmt = '>' + ''.join('B' if ty == 1 else 'H' for _, ty in attrs); vsz = struct.calcsize(fmt)
        ipos = [a for a, _ in attrs].index(9)
        size, off = locs[first]; q = so[5] + off; end = q + size; tris = []
        while q < end and s[q] != 0:
            op = s[q]; cnt = struct.unpack_from('>H', s, q + 1)[0]; q += 3; vs_ = []
            for _ in range(cnt): vs_.append(struct.unpack_from(fmt, s, q)); q += vsz
            if op == 0x90: tris += [(vs_[k], vs_[k + 1], vs_[k + 2]) for k in range(0, cnt - 2, 3)]
            elif op == 0x98: tris += [((vs_[k], vs_[k + 1], vs_[k + 2]) if k % 2 == 0 else (vs_[k + 1], vs_[k], vs_[k + 2])) for k in range(cnt - 2)]
            elif op == 0xA0: tris += [(vs_[0], vs_[k], vs_[k + 1]) for k in range(1, cnt - 1)]
            else: raise SystemExit('primitive %x' % op)
        keep = [t for t in tris if not drop(i, np.array([pos[vx[ipos]] for vx in t]))]
        if len(keep) == len(tris): continue
        dropped += len(tris) - len(keep)
        if not keep: keep = [(tris[0][0],) * 3]   # a whole shape gone: one zero-area triangle keeps it valid, draws nothing
        dl = bytearray()
        for k in range(0, len(keep), 21000):
            part = keep[k:k + 21000]
            dl += struct.pack('>BH', 0x90, 3 * len(part))
            for t in part:
                for vx in t: dl += struct.pack(fmt, *vx)
        new_dl[first] = _pad(bytes(dl), 32)
    prim = bytearray(); new_locs = []
    for k in range(npk_total):
        size, off = locs[k]
        blk = new_dl.get(k, s[so[5] + off:so[5] + off + size])
        new_locs.append((len(blk), len(prim))); prim += blk
    assert so[5] < so[6] < so[7]
    sh = bytearray(s[:so[5]]); o5 = len(sh); sh += prim
    sh = bytearray(_pad(bytes(sh), 4)); o6 = len(sh); sh += s[so[6]:so[7]]
    sh = bytearray(_pad(bytes(sh), 4)); o7 = len(sh); sh += b''.join(struct.pack('>II', a, b) for a, b in new_locs)
    sh = bytearray(_pad(bytes(sh), 32))
    so2 = so[:]; so2[5], so2[6], so2[7] = o5, o6, o7
    struct.pack_into('>8I', sh, 0xC, *so2); struct.pack_into('>I', sh, 4, len(sh))
    out = bytearray(d[:32]); o = 32
    for tag in S:
        so3, sz = S[tag]
        out += bytes(sh) if tag == b'SHP1' else d[so3:so3 + sz]
    struct.pack_into('>I', out, 8, len(out))
    return bytes(out), dropped
