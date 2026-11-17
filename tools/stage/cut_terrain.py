"""Our copy of a field room model with everything inside Beedle's tent cut away along the tent's outline
(the field's ground under the tent is a few huge triangles: removing whole ones would leave a hole you'd
see out of the door). Triangles crossing the outline are clipped; the parts outside stay, with new
corner vertices interpolated (position, normal, colour, UV) in the model's own vertex formats. Shapes
that don't reach the tent keep their display lists byte for byte; clipped shapes are rewritten as
triangle lists with the same attributes. Only for single-packet, unskinned shapes with 16-bit indices
(the field room models are all of that kind; checked as it goes).
Usage: python3 -I cut_terrain.py IN.bmd OUT.bmd X0 Y0 Z0 YAW16 HALF_W HALF_L"""
import sys, struct, math
import numpy as np
sys.path[:0] = ['/home/claude/anim']
from bmdwrite import sections, pad

src, dst = sys.argv[1:3]
TX, TY, TZ, YAW, HW, HL = (float(x) for x in sys.argv[3:9])
_a = YAW / 65536 * 2 * math.pi; C, S = math.cos(_a), math.sin(_a)
def local(p):       # world -> the tent's own frame (x across, z toward the doorway)
    dx, dz = p[0] - TX, p[2] - TZ
    return dx * C - dz * S, dz * C + dx * S

d = open(src, 'rb').read()
T = sections(d)
order = list(T.keys())
assert order == [b'INF1', b'VTX1', b'EVP1', b'DRW1', b'JNT1', b'SHP1', b'MAT3', b'TEX1'], order

# ---- VTX1: formats and arrays ----
v = bytes(T[b'VTX1'])
fo = struct.unpack_from('>I', v, 8)[0]
voffs = list(struct.unpack_from('>13I', v, 0xC))
fmts = []
p = fo
while True:
    a, ct, dt, fr = struct.unpack_from('>IIIB', v, p); p += 16
    if a == 0xFF: break
    fmts.append((a, ct, dt, fr))
SLOT = {9: 0, 10: 1, 11: 3, 13: 5}
assert [f[0] for f in fmts] == [9, 10, 11, 13], fmts
assert fmts[0][2] == 4 and fmts[1][2] == 3 and fmts[2][2] == 5 and fmts[3][2] == 3, fmts
NRM_FR, UV_FR = fmts[1][3], fmts[3][3]
used = sorted(o for o in voffs if o)
def raw(slot):
    st = voffs[slot]; nxt = [o for o in used if o > st]
    return v[st:(nxt[0] if nxt else len(v))]
