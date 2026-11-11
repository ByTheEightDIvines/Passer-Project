"""Trace the travellers' road network from the logged spots over the walkable-ground grid.

Graph: road spots in the order they were logged; a spot that lies much closer to an earlier spot
than to the previous one branches from there (side trips). Camps and Beedle's tent join the
nearest road node by a spur. Stage changes become hand-off links at the game's own scene-change
triggers. Each edge is found with A* (dirt preferred, steep steps and walls refused); where the
grid has no ground (object bridges) the logged spots are trusted and joined straight.
"""
import sys, pickle, math, json, heapq
import numpy as np
sys.path.insert(0, '/home/claude/anim/field')
from navgrid import Grid, CELL
from place import load_spots
from dzr import actors

MAX_STEP = 55.0          # comfortable height change per 100-unit cell
HARD_STEP = 90.0         # never climb more than this (cliffs)
DIRT, GRASS = 1, 2


def astar(g, a, b, margin=60):
    (ai, aj), (bi, bj) = a, b
    i0, i1 = max(min(ai, bi) - margin, 0), min(max(ai, bi) + margin, g.nz - 1)
    j0, j1 = max(min(aj, bj) - margin, 0), min(max(aj, bj) + margin, g.nx - 1)
    H = g.h[i0:i1 + 1, j0:j1 + 1]; M = g.mat[i0:i1 + 1, j0:j1 + 1]
    clear = g.walk[i0:i1 + 1, j0:j1 + 1]
    walk = (H > -1e8) & ~g.bad[i0:i1 + 1, j0:j1 + 1]          # walls are avoided, not forbidden
    mult = np.where(M == DIRT, 1.0, np.where(M == GRASS, 1.35, 1.6)) * np.where(clear, 1.0, 10.0)
    s, t = (ai - i0, aj - j0), (bi - i0, bj - j0)
    if not (0 <= s[0] < walk.shape[0] and 0 <= t[0] < walk.shape[0]):
        return None
    dist = {s: 0.0}; prev = {}
    pq = [(0.0, s)]
    nb = [(-1, 0, 1), (1, 0, 1), (0, -1, 1), (0, 1, 1), (-1, -1, 1.414), (-1, 1, 1.414), (1, -1, 1.414), (1, 1, 1.414)]
    hfun = lambda p: math.hypot(p[0] - t[0], p[1] - t[1])
    while pq:
        f, p = heapq.heappop(pq)
        if p == t:
            break
        d = dist[p]
        if f - hfun(p) > d + 1e-9:
            continue
        for di, dj, w in nb:
            q = (p[0] + di, p[1] + dj)
            if not (0 <= q[0] < walk.shape[0] and 0 <= q[1] < walk.shape[1]) or not walk[q]:
                continue
            dh = abs(H[q] - H[p])
            if dh > HARD_STEP * w:
                continue
            steep = dh / (MAX_STEP * w)
            nd = d + w * mult[q] * (1 + steep ** 2 * 0.8 + (12.0 if steep > 1 else 0.0))
            if nd < dist.get(q, 1e18):
                dist[q] = nd; prev[q] = p
                heapq.heappush(pq, (nd + hfun(q), q))
    if t not in prev and t != s:
        return None
    path = [t]
    while path[-1] != s:
        path.append(prev[path[-1]])
    return [(i + i0, j + j0) for i, j in reversed(path)]


def dijkstra_all(g, a, margin=200):
    """costs from cell a to everything reachable in a window (same rules as astar)"""
    ai, aj = a
    i0, i1 = max(ai - margin, 0), min(ai + margin, g.nz - 1)
    j0, j1 = max(aj - margin, 0), min(aj + margin, g.nx - 1)
    H = g.h[i0:i1 + 1, j0:j1 + 1]; M = g.mat[i0:i1 + 1, j0:j1 + 1]
    clear = g.walk[i0:i1 + 1, j0:j1 + 1]
    walk = (H > -1e8) & ~g.bad[i0:i1 + 1, j0:j1 + 1]
    mult = np.where(M == DIRT, 1.0, np.where(M == GRASS, 1.35, 1.6)) * np.where(clear, 1.0, 10.0)
    s = (ai - i0, aj - j0)
    dist = {s: 0.0}; prev = {}; pq = [(0.0, s)]
    nb = [(-1, 0, 1), (1, 0, 1), (0, -1, 1), (0, 1, 1), (-1, -1, 1.414), (-1, 1, 1.414), (1, -1, 1.414), (1, 1, 1.414)]
    while pq:
        d, p = heapq.heappop(pq)
        if d > dist[p] + 1e-9:
            continue
        for di, dj, w in nb:
            q = (p[0] + di, p[1] + dj)
            if not (0 <= q[0] < walk.shape[0] and 0 <= q[1] < walk.shape[1]) or not walk[q]:
                continue
            dh = abs(H[q] - H[p])
            if dh > HARD_STEP * w:
                continue
            steep = dh / (MAX_STEP * w)
            nd = d + w * mult[q] * (1 + steep ** 2 * 0.8 + (12.0 if steep > 1 else 0.0))
            if nd < dist.get(q, 1e18):
                dist[q] = nd; prev[q] = p; heapq.heappush(pq, (nd, q))
    return dist, prev, (i0, j0)


