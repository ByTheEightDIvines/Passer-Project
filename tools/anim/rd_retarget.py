"""The Bulblin archer's (E_rd) on-foot bow animations refitted to the townsfolk men's 19-joint rig
(passer 8, MAN_a2). A straight copy of joint rotations carries the Bulblin's hunch (he leans back, hips
forward, head tipped) and its longer arms (his hands land mid-chest). So:
  - hips, chest, neck, head: the man's own upright pose, turned only as far as the Bulblin turns (its yaw)
  - legs: the Bulblin's world rotations (stance and steps), the hips following its hips, scaled by height
  - arms: placed by reach. Each hand goes where the Bulblin's hand is from its shoulder, in its chest's
    turned frame, scaled by arm length (Bulblin upper arm + forearm 88, the man's 67.7); the elbow bends
    the way the Bulblin's does (two-bone IK). The hands keep the Bulblin's world rotations (the grip:
    the bow and arrow sit as they do in its hands).
Usage: python3 -I rd_retarget.py E_rd.arc P_MAN_a2.arc OUT_DIR"""
import sys, os, numpy as np
sys.path[:0] = ['/home/claude/work', '/home/claude/anim']
from rarc import rarc
from bmd import BMD, joint_names
from bck import BCK, srt_matrix, write_bck, euler_xyz

DELTA = {   # target joint -> Bulblin joint whose world rotation (from rest) it copies
    'legL1': 'momoL', 'legL2': 'suneL1', 'footL': 'asiL', 'legR1': 'momorR', 'legR2': 'suneR1', 'footR': 'asiR',
    'handL': 'handL', 'handR': 'handR', 'armL1': 'armL1', 'armL2': 'armL2', 'armR1': 'armR1', 'armR2': 'armR2',
}
YAW = {'center': 'kosi', 'waist': 'hip1', 'backbone': 'mune2', 'neck': 'kubi', 'head': 'head'}
ANIMS = [('rd_shoot_ready', 'bow_draw', 0), ('rd_shoot_wait', 'bow_aim', 2), ('rd_shoot', 'bow_shoot', 0)]
# From the nock to the release the hands take an archer's line instead (the Bulblin's draw hand lands at
# chin height on it, mid-chest on a man, hiding the arrow along the bow arm): the bow hand straight out from
# its shoulder along the Bulblin's aim, the draw hand anchored under the chin. Weight per frame:
def archer_weight(anim, f):
    ramp = lambda a, b: min(1.0, max(0.0, (f - a) / float(b - a)))
    return {'bow_draw': ramp(12, 22), 'bow_aim': 1.0, 'bow_shoot': 1.0 - ramp(2, 7)}[anim]
ANCHOR = (4.0, 50.0, 10.0)   # the chin, in the chest's turned frame from the backbone joint: right, up, forward

def world(parent, locs):
    W = [None] * len(locs)
    for i in range(len(locs)):
        W[i] = locs[i] if parent[i] < 0 else W[parent[i]] @ locs[i]
    return W
def roty(a):
    c, s = np.cos(a), np.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def yaw_of(D):   # the turn about world up of a world rotation delta (its forward, flattened)
    f = D @ np.array([0.0, 0.0, 1.0]); return np.arctan2(f[0], f[2])
def align(a, b):  # smallest rotation taking direction a to direction b
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b); v = np.cross(a, b); c = float(a @ b)
    if np.linalg.norm(v) < 1e-9: return np.eye(3) if c > 0 else -np.eye(3)
    K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + K + K @ K * (1.0 / (1.0 + c))

class Rig:
    def __init__(self, b):
        self.names = joint_names(b); self.idx = {n: i for i, n in enumerate(self.names)}
        self.parent = b.parent; self.rest_loc = [m.copy() for m in b.jloc]; self.rest_world = world(self.parent, self.rest_loc)
    def p(self, W, n): return W[self.idx[n]][:3, 3]