pos = np.frombuffer(raw(0), '>f4').astype(float); pos = pos[:len(pos) // 3 * 3].reshape(-1, 3)
nrm = np.frombuffer(raw(1), '>i2').astype(float) / (1 << NRM_FR); nrm = nrm[:len(nrm) // 3 * 3].reshape(-1, 3)
clr = np.frombuffer(raw(3), 'u1').astype(float); clr = clr[:len(clr) // 4 * 4].reshape(-1, 4)
uvs = np.frombuffer(raw(5), '>i2').astype(float) / (1 << UV_FR); uvs = uvs[:len(uvs) // 2 * 2].reshape(-1, 2)
arrays = [list(map(tuple, pos)), list(map(tuple, nrm)), list(map(tuple, clr)), list(map(tuple, uvs))]

# ---- SHP1 ----
s = bytes(T[b'SHP1'])
nsh = struct.unpack_from('>H', s, 8)[0]
so = list(struct.unpack_from('>8I', s, 0xC))
def shape_info(i):
    e = s[so[0] + 0x28 * i:so[0] + 0x28 * (i + 1)]
    npk, attr_off, _, first = struct.unpack_from('>HHHH', e, 2)
    attrs = []; o = so[3] + attr_off
    while True:
        at, ty = struct.unpack_from('>II', s, o); o += 8
        if at == 0xFF: break
        attrs.append((at, ty))
    return e[0], npk, first, attrs
npk_total = (len(s) - so[7]) // 8
locs = [struct.unpack_from('>II', s, so[7] + 8 * k) for k in range(npk_total)]

def read_tris(first, attrs):
    size, off = locs[first]; q = so[5] + off; end = q + size; tris = []
    while q < end and s[q] != 0:
        op = s[q]; cnt = struct.unpack_from('>H', s, q + 1)[0]; q += 3; vs = []
        for _ in range(cnt):
            vs.append(struct.unpack_from('>%dH' % len(attrs), s, q)); q += 2 * len(attrs)
        if op == 0x90: tris += [(vs[k], vs[k + 1], vs[k + 2]) for k in range(0, cnt - 2, 3)]
        elif op == 0x98: tris += [((vs[k], vs[k + 1], vs[k + 2]) if k % 2 == 0 else (vs[k + 1], vs[k], vs[k + 2])) for k in range(cnt - 2)]
        elif op == 0xA0: tris += [(vs[0], vs[k], vs[k + 1]) for k in range(1, cnt - 1)]
        else: raise SystemExit('primitive %x' % op)
    return tris

# a vertex while clipping: (index tuple or None, (pos, nrm, clr, uv) floats)
def vert(ix): return (ix, tuple(np.array(arrays[k][ix[k]], float) for k in range(4)))
def lerp(A, B, t):
    pa, na, ca, ua = A[1]; pb, nb, cb, ub = B[1]
    n = na + (nb - na) * t; n = n / max(np.linalg.norm(n), 1e-9)
    return (None, (pa + (pb - pa) * t, n, ca + (cb - ca) * t, ua + (ub - ua) * t))
def clip(poly, f):   # keep f(v) >= 0 (f linear in world x, z)
    out = []
    for i in range(len(poly)):
        A, B = poly[i], poly[(i + 1) % len(poly)]
        fa, fb = f(A), f(B)
        if fa >= 0: out.append(A)
        if (fa >= 0) != (fb >= 0): out.append(lerp(A, B, fa / (fa - fb)))
    return out
lx = lambda V: local(V[1][0])[0]
lz = lambda V: local(V[1][0])[1]
REGIONS = [   # the outside of the outline, as four convex pieces
    [lambda V: -HW - lx(V)],
    [lambda V: lx(V) - HW],
    [lambda V: lx(V) + HW, lambda V: HW - lx(V), lambda V: -HL - lz(V)],
    [lambda V: lx(V) + HW, lambda V: HW - lx(V), lambda V: lz(V) - HL],
]
def new_index(V):
    if V[0] is not None: return V[0]
    p_, n_, c_, u_ = V[1]
    ix = []
    for k, val in enumerate((p_, n_, np.clip(np.round(c_), 0, 255), u_)):
        arrays[k].append(tuple(val)); ix.append(len(arrays[k]) - 1)
    return tuple(ix)

new_dl = {}
stats = []
for i in range(nsh):
    mtype, npk, first, attrs = shape_info(i)
    tris = read_tris(first, attrs)
    touch = False
    for t in tris:
        L = [local(arrays[0][ix[0]]) for ix in t]
        xs = [a for a, _ in L]; zs = [b for _, b in L]
        if max(xs) > -HW and min(xs) < HW and max(zs) > -HL and min(zs) < HL: touch = True; break
    if not touch: continue
    assert npk == 1 and mtype == 0 and [a for a, _ in attrs] == [9, 10, 11, 13] and all(ty == 3 for _, ty in attrs), (i, attrs)
    out = []; cut = 0
    for t in tris:
        L = [local(arrays[0][ix[0]]) for ix in t]
        xs = [a for a, _ in L]; zs = [b for _, b in L]
        if not (max(xs) > -HW and min(xs) < HW and max(zs) > -HL and min(zs) < HL):
            out.append(t); continue
        cut += 1
        for fs in REGIONS:
            poly = [vert(ix) for ix in t]
            for f in fs:
                poly = clip(poly, f)
                if len(poly) < 3: break
            if len(poly) < 3: continue
            ids = [new_index(V) for V in poly]
            out += [(ids[0], ids[k], ids[k + 1]) for k in range(1, len(ids) - 1)]
    dl = bytearray()
    for k in range(0, len(out), 21000):
        part = out[k:k + 21000]
        dl += struct.pack('>BH', 0x90, 3 * len(part))
        for t in part:
            for ix in t: dl += struct.pack('>4H', *ix)
    new_dl[first] = bytes(pad(bytes(dl), 32))
    stats.append((i, len(tris), cut, len(out)))
for a in arrays: assert len(a) < 0xFFFF
print('clipped shapes (shape, triangles, crossing the outline, triangles after):', stats)
print('new vertices: pos +%d' % (len(arrays[0]) - len(pos)))

# rebuild SHP1: everything before the primitive data, the data, the matrix data, the packet locations
prim = bytearray(); new_locs = []
for k in range(npk_total):
    size, off = locs[k]
    blk = new_dl.get(k, s[so[5] + off:so[5] + off + size])
    new_locs.append((len(blk), len(prim))); prim += blk
assert so[5] < so[6] < so[7] and so[2] == 0
sh = bytearray(s[:so[5]]); o5 = len(sh); sh += prim
sh = bytearray(pad(bytes(sh), 4)); o6 = len(sh); sh += s[so[6]:so[7]]
sh = bytearray(pad(bytes(sh), 4)); o7 = len(sh); sh += b''.join(struct.pack('>II', a, b) for a, b in new_locs)
sh = bytearray(pad(bytes(sh), 32))
so2 = so[:]; so2[5], so2[6], so2[7] = o5, o6, o7
struct.pack_into('>8I', sh, 0xC, *so2); struct.pack_into('>I', sh, 4, len(sh))

# rebuild VTX1: same header and formats, the four arrays 32-aligned
vt = bytearray(v[:used[0]])
packs = [('>3f', 0, 1), ('>3h', 1, 1 << NRM_FR), ('4B', 3, 1), ('>2h', 5, 1 << UV_FR)]
nvoffs = [0] * 13
for k, (fmt, slot, scale) in enumerate(packs):
    vt = bytearray(pad(bytes(vt), 32)); nvoffs[slot] = len(vt)
    for val in arrays[k]:
        if fmt == '>3f': vt += struct.pack(fmt, *val)
        elif fmt == '4B': vt += struct.pack(fmt, *[int(x) for x in val])
        else: vt += struct.pack(fmt, *[int(max(-32768, min(32767, round(x * scale)))) for x in val])
vt = bytearray(pad(bytes(vt), 32))
struct.pack_into('>13I', vt, 0xC, *nvoffs); struct.pack_into('>I', vt, 4, len(vt))

T[b'VTX1'] = bytes(vt); T[b'SHP1'] = bytes(sh)
out = bytearray(d[:32])
for k in order: out += bytes(T[k])
struct.pack_into('>I', out, 8, len(out))
open(dst, 'wb').write(out)
print(dst, len(d), '->', len(out), 'bytes')