def ground_then_gap(g, A, B, max_gap=4500.0):
    """walk the ground from A as close as possible to B, then a short straight hop (a bridge)"""
    dist, prev, (i0, j0) = dijkstra_all(g, tuple(A['cell']))
    best = None
    for (i, j), d in dist.items():
        x, z = g.world(i + i0, j + j0)
        gap = math.hypot(x - B['x'], z - B['z'])
        sc = d * CELL * 0.25 + gap * 3.0
        if best is None or sc < best[0]:
            best = (sc, (i, j), gap)
    if best is None or best[2] > max_gap:
        return None
    p = best[1]; path = [p]
    while path[-1] in prev:
        path.append(prev[path[-1]])
    path = [(i + i0, j + j0) for i, j in reversed(path)]
    return path, best[2]


def snap(g, x, z, rad=8):
    """nearest walkable cell, preferring dirt"""
    ci, cj = g.cell(x, z)
    best = None
    for i in range(ci - rad, ci + rad + 1):
        for j in range(cj - rad, cj + rad + 1):
            if 0 <= i < g.nz and 0 <= j < g.nx and g.walk[i, j]:
                d = math.hypot(i - ci, j - cj) * (0.8 if g.mat[i, j] == DIRT else 1.0)
                if best is None or d < best[0]:
                    best = (d, (i, j))
    return None if best is None else best[1]


def simplify(pts, tol):
    if len(pts) < 3:
        return pts
    if len(pts) > 600:   # iterative split to avoid deep recursion on long paths
        k = len(pts) // 2
        return simplify(pts[:k + 1], tol)[:-1] + simplify(pts[k:], tol)
    a, b = np.array(pts[0]), np.array(pts[-1])
    ab = b - a; L = np.linalg.norm(ab)
    d = [abs(ab[0] * (p[1] - a[1]) - ab[1] * (p[0] - a[0])) / L if L > 0 else np.linalg.norm(np.array(p) - a) for p in pts[1:-1]]
    k = int(np.argmax(d)) + 1
    if d[k - 1] > tol:
        return simplify(pts[:k + 1], tol)[:-1] + simplify(pts[k:], tol)
    return [pts[0], pts[-1]]


def scene_changes(rooms):
    out = []
    for (st, room), r in rooms.items():
        if r['dzr'] is None:
            continue
        for a in actors(r['dzr']):
            if a['name'] == 'scnChg' and a['pos'][1] > -14000:
                out.append(dict(stage=st, room=room, pos=a['pos'], params=a['params']))
    return out


