"""Walkable-ground grid for a stage, rasterised from the rooms' KCL collision.

Per 100-unit cell: top walkable height, its surface material (att0), and whether a wall stands
on it at body height. Walkable = facing up (slope < ~45 deg), not a special surface (spl), not
horse-forbidden (water edges, fences), not a pass-through polygon.
"""
import sys, pickle, math
import numpy as np
sys.path.insert(0, '/home/claude/anim/field')

CELL = 100.0


class Grid:
    def __init__(self, rooms, stage):
        keys = [k for k in rooms if k[0] == stage]
        P = np.concatenate([rooms[k]['T'].reshape(-1, 3) for k in keys])
        self.x0, self.z0 = P[:, 0].min() - 500, P[:, 2].min() - 500
        self.nx = int((P[:, 0].max() + 500 - self.x0) / CELL) + 1
        self.nz = int((P[:, 2].max() + 500 - self.z0) / CELL) + 1
        self.h = np.full((self.nz, self.nx), -1e9)
        self.mat = np.full((self.nz, self.nx), -1, int)
        self.room = np.full((self.nz, self.nx), -1, int)
        self.bad = np.zeros((self.nz, self.nx), bool)       # top surface is water/forbidden
        self.wlo = np.full((self.nz, self.nx), 1e9)
        self.whi = np.full((self.nz, self.nx), -1e9)
        for k in keys:
            r = rooms[k]
            for T, n, a in zip(r['T'], r['FN'], r['AT']):
                p = r['plc'][a] if a < len(r['plc']) else None
                if p is not None and (p['link_through'] or p['obj_through']):
                    continue
                if n[1] > 0.70:
                    self._raster_top(T, p, k[1])
                elif abs(n[1]) < 0.5:
                    self._raster_wall(T)

    def cell(self, x, z):
        return int((z - self.z0) / CELL), int((x - self.x0) / CELL)

    def world(self, i, j):
        return self.x0 + (j + 0.5) * CELL, self.z0 + (i + 0.5) * CELL

    def _raster_top(self, T, p, room):
        xs, zs = T[:, 0], T[:, 2]
        j0, j1 = int((xs.min() - self.x0) / CELL), int((xs.max() - self.x0) / CELL) + 1
        i0, i1 = int((zs.min() - self.z0) / CELL), int((zs.max() - self.z0) / CELL) + 1
        gx = self.x0 + (np.arange(j0, j1) + 0.5) * CELL
        gz = self.z0 + (np.arange(i0, i1) + 0.5) * CELL
        X, Z = np.meshgrid(gx, gz)
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = T
        den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if abs(den) < 1e-6:
            return
        w0 = ((bz - cz) * (X - cx) + (cx - bx) * (Z - cz)) / den
        w1 = ((cz - az) * (X - cx) + (ax - cx) * (Z - cz)) / den
        w2 = 1 - w0 - w1
        m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
        if not m.any():
            return
        Y = w0 * ay + w1 * by + w2 * cy
        sub = self.h[i0:i1, j0:j1]
        upd = m & (Y > sub)
        sub[upd] = Y[upd]
        bad = p is not None and (p['spl'] != 0 or p['horse_no'])
        self.bad[i0:i1, j0:j1][upd] = bad
        self.mat[i0:i1, j0:j1][upd] = p['att0'] if p is not None else -1
        self.room[i0:i1, j0:j1][upd] = room

    def _raster_wall(self, T):
        # footprint of a near-vertical triangle: sample along its edges and interior in xz
        a, b, c = T
        L = max(np.linalg.norm((b - a)[[0, 2]]), np.linalg.norm((c - a)[[0, 2]]), np.linalg.norm((c - b)[[0, 2]]))
        n = max(2, int(L / (CELL * 0.5)) + 1)
        u, v = np.meshgrid(np.linspace(0, 1, n), np.linspace(0, 1, n))
        m = u + v <= 1
        P = a + u[m][:, None] * (b - a) + v[m][:, None] * (c - a)
        i = ((P[:, 2] - self.z0) / CELL).astype(int); j = ((P[:, 0] - self.x0) / CELL).astype(int)
        ok = (i >= 0) & (i < self.nz) & (j >= 0) & (j < self.nx)
        i, j, y = i[ok], j[ok], P[ok, 1]
        np.minimum.at(self.wlo, (i, j), y)
        np.maximum.at(self.whi, (i, j), y)

    def finalize(self, body_lo=25.0, body_hi=90.0):
        has = self.h > -1e8
        wall = (self.wlo <= self.h + body_lo) & (self.whi >= self.h + body_hi)
        self.walk = has & ~self.bad & ~wall
        return self


def build(stage):
    rooms = pickle.load(open('/home/claude/anim/field/rooms.pkl', 'rb'))
    return Grid(rooms, stage).finalize()


if __name__ == '__main__':
    import time
    for st in ('F_SP121', 'F_SP122'):
        t = time.time()
        g = build(st)
        pickle.dump(g, open('/home/claude/anim/field/grid_%s.pkl' % st, 'wb'))
        print(st, g.nx, 'x', g.nz, 'cells; walkable', int(g.walk.sum()), 'of', int((g.h > -1e8).sum()), 'ground cells; %.1fs' % (time.time() - t))
