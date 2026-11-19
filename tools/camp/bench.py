"""The camp benches: a split log, flat face up, on two short log legs (seats for 2 or 3).

Textures straight from the camp's own props, unchanged (packed side by side into one atlas): the
mossy bark of the camp stools (psr_seat.bmd) for the bark, the pale log of Coro's firewood
(coro_wood.bmd) for the split face and the cut ends. Rolled edges where the split face meets the bark.

Model space: length along X, sitters face +Z, the ground at y = 0, the seat top at kTop.
usage: bench.py <dusk with PsrPack/PsrCamp> <out dir>
"""
import sys, os, zipfile, numpy as np
sys.path[:0] = ['/home/claude/w/anim', os.path.join(os.path.dirname(__file__), '..', 'looks')]
from rarc import rarc
import regions as R
from bmdwrite import write_rigid_bmd

kTop = 44.0          # seat top (the camp stools stood 40)
kR = 17.0            # the split log's radius (it's a half log: kR deep)
kRoll = 3.0          # the rounded edge where the split face meets the bark
kSeatGap = 90.0      # seat to seat
kLegR = 11.0
kSeg = 10            # around the half log


def textures(dusk):
    z = zipfile.ZipFile(dusk)
    pack = rarc(z.read('overlay/res/Object/PsrPack.arc'))
    camp = rarc(z.read('overlay/res/Object/PsrCamp.arc'))
    bark = np.asarray(R.load(pack['bmdr/psr_seat.bmd'])[0].tex[0])[..., :3]
    wood = np.asarray(R.load(camp['bmdr/coro_wood.bmd'])[0].tex[0])[..., :3]
    assert bark.shape == (128, 128, 3) and wood.shape == (128, 128, 3)
    return np.concatenate([bark, wood], 0)   # 128 wide, 256 tall: bark on top, the firewood under it


# atlas regions (u0, u1, v0, v1)
BARK = (0.0, 1.0, 0.05, 0.47)            # the stools' mossy bark
PALE = (0.24, 0.48, 0.79, 0.99)          # Coro's pale log: the split face and the cut ends


def reg(r, u, v):
    return (r[0] + (r[1] - r[0]) * u, r[2] + (r[3] - r[2]) * v)


def quad(tris, a, b, c, d, ua, ub, uc, ud):
    tris.append((np.array([a, b, c]), np.array([ua, ub, uc])))
    tris.append((np.array([a, c, d]), np.array([ua, uc, ud])))


def bench(seats):
    L = seats * kSeatGap + 40.0
    x0, x1 = -L / 2, L / 2
    tris = []
    # the half log's cross-section: the flat top (rounded at its two edges), then the bark round below
    prof = []
    for k in range(3):   # rolled edge, -z side
        a = np.pi / 2 * k / 2
        prof.append((-(kR - kRoll) - kRoll * np.sin(a), kTop - kRoll + kRoll * np.cos(a)))
    for k in range(1, kSeg):   # the bark: from -z round under to +z
        a = np.pi * k / kSeg
        prof.append((-kR * np.cos(a), kTop - kRoll - (kR - kRoll) * np.sin(a)))
    for k in range(3):   # rolled edge, +z side
        a = np.pi / 2 * (1 - k / 2)
        prof.append(((kR - kRoll) + kRoll * np.sin(a), kTop - kRoll + kRoll * np.cos(a)))
    # bark and rolled edges along the length
    for i in range(len(prof) - 1):
        (za, ya), (zb, yb) = prof[i], prof[i + 1]
        va, vb = i / (len(prof) - 1), (i + 1) / (len(prof) - 1)
        rr = BARK
        quad(tris, (x0, ya, za), (x1, ya, za), (x1, yb, zb), (x0, yb, zb),
             reg(rr, va, 0), reg(rr, va, 1), reg(rr, vb, 1), reg(rr, vb, 0))
    # the split face on top
    zt = kR - kRoll
    quad(tris, (x0, kTop, zt), (x1, kTop, zt), (x1, kTop, -zt), (x0, kTop, -zt),
         reg(PALE, 0, 0), reg(PALE, 0, 1), reg(PALE, 1, 1), reg(PALE, 1, 0))
    # the cut ends (half discs)
    for x, s in ((x0, -1), (x1, 1)):
        c = (x, kTop - kRoll, 0.0)
        cuv = reg(PALE, 0.5, 0.15)
        pts = prof + [prof[0]]
        for i in range(len(prof)):
            (za, ya), (zb, yb) = pts[i], pts[i + 1]
            ua = reg(PALE, 0.5 + za / (2 * kR), 0.15 + (kTop - ya) / kR * 0.7)
            ub = reg(PALE, 0.5 + zb / (2 * kR), 0.15 + (kTop - yb) / kR * 0.7)
            if s > 0: tris.append((np.array([c, (x, ya, za), (x, yb, zb)]), np.array([cuv, ua, ub])))
            else: tris.append((np.array([c, (x, yb, zb), (x, ya, za)]), np.array([cuv, ub, ua])))
    # two legs: short logs, a little in from each end, slightly splayed out
    legTop = kTop - kR + 2.0
    for lx in (x0 + 30.0, x1 - 30.0):
        splay = 6.0 if lx > 0 else -6.0
        n = 8
        for k in range(n):
            a0, a1 = 2 * np.pi * k / n, 2 * np.pi * (k + 1) / n
            t0 = (lx + kLegR * np.cos(a0), legTop, kLegR * np.sin(a0))
            t1 = (lx + kLegR * np.cos(a1), legTop, kLegR * np.sin(a1))
            b0 = (lx + splay + kLegR * 1.1 * np.cos(a0), 0.0, kLegR * 1.1 * np.sin(a0))
            b1 = (lx + splay + kLegR * 1.1 * np.cos(a1), 0.0, kLegR * 1.1 * np.sin(a1))
            quad(tris, t0, t1, b1, b0, reg(BARK, k / n, 0.2), reg(BARK, (k + 1) / n, 0.2),
                 reg(BARK, (k + 1) / n, 0.55), reg(BARK, k / n, 0.55))
    return [(np.asarray(p, float), np.asarray(uv, float)) for p, uv in tris]


if __name__ == '__main__':
    dusk, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    tex = textures(dusk)
    for n in (2, 3):
        write_rigid_bmd(os.path.join(out, 'psr_bench%d.bmd' % n), bench(n), tex, texname='psr_bench',
                        jointname='bench', matname='psr_bench')
        print('psr_bench%d.bmd' % n)
