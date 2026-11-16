"""Beedle's shop as a room of its own stage: the room (walls included) and the game's props merged into
one model, by day and by night, in game units."""
import sys, numpy as np, hashlib
sys.path[:0] = ['/home/claude/anim', '/home/claude/room', '/home/claude/passer-project/tools/tents']
import scene
from multibmd import write_multi
L = scene.LAY
W, Hw, Hr, ZF, ZB = L['W'], L['Hw'], L['Hr'], L['ZF'], L['ZB']
OUT = sys.argv[1]
def shell(P):
    c = P.mean(0)
    if abs(abs(c[0]) - W / 2) < 6 and np.ptp(P[:, 0]) < 3: return True
    if (abs(c[2] - ZB) < 6 or abs(c[2] - ZF) < 6) and np.ptp(P[:, 2]) < 3: return True
    roofy = Hr - (Hr - Hw) * abs(c[0]) / (W / 2)
    return c[1] > Hw - 5 and abs(c[1] - roofy) < 8
for night in (False, True):
    S, T = scene.build(night, clear_floor=False)
    groups = {}; order = []
    for ti, P, UV in S:
        P = np.asarray(P, float)
        tex = (np.clip(T[ti], 0, 1) * 255).astype(np.uint8)
        if tex.shape[2] == 3: tex = np.dstack([tex, np.full(tex.shape[:2], 255, np.uint8)])
        k = hashlib.md5(tex.tobytes()).hexdigest()
        if k not in groups: groups[k] = (tex, bool(tex[..., 3].min() < 128), []); order.append(k)
        groups[k][2].append((P, np.asarray(UV, float)))
    G = [groups[k] for k in order]
    name = 'shop_room_n' if night else 'shop_room'
    size, npos = write_multi(f'{OUT}/{name}.bmd', G, jointname=name)
    print(name, sum(len(g[2]) for g in G), 'tris', len(G), 'materials', size // 1024, 'KB', npos, 'positions')