if __name__ == '__main__':
    G = {st: pickle.load(open('/home/claude/anim/field/grid_%s.pkl' % st, 'rb')) for st in ('F_SP121', 'F_SP122')}
    rooms = pickle.load(open('/home/claude/anim/field/rooms.pkl', 'rb'))
    camps = {c['i']: c for c in json.load(open('/home/claude/anim/field/camps.json'))}
    spots = load_spots()
    nodes = {}
    for s in spots:
        g = G[s['stage']]
        if s['kind'] == 'road':
            c = snap(g, s['x'], s['z'])
            nodes[s['i']] = dict(s, cell=c, x2=None)
        else:
            p = camps[s['i']]['placed']
            nodes[s['i']] = dict(s, cell=g.cell(p['x'], p['z']))
    # ---- edges
    edges = []
    road_ids = [s['i'] for s in spots if s['kind'] == 'road']
    seen = []
    prev = None
    for sid in road_ids:
        s = nodes[sid]
        if prev is not None:
            p = nodes[prev]
            dprev = math.hypot(s['x'] - p['x'], s['z'] - p['z'])
            # branch back to a closer earlier node (after a side trip)
            cand = [(math.hypot(s['x'] - nodes[q]['x'], s['z'] - nodes[q]['z']), q) for q in seen if nodes[q]['stage'] == s['stage']]
            near = min(cand) if cand else (1e18, None)
            src = prev
            if near[1] is not None and near[0] * 1.6 < dprev and near[1] != prev:
                src = near[1]
            if s['stage'] != p['stage'] or (dprev > 30000 and near[0] > 30000):
                # stage change: hand off from the nearest earlier node in the stage we came from
                other = [(math.hypot(s['x'] - nodes[q]['x'], s['z'] - nodes[q]['z']), q) for q in seen if nodes[q]['stage'] == p['stage']]
                o = min(other) if other else (1e18, None)
                src = o[1] if o[0] < 25000 else None
            if src is not None:
                edges.append((src, sid, 'road'))
        seen.append(sid); prev = sid
    # camps: spur to the nearest road nodes in the same stage
    for s in spots:
        if s['kind'] == 'road':
            continue
        cand = sorted((math.hypot(s['x'] - nodes[q]['x'], s['z'] - nodes[q]['z']), q) for q in road_ids if nodes[q]['stage'] == s['stage'])
        edges.append((cand[0][1], s['i'], 'spur'))
    # ---- trace
    SC = scene_changes(rooms)
    out_edges = []
    for a, b, kind in edges:
        A, B = nodes[a], nodes[b]
        rec = dict(a=a, b=b, kind=kind)
        if A['stage'] != B['stage']:
            # hand-off at the nearest scene-change trigger on each side
            mid = ((A['x'] + B['x']) / 2, (A['z'] + B['z']) / 2)
            sa = min((s for s in SC if s['stage'] == A['stage']), key=lambda s: math.hypot(s['pos'][0] - mid[0], s['pos'][2] - mid[1]))
            sb = min((s for s in SC if s['stage'] == B['stage']), key=lambda s: math.hypot(s['pos'][0] - mid[0], s['pos'][2] - mid[1]))
            legs = []
            for st, n0, xz in ((A['stage'], A, sa['pos']), (B['stage'], B, sb['pos'])):
                g = G[st]; c = snap(g, xz[0], xz[2], rad=15)
                path = astar(g, n0['cell'], c) if c else None
                legs.append([g.world(i, j) + (float(g.h[i, j]),) for i, j in path] if path else [])
            rec.update(kind='handoff', handoff=dict(from_trigger=[round(v) for v in sa['pos']], to_trigger=[round(v) for v in sb['pos']]),
                       leg_a=legs[0], leg_b=legs[1][::-1])
            out_edges.append(rec); continue
        g = G[A['stage']]
        path = astar(g, A['cell'], B['cell']) if A['cell'] and B['cell'] else None
        straight = math.hypot(A['x'] - B['x'], A['z'] - B['z'])
        if path is None or len(path) * CELL > 2.6 * straight + 3000:
            gg = ground_then_gap(g, A, B) if straight > 4500 else None
            if gg is not None:
                gpath, gap = gg
                pts = [g.world(i, j) for i, j in gpath]
                keep = simplify(pts, 120.0)
                rec['points'] = [(x, z, float(g.h[g.cell(x, z)])) for x, z in keep] + [(B['x'], B['z'], B['y'])]
                rec['bridge_gap'] = gap
                rec['length'] = len(gpath) * CELL + gap
                print(f"{a:2d}->{b:2d} {kind:7s} ground {len(gpath) * CELL:6.0f} then a {gap:4.0f}-unit hop (bridge)")
                out_edges.append(rec); continue
            rec['trusted_straight'] = True
            rec['points'] = [(A['x'], A['z'], A['y']), (B['x'], B['z'], B['y'])]
        else:
            pts = [g.world(i, j) for i, j in path]
            keep = simplify(pts, 120.0)
            rec['points'] = [(x, z, float(g.h[g.cell(x, z)])) for x, z in keep]
            rec['length'] = len(path) * CELL
        out_edges.append(rec)
        print(f"{a:2d}->{b:2d} {kind:7s} {A['stage']} straight {straight:6.0f} "
              + (f"path {rec['length']:6.0f} ({len(rec['points'])} waypoints)" if 'length' in rec else 'NO GROUND PATH -> trusted straight line'))
    json.dump(dict(nodes={k: {kk: (list(v) if isinstance(v, tuple) else v) for kk, v in n.items()} for k, n in nodes.items()},
                   edges=out_edges), open('/home/claude/anim/field/routes.json', 'w'), indent=1, default=float)
    print('handoffs:', [(e['a'], e['b'], e['handoff']['from_trigger']) for e in out_edges if e['kind'] == 'handoff'])
