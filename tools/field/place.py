"""Find the best ground near each logged camp / Beedle's tent, and fit the tent to the slope."""
import sys, pickle, math, json
import numpy as np
sys.path.insert(0, '/home/claude/anim/field')
from navgrid import Grid, CELL

SPOTS = '/mnt/user-data/uploads/com.bytheeightdivines.dynamictravelers/spots.tsv'
SEARCH = 1800.0          # how far from your spot we may move it
SEARCH_WIDE = 3200.0     # if nothing reachable is that close (the amphitheater rim)
FOOT = {'camp': 650.0, 'beedle_tent': 420.0}   # clear radius needed (camp = tents + fire + seats)
ROAD_MAT = 1             # dirt road surface


DROP = set()   # (spots 0-2 are the escort-only valley, kept)


def load_spots():
    rows = [l.rstrip('\n').split('\t') for l in open(SPOTS) if not l.startswith('#')]
    out = []
    for n, r in enumerate(rows):
        if n in DROP:
            continue
        out.append(dict(i=n, kind=r[0], stage=r[1], room=int(r[2]), x=float(r[3]), y=float(r[4]),
                        z=float(r[5]), ang=int(r[6])))
    return out


def disk(rad):
    k = int(math.ceil(rad / CELL))
    ii, jj = np.mgrid[-k:k + 1, -k:k + 1]
    m = (ii * ii + jj * jj) * CELL * CELL <= rad * rad
    return ii[m], jj[m]


ROADSEG = []
FOOT_TRY = {}
REACH = {}


def reachable(g, seeds, step=90.0):
    from collections import deque
    ground = (g.h > -1e8) & ~g.bad
    R = np.zeros(g.h.shape, bool); q = deque()
    for s in seeds:
        if s is not None:
            R[s] = True; q.append(s)
    while q:
        i, j = q.popleft()
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                a, b = i + di, j + dj
                if (di or dj) and 0 <= a < g.nz and 0 <= b < g.nx and not R[a, b] and ground[a, b] \
                        and abs(g.h[a, b] - g.h[i, j]) <= step * (1.414 if di and dj else 1):
                    R[a, b] = True; q.append((a, b))
    return R


def road_dist(x, z):
    best = 1e9
    for (ax, az), (bx, bz) in ROADSEG:
        dx, dz = bx - ax, bz - az; L2 = dx * dx + dz * dz
        t = 0 if L2 == 0 else max(0, min(1, ((x - ax) * dx + (z - az) * dz) / L2))
        best = min(best, math.hypot(x - ax - t * dx, z - az - t * dz))
    return best


def place(g, s):
    rad = FOOT_TRY.get(s['kind'], FOOT[s['kind']])
    di, dj = disk(rad)
    ci, cj = g.cell(s['x'], s['z'])
    k = int(SEARCH / CELL)
    best = None
    for i in range(ci - k, ci + k + 1, 2):
        for j in range(cj - k, cj + k + 1, 2):
            dd = math.hypot(i - ci, j - cj) * CELL
            if dd > SEARCH:
                continue
            I, J = i + di, j + dj
            if I.min() < 0 or J.min() < 0 or I.max() >= g.nz or J.max() >= g.nx:
                continue
            wk = g.walk[I, J] & REACH[g.stage_name][I, J]
            Hc = g.h[I, J]
            if wk.any():
                med = np.median(Hc[wk])
                wk = wk & (np.abs(Hc - med) < 60 + np.hypot(di, dj) * CELL * 0.1)   # pillars / tiers are obstacles
            frac = wk.mean()
            if frac < 0.9:
                continue
            H = g.h[I, J][wk]
            # plane fit -> slope and roughness
            X = np.stack([dj[wk] * CELL, di[wk] * CELL, np.ones_like(H)], 1)
            coef, *_ = np.linalg.lstsq(X, H, rcond=None)
            rough = np.abs(H - X @ coef).max()
            slope = math.degrees(math.atan(math.hypot(coef[0], coef[1])))
            wx, wz = g.world(i, j)
            dr = road_dist(wx, wz)
            road_pen = 0.0 if dr >= rad * 0.6 else (rad * 0.6 - dr) / 40.0
            sc = slope * 1.5 + rough / 4.0 + dd / 300.0 + road_pen + (1 - frac) * 60
            if best is None or sc < best[0]:
                best = (sc, i, j, slope, rough, coef, dd, dr)
    if best is None:
        return None
    sc, i, j, slope, rough, coef, dd, dr = best
    x, z = g.world(i, j)
    # tilt: tent follows up to 6 degrees of slope; sink the low side a little so no corner floats
    tilt = min(slope, 6.0)
    return dict(x=x, z=z, y=float(coef[2]), slope=slope, rough=float(rough), moved=dd, road_dist=float(dr),
                tilt_deg=tilt, slope_dir=[float(coef[0]), float(coef[1])], sink=float(min(25.0, rough + 5)))


if __name__ == '__main__':
    G = {st: pickle.load(open('/home/claude/anim/field/grid_%s.pkl' % st, 'rb')) for st in ('F_SP121', 'F_SP122')}
    res = []
    sp = load_spots()
    roads = [q for q in sp if q['kind'] == 'road']
    for st, g in G.items():
        g.stage_name = st
        seeds = []
        for q in roads:
            if q['stage'] == st:
                ci, cj = g.cell(q['x'], q['z'])
                # nearest ground cell to the logged road spot
                best = None
                for i in range(ci - 6, ci + 7):
                    for j in range(cj - 6, cj + 7):
                        if g.walk[i, j] and (best is None or (i - ci) ** 2 + (j - cj) ** 2 < best[0]):
                            best = ((i - ci) ** 2 + (j - cj) ** 2, (i, j))
                seeds.append(best[1] if best else None)
        REACH[st] = reachable(g, seeds)
    for a, b in zip(roads, roads[1:]):
        if a['stage'] == b['stage'] and math.hypot(a['x'] - b['x'], a['z'] - b['z']) < 16000:
            ROADSEG.append(((a['x'], a['z']), (b['x'], b['z'])))
    for s in sp:
        if s['kind'] == 'road':
            continue
        p = None
        for r, sr in [(r, sr) for sr in (1800.0, SEARCH_WIDE) for r in (FOOT[s['kind']], 520.0, 400.0, 320.0)]:
            SEARCH = sr
            FOOT_TRY[s['kind']] = r
            p = place(G[s['stage']], s)
            if p is not None:
                p['clear_radius'] = r
                break
        if p is None:
            print(s['kind'], s['stage'], s['room'], 'NO SPOT FOUND'); continue
        print(f"{s['kind']:11s} {s['stage']} r{s['room']:2d}: moved {p['moved']:5.0f}  slope {p['slope']:4.1f} deg  "
              f"bumps {p['rough']:4.0f}  {p['road_dist']:5.0f} from road  clear radius {p['clear_radius']:.0f}  -> ({p['x']:.0f}, {p['y']:.0f}, {p['z']:.0f})")
        res.append({**s, 'placed': p})
    json.dump(res, open('/home/claude/anim/field/camps.json', 'w'), indent=1)