def retarget(src, tgt, bck, anim):
    hs = src.rest_world[src.idx['kosi']][:3, 3]
    ht = tgt.rest_world[0][:3, 3]
    ratio = ht[1] / hs[1]
    s_reach = {sd: np.linalg.norm(src.p(src.rest_world, 'arm%s2' % sd) - src.p(src.rest_world, 'arm%s1' % sd)) +
                   np.linalg.norm(src.p(src.rest_world, 'hand' + sd) - src.p(src.rest_world, 'arm%s2' % sd)) for sd in 'LR'}
    seg = {sd: (np.linalg.norm(tgt.rest_loc[tgt.idx['arm%s2' % sd]][:3, 3]), np.linalg.norm(tgt.rest_loc[tgt.idx['hand' + sd]][:3, 3])) for sd in 'LR'}
    frames = []
    for f in range(bck.dur + 1):
        Ws = world(src.parent, [srt_matrix(*bck.sample(j, f)) for j in range(bck.nj)])
        dS = lambda n: Ws[src.idx[n]][:3, :3] @ np.linalg.inv(src.rest_world[src.idx[n]][:3, :3])
        chest_yaw_s = yaw_of(dS('mune2'))
        # world rotations: the upright parts by yaw, the rest by the Bulblin's deltas
        Rw = {}
        for n in tgt.names:
            j = tgt.idx[n]
            if n in YAW: Rw[n] = roty(yaw_of(dS(YAW[n]))) @ tgt.rest_world[j][:3, :3]
            elif n in DELTA: Rw[n] = dS(DELTA[n]) @ tgt.rest_world[j][:3, :3]
        # positions so far (for the arms' anchors): build the chain with what we have
        def build(Rw):
            Wt = [None] * len(tgt.names); out = []
            for j, n in enumerate(tgt.names):
                p = tgt.parent[j]
                Rp = np.eye(3) if p < 0 else Wt[p][:3, :3]
                R = Rw[n] if n in Rw else Rp @ tgt.rest_loc[j][:3, :3]   # shoulders follow the chest as at rest
                Rl = Rp.T @ R; U, _, Vt = np.linalg.svd(Rl); Rl = U @ Vt
                t = ht + (Ws[src.idx['kosi']][:3, 3] - hs) * ratio if p < 0 else tgt.rest_loc[j][:3, 3]
                M = np.eye(4); M[:3, :3] = Rl; M[:3, 3] = t
                Wt[j] = M if p < 0 else Wt[p] @ M
                out.append((Rl, t))
            return Wt, out
        Wt, _ = build(Rw)
        chest_yaw_t = yaw_of(Wt[tgt.idx['backbone']][:3, :3] @ np.linalg.inv(tgt.rest_world[tgt.idx['backbone']][:3, :3]))
        Ry_s, Ry_t = roty(chest_yaw_s), roty(chest_yaw_t)
        w = archer_weight(anim, f)
        aim = src.p(Ws, 'handL') - src.p(Ws, 'handR'); aim = Ry_t @ (Ry_s.T @ aim); aim /= np.linalg.norm(aim)
        if w > 0.0:
            # looking down the arrow (the Bulblin turns its head to its target in the game, aim_type 4):
            # the neck takes half the turn, the head the rest
            look = np.arctan2(aim[0], aim[2])
            for n, part in (('neck', 0.5), ('head', 1.0)):
                cur = yaw_of(Rw[n] @ np.linalg.inv(tgt.rest_world[tgt.idx[n]][:3, :3]))
                dy = (look - cur + np.pi) % (2 * np.pi) - np.pi
                Rw[n] = roty(dy * part * w) @ Rw[n]
            Wt, _ = build(Rw)
        chin = Wt[tgt.idx['backbone']][:3, 3] + Ry_t @ np.array([-ANCHOR[0], ANCHOR[1], ANCHOR[2]])
        for sd in 'LR':
            A_s, E_s, H_s = (src.p(Ws, 'arm%s1' % sd), src.p(Ws, 'arm%s2' % sd), src.p(Ws, 'hand' + sd))
            a, b = seg[sd]; k = (a + b) / s_reach[sd]
            A = Wt[tgt.idx['arm%s1' % sd]][:3, 3]
            T = A + Ry_t @ (Ry_s.T @ (H_s - A_s)) * k
            if w > 0.0:
                line = A + aim * (a + b) * 0.97 if sd == 'L' else chin
                T = T + (line - T) * w
            P = A + Ry_t @ (Ry_s.T @ (E_s - A_s)) * k           # where its elbow points
            d = T - A; L = np.linalg.norm(d)
            if L > a + b - 0.05: T = A + d / L * (a + b - 0.05); d = T - A; L = a + b - 0.05
            u = d / L
            x = (a * a - b * b + L * L) / (2 * L); h = np.sqrt(max(a * a - x * x, 0.0))
            pv = (P - A) - u * ((P - A) @ u); pn = np.linalg.norm(pv)
            n_ = pv / pn if pn > 1e-6 else np.cross(u, [0.0, 1.0, 0.0])
            E = A + u * x + n_ * h
            up = 'arm%s1' % sd; fo = 'arm%s2' % sd
            Rw[up] = align(Rw[up][:, 0], E - A) @ Rw[up]          # the bone runs along its local x
            Rw[fo] = align(Rw[fo][:, 0], T - E) @ Rw[fo]
        Wt, out = build(Rw)
        frames.append([([1.0, 1.0, 1.0], euler_xyz(Rl), list(t)) for Rl, t in out])
    return frames

if __name__ == '__main__':
    rd_arc, man_arc, out_dir = sys.argv[1:4]
    rd = rarc(open(rd_arc, 'rb').read())
    get = lambda a, end: [v for k, v in a.items() if k.endswith(end)][0]
    src = Rig(BMD(get(rd, '/rd.bmd')))
    tgt = Rig(BMD(get(rarc(open(man_arc, 'rb').read()), '/man_a2.bmd')))
    os.makedirs(out_dir, exist_ok=True)
    for name, out, loop in ANIMS:
        bck = BCK(get(rd, '/' + name + '.bck'))
        open(f'{out_dir}/psr_m_{out}.bck', 'wb').write(write_bck(retarget(src, tgt, bck, out), loop=loop))
        print(name, '->', out, bck.dur + 1, 'frames')
