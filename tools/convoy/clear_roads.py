"""Move the convoy roads (src/convoy_routes.inc) away from the canyon walls.

The roads were traced by A* over the field's walkable grid and often run a single cell (100 units)
from a cliff: a wagon is ~220 wide, so its coach scraped up the rock. Each point is pushed up the
wall-distance field toward the middle of its corridor until it has kClear of room on both sides (or
the corridor's own middle, where it's narrower), the line is kept smooth, and heights and rooms are
taken again from the ground there. Bridges (no ground in the grid) and the loading-zone ends stay put.

usage: python3 -I clear_roads.py <grid_F_SP121.pkl> <rooms.pkl> <convoy_routes.inc>   (rewrites it)
"""
import sys, re, pickle, numpy as np
sys.path[:0] = [__import__('os').path.join(__import__('os').path.dirname(__file__), '..', 'field')]
import navgrid, __main__
__main__.Grid = navgrid.Grid
from scipy.ndimage import distance_transform_edt, sobel, map_coordinates
from scipy.spatial import cKDTree

kClear = 320.0     # room wanted from the road's middle to the nearest wall
kStep = 25.0       # most a point moves per pass
kPasses = 40
kKeepEnds = 6      # points at each end left alone (the loading zones)


def load_roads(src):
    roads = {}
    for name in re.findall(r'constexpr ConvoyPt (\w+)\[\]', src):
        blk = src[src.index(name):]
        blk = blk[:blk.index('};')]
        roads[name] = np.array([[float(a), float(b), float(c), int(r)] for a, b, c, r in
                                re.findall(r'\{(-?[\d.]+)f, (-?[\d.]+)f, (-?[\d.]+)f, (-?\d+)\}', blk)])
    return roads


def main(grid_p, rooms_p, inc):
    g = pickle.load(open(grid_p, 'rb'))
    rooms = pickle.load(open(rooms_p, 'rb'))
    C, L = [], []
    for (st, rn), v in rooms.items():
        if st == 'F_SP121':
            C.append(v['T'].mean(1)[:, [0, 2]]); L += [rn] * len(v['T'])
    tree = cKDTree(np.concatenate(C)); L = np.array(L)
    ground = (g.h > -1e8) & ~g.bad
    free = ground & g.walk
    dt = distance_transform_edt(free) * navgrid.CELL
    gi, gj = sobel(dt, 0), sobel(dt, 1)

    def at(F, x, z):
        fi = (z - g.z0) / navgrid.CELL - 0.5
        fj = (x - g.x0) / navgrid.CELL - 0.5
        return map_coordinates(F, [np.atleast_1d(fi), np.atleast_1d(fj)], order=1, mode='nearest')

    src = open(inc).read()
    roads = load_roads(src)
    out = {}
    for name, R in roads.items():
        P = R[:, [0, 2]].copy()
        on_ground = np.array([ground[g.cell(x, z)] for x, z in P])
        movable = on_ground.copy()
        movable[:kKeepEnds] = movable[-kKeepEnds:] = False
        # bridges and their approaches stay exactly as they are
        for k in np.where(~on_ground)[0]:
            movable[max(0, k - 3):k + 4] = False
        before = at(dt, P[:, 0], P[:, 1])
        for _ in range(kPasses):
            d = at(dt, P[:, 0], P[:, 1])
            gz, gx = at(gi, P[:, 0], P[:, 1]), at(gj, P[:, 0], P[:, 1])
            n = np.hypot(gx, gz) + 1e-6
            want = np.clip(kClear - d, 0, kStep)
            step = np.stack([gx / n, gz / n], 1) * want[:, None]
            P[movable] += step[movable]
            # keep it a smooth line (only where it may move)
            S = P.copy()
            S[1:-1] = 0.25 * P[:-2] + 0.5 * P[1:-1] + 0.25 * P[2:]
            P[movable] = S[movable]
        after = at(dt, P[:, 0], P[:, 1])
        # heights: the surface carrying on from the last point (not a ledge beside it)
        ys = R[:, 1].copy(); prev = None
        for k, (x, z) in enumerate(P):
            if not movable[k]:
                prev = ys[k]; continue
            i, j = g.cell(x, z)
            h = g.h[i - 2:i + 3, j - 2:j + 3]; h = h[h > -1e8]
            if h.size:
                ys[k] = float(h[np.argmin(np.abs(h - prev))]) if prev is not None else float(np.median(h))
            prev = ys[k]
        ys = np.array([np.median(ys[max(0, i - 3):i + 4]) if movable[i] else ys[i] for i in range(len(ys))])
        rm = np.where(movable, L[tree.query(P)[1]], R[:, 3]).astype(int)
        out[name] = np.column_stack([P[:, 0], ys, P[:, 1], rm])
        moved = np.hypot(*(P - R[:, [0, 2]]).T)
        print(f'{name}: {len(P)} points; under 200 from a wall {int((before[on_ground] < 200).sum())} -> '
              f'{int((after[on_ground] < 200).sum())}; most moved {moved.max():.0f}, mean {moved[movable].mean():.0f}')
    lines = []
    for line in src.split('\n'):
        lines.append(line)
    res = src.split('constexpr ConvoyPt')[0]
    for name, A in out.items():
        res += 'constexpr ConvoyPt %s[] = {\n' % name
        res += ''.join('    {%.1ff, %.1ff, %.1ff, %d},\n' % (x, y, z, r) for x, y, z, r in A)
        res += '};\n'
    res = res.replace('// Points every ~150 units: x, y (ground), z, room.',
                      '// Points every ~150 units: x, y (ground), z, room. Kept clear of the walls by\n'
                      '// tools/convoy/clear_roads.py.')
    open(inc, 'w').write(res)


if __name__ == '__main__':
    main(*sys.argv[1:4])
