"""J3D BCK (ANK1) reader / sampler / writer.

Joint track layout (54 bytes): for axis x,y,z: scale(cnt,idx,tan), rot(cnt,idx,tan), trans(cnt,idx,tan).
Keys: cnt==1 -> constant table[idx]; else cnt keys of (time,value,tan) [tan type 0] or
(time,value,inTan,outTan) [tan type 1]. Rotations are s16, scaled by (1<<rotShift)*pi/32768.
"""
import struct, math
import numpy as np


def _hermite(keys, t):
    # keys: list of (time, value, inTan, outTan)
    if t <= keys[0][0]:
        return keys[0][1]
    if t >= keys[-1][0]:
        return keys[-1][1]
    for i in range(len(keys) - 1):
        t0, v0, _, o0 = keys[i]
        t1, v1, i1, _ = keys[i + 1]
        if t0 <= t <= t1:
            dt = t1 - t0
            if dt == 0:
                return v1
            s = (t - t0) / dt
            s2, s3 = s * s, s * s * s
            h00 = 2 * s3 - 3 * s2 + 1
            h10 = s3 - 2 * s2 + s
            h01 = -2 * s3 + 3 * s2
            h11 = s3 - s2
            return h00 * v0 + h10 * dt * o0 + h01 * v1 + h11 * dt * i1
    return keys[-1][1]


class BCK:
    def __init__(self, d):
        self.d = d
        o = 0x20
        assert d[o:o + 4] == b'ANK1', d[o:o + 4]
        self.loop = d[o + 8]
        self.rshift = d[o + 9]
        self.dur, self.nj, ns, nr, nt = struct.unpack_from('>5H', d, o + 0xA)
        jo, so, ro, to = struct.unpack_from('>4I', d, o + 0x14)
        S = struct.unpack_from('>%df' % ns, d, o + so)
        R = struct.unpack_from('>%dh' % nr, d, o + ro)
        T = struct.unpack_from('>%df' % nt, d, o + to)
        rs = (1 << self.rshift) * math.pi / 32768
        self.tracks = []  # per joint: [axis][s/r/t] -> keys
        for j in range(self.nj):
            e = o + jo + j * 54
            axes = []
            for a in range(3):
                comps = []
                for c, (tbl, mul) in enumerate(((S, 1.0), (R, rs), (T, 1.0))):
                    cnt, idx, tan = struct.unpack_from('>3H', self.d, e + a * 18 + c * 6)
                    comps.append(self._keys(tbl, mul, cnt, idx, tan))
                axes.append(comps)
            self.tracks.append(axes)

    @staticmethod
    def _keys(tbl, mul, cnt, idx, tan):
        if cnt <= 1:
            return [(0.0, tbl[idx] * mul, 0.0, 0.0)]
        st = 3 if tan == 0 else 4
        out = []
        for k in range(cnt):
            v = tbl[idx + k * st: idx + k * st + st]
            if st == 3:
                out.append((v[0], v[1] * mul, v[2] * mul, v[2] * mul))
            else:
                out.append((v[0], v[1] * mul, v[2] * mul, v[3] * mul))
        return out

    def sample(self, j, t):
        """returns (scale[3], rot[3] radians, trans[3]) for joint j at frame t"""
        a = self.tracks[j]
        s = [_hermite(a[k][0], t) for k in range(3)]
        r = [_hermite(a[k][1], t) for k in range(3)]
        tr = [_hermite(a[k][2], t) for k in range(3)]
        return s, r, tr


def srt_matrix(s, r, t):
    cx, sx = math.cos(r[0]), math.sin(r[0])
    cy, sy = math.cos(r[1]), math.sin(r[1])
    cz, sz = math.cos(r[2]), math.sin(r[2])
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    M = np.eye(4)
    M[:3, :3] = Rz @ Ry @ Rx @ np.diag(s)
    M[:3, 3] = t
    return M


def euler_xyz(R):
    """inverse of Rz@Ry@Rx"""
    sy = -R[2, 0]
    sy = max(-1.0, min(1.0, sy))
    ry = math.asin(sy)
    if abs(sy) < 0.99999:
        rx = math.atan2(R[2, 1], R[2, 2])
        rz = math.atan2(R[1, 0], R[0, 0])
    else:  # gimbal
        rx = math.atan2(-R[1, 2], R[1, 1])
        rz = 0.0
    return [rx, ry, rz]


def _unwrap(vals):
    out = [vals[0]]
    for v in vals[1:]:
        p = out[-1]
        while v - p > math.pi:
            v -= 2 * math.pi
        while v - p < -math.pi:
            v += 2 * math.pi
        out.append(v)
    return out


def write_bck(frames, loop=2, eps=(1e-4, 1e-4, 1e-3)):
    """frames: list over frame index of list over joints of (s[3], r[3], t[3]).
    Baked one key per frame, tangent type 0, tangents from central differences."""
    nf = len(frames)
    nj = len(frames[0])
    # unwrap rotations per channel
    rot = [[_unwrap([frames[f][j][1][a] for f in range(nf)]) for a in range(3)] for j in range(nj)]
    rmax = max(abs(v) for j in rot for a in j for v in a)
    rshift = 0
    while rmax * 32768 / math.pi / (1 << rshift) > 32000:
        rshift += 1
    rs = (1 << rshift) * math.pi / 32768
    S, R, T = [], [], []

    def add(tbl, series, kind):
        e = eps[kind]
        if max(series) - min(series) <= e or nf == 1:
            v = series[0]
            if kind == 1:
                v = int(round(v / rs))
            # reuse existing constant
            for i, x in enumerate(tbl):
                if x == v:
                    return (1, i, 0)
            tbl.append(v)
            return (1, len(tbl) - 1, 0)
        idx = len(tbl)
        for f in range(nf):
            a = series[max(f - 1, 0)]
            b = series[min(f + 1, nf - 1)]
            tan = (b - a) / (min(f + 1, nf - 1) - max(f - 1, 0))
            v = series[f]
            if kind == 1:
                tbl += [f, int(round(v / rs)), int(round(tan / rs))]
            else:
                tbl += [float(f), v, tan]
        return (nf, idx, 0)

    entries = []
    for j in range(nj):
        ent = []
        for a in range(3):
            ent.append(add(S, [frames[f][j][0][a] for f in range(nf)], 0))
            ent.append(add(R, rot[j][a], 1))
            ent.append(add(T, [frames[f][j][2][a] for f in range(nf)], 2))
        entries.append(ent)
    R = [max(-32768, min(32767, int(v))) for v in R]

    def pad32(b):
        return b + b'\0' * ((-len(b)) % 32)

    jb = b''.join(struct.pack('>3H', *c) for ent in entries for c in ent)
    sb = struct.pack('>%df' % len(S), *S)
    rb = struct.pack('>%dh' % len(R), *R)
    tb = struct.pack('>%df' % len(T), *T)
    hdr_len = 0x40
    off = hdr_len
    jo = off; off += len(pad32(jb))
    so = off; off += len(pad32(sb))
    ro = off; off += len(pad32(rb))
    to = off; off += len(pad32(tb))
    ank = b'ANK1' + struct.pack('>I', off) + struct.pack('>BBHHHHH', loop, rshift, nf - 1, nj, len(S), len(R), len(T))
    ank += struct.pack('>4I', jo, so, ro, to)
    ank = ank + b'\0' * (hdr_len - len(ank))
    ank += pad32(jb) + pad32(sb) + pad32(rb) + pad32(tb)
    head = b'J3D1bck1' + struct.pack('>II', 0x20 + len(ank), 1) + b'\xff' * 16
    return head + ank
