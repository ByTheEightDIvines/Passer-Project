import sys, pickle, json, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, '/home/claude/anim/field')
from navgrid import Grid, CELL
from dzr import actors

G = {st: pickle.load(open('/home/claude/anim/field/grid_%s.pkl' % st, 'rb')) for st in ('F_SP121', 'F_SP122')}
rooms = pickle.load(open('/home/claude/anim/field/rooms.pkl', 'rb'))
R = json.load(open('/home/claude/anim/field/routes.json'))
C = json.load(open('/home/claude/anim/field/camps.json'))
nodes = {int(k): v for k, v in R['nodes'].items()}


def terrain(g, tint):
    h = g.h.copy(); has = h > -1e8
    lo, hi = np.percentile(h[has], 2), np.percentile(h[has], 98)
    v = np.clip((h - lo) / (hi - lo), 0, 1)
    # hillshade
    gy, gx = np.gradient(np.where(has, h, lo))
    shade = np.clip(0.75 + (-gx * 0.6 - gy * 0.4) / 120.0, 0.45, 1.15)
    base = np.array(tint)[None, None, :] * (0.55 + 0.45 * v[..., None]) * shade[..., None]
    road = (g.mat == 1) & has
    base[road] = base[road] * 0.6 + np.array([190, 160, 110]) * 0.4
    blocked = has & ~g.walk
    base[blocked] = base[blocked] * 0.5 + np.array([120, 40, 40]) * 0.5
    img = np.zeros(h.shape + (4,), np.uint8)
    img[..., :3] = np.clip(base, 0, 255); img[..., 3] = has * 255
    return img


def render(bounds, scale, path, label_nodes=True, title=''):
    (x0, z0, x1, z1) = bounds
    W, H = int((x1 - x0) / scale), int((z1 - z0) / scale)
    canvas = Image.new('RGBA', (W, H), (30, 32, 38, 255))
    for st, tint in (('F_SP121', (110, 150, 95)), ('F_SP122', (95, 130, 150))):
        g = G[st]
        img = Image.fromarray(terrain(g, tint), 'RGBA')
        # grid cell (i,j) -> world; place onto canvas
        gw, gh = g.nx * CELL / scale, g.nz * CELL / scale
        img = img.resize((max(1, int(gw)), max(1, int(gh))), Image.NEAREST)
        ox, oz = int((g.x0 - x0) / scale), int((g.z0 - z0) / scale)
        canvas.alpha_composite(img, (ox, oz)) if ox >= 0 and oz >= 0 else canvas.paste(img, (ox, oz), img)
    d = ImageDraw.Draw(canvas)
    P = lambda x, z: ((x - x0) / scale, (z - z0) / scale)
    for e in R['edges']:
        if e['kind'] == 'handoff':
            for leg, col in ((e['leg_a'], (255, 210, 60)), (e['leg_b'], (255, 210, 60))):
                if leg:
                    d.line([P(p[0], p[1]) for p in leg], fill=col, width=3)
            t = e['handoff']['from_trigger']
            d.rectangle([P(t[0], t[2])[0] - 6, P(t[0], t[2])[1] - 6, P(t[0], t[2])[0] + 6, P(t[0], t[2])[1] + 6], outline=(255, 255, 255), width=2)
            continue
        pts = [P(p[0], p[1]) for p in e['points']]
        if e['kind'] == 'bridge':
            d.line(pts, fill=(255, 60, 60), width=2)
            continue
        col = (255, 90, 60) if e.get('trusted_straight') else ((120, 220, 255) if e['kind'] == 'spur' else ((200, 120, 255) if e['kind'] == 'escort' else (255, 235, 120)))
        d.line(pts, fill=col, width=3 if e['kind'] != 'spur' else 2)
    for k, n in nodes.items():
        x, y = P(n['x'], n['z'])
        if n['kind'] == 'broken_bridge':
            d.line([x - 9, y - 9, x + 9, y + 9], fill=(255, 40, 40), width=4); d.line([x - 9, y + 9, x + 9, y - 9], fill=(255, 40, 40), width=4)
            d.text((x + 12, y - 6), 'bridge: valley route opens once repaired', fill=(255, 90, 90))
        if n['kind'] == 'road':
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(255, 255, 255))
            if label_nodes:
                d.text((x + 4, y - 6), str(k), fill=(255, 255, 255))
    for c in C:
        p = c['placed']; x, y = P(p['x'], p['z']); r = p['clear_radius'] / scale
        col = (255, 120, 200) if c['kind'] == 'beedle_tent' else (255, 150, 40)
        d.ellipse([x - max(r, 6), y - max(r, 6), x + max(r, 6), y + max(r, 6)], outline=col, width=3)
        d.text((x + 8, y + 4), ('Beedle' if c['kind'] == 'beedle_tent' else 'camp') + ' ' + str(c['i']), fill=col)
    # gates
    for (st, room), r in rooms.items():
        if r['dzr'] is None:
            continue
        for a in actors(r['dzr']):
            if a['name'] == 'R_Gate':
                x, y = P(a['pos'][0], a['pos'][2])
                d.rectangle([x - 6, y - 6, x + 6, y + 6], fill=(230, 40, 40))
                d.text((x + 8, y - 6), 'gate r%d' % room, fill=(255, 80, 80))
    if title:
        d.text((10, 10), title, fill=(255, 255, 255))
    canvas.convert('RGB').save(path)
    return canvas


if __name__ == '__main__':
    allx = []
    for g in G.values():
        allx += [g.x0, g.x0 + g.nx * CELL]
    render((-121000, -71000, 59000, 116000), 100.0, '/mnt/user-data/outputs/field_routes_map.png',
           title='Dynamic Travelers routes: yellow road, purple = escort-only, closed until the bridge is repaired, blue camp spur, red = trusted straight (bridges/gaps), orange camps, pink Beedle, white boxes stage hand-offs')
