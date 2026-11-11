"""Add the bridge camp on the valley side of the broken bridge (room 0, east)."""
import sys, json, pickle, math
sys.path.insert(0, '/home/claude/anim/field')
import place as P
from navgrid import Grid

G = {st: pickle.load(open('/home/claude/anim/field/grid_%s.pkl' % st, 'rb')) for st in ('F_SP121', 'F_SP122')}
sp = P.load_spots()
roads = [q for q in sp if q['kind'] == 'road']
for a, b in zip(roads, roads[1:]):
    if a['stage'] == b['stage'] and math.hypot(a['x'] - b['x'], a['z'] - b['z']) < 16000:
        P.ROADSEG.append(((a['x'], a['z']), (b['x'], b['z'])))
g = G['F_SP121']; g.stage_name = 'F_SP121'
# reachable from the valley spots only (the camp must be on the valley side of the gap)
seeds = []
for q in roads[:3]:
    ci, cj = g.cell(q['x'], q['z'])
    best = min(((i - ci) ** 2 + (j - cj) ** 2, (i, j)) for i in range(ci - 6, ci + 7) for j in range(cj - 6, cj + 7) if g.walk[i, j])
    seeds.append(best[1])
P.REACH['F_SP121'] = P.reachable(g, seeds)
spot = dict(i=901, kind='camp', stage='F_SP121', room=0, x=34197.0, y=-299.0, z=-28095.0 - 1200.0, ang=0)
p = None
for sr in (1800.0, 3200.0):
    P.SEARCH = sr
    for r in (650.0, 520.0, 400.0):
        P.FOOT_TRY['camp'] = r
        p = P.place(g, spot)
        if p is not None:
            p['clear_radius'] = r
            break
    if p is not None:
        break
print('bridge camp:', {k: (round(v) if isinstance(v, float) else v) for k, v in p.items() if k in ('x', 'y', 'z', 'slope', 'moved', 'clear_radius')})
camps = [c for c in json.load(open('/home/claude/anim/field/camps.json')) if c['i'] != 901]
camps.append({**spot, 'placed': p, 'note': 'bridge camp: escort destination until the bridge is repaired'})
json.dump(camps, open('/home/claude/anim/field/camps.json', 'w'), indent=1)
