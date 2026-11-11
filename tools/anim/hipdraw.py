"""Hand-keyed hip draw / sheathe for each fighter passer, solved with arm IK.

Keys (30 fps):  0 idle (m_wait_a) -> 6 right hand grips the weapon at the left hip (torso twisted,
knees bent, left hand steadying the scabbard) -> 11 weapon clear of the sheath/loop -> 18 ready
pose (Rusl's converted s_wait). Sheathe is the same path played back with its own timing.
Writes one BCK per model; centre translation stored in MAN_a2 units (the game multiplies it by
the passer's translate scale, other joints use the model's own bone lengths).
"""
import sys, math, json
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation as Rot, Slerp
sys.argv = sys.argv[:1] if __name__ != '__main__' else sys.argv
sys.path.insert(0, '/home/claude/anim')
sys.path.insert(0, '/home/claude/ptb/Passer_Travellers_Build/tools')
from rarc import rarc
from bmd import BMD, joint_names
from bck import BCK, srt_matrix, euler_xyz, write_bck
from retarget import Rig, world_mats, anim_locals
from pose import posed_tris, Scene, sheet
from render import render

BASE = '/mnt/user-data/uploads/New Modding/Rom Dump/files/res/Object/'
PP = '/mnt/user-data/outputs/pp/obj/'
import os as _os
_NEWG = '/home/claude/anim/out/gear/'
GEAR = json.load(open(_NEWG + 'gear_table.json' if _os.path.exists(_NEWG + 'gear_table.json') else '/home/claude/ptb/Passer_Travellers_Build/build/gear/gear_table.json'))
GEAR_ARC = _NEWG + 'PsrGear.arc' if _os.path.exists(_NEWG + 'PsrGear.arc') else PP + 'PsrGear.arc'
TSCALE = {'MON_a': (1, .95, 1), 'MAS_a': (1, .78816104, 1), 'MAN_a2': (1, 1, 1),
          'MAD_a2': (1, 1.1983438, 2.173357), 'MON_a2': (1, .95, 1), 'MAN_c2': (1, 1.0820773, 1),
          'MAS_a2': (1, .78816104, 1), 'WAN_b2': (1, 1.0529536, 1)}
LOOPED = {'knob_mace', 'flanged_mace', 'trail_hatchet', 'bearded_travel_axe'}  # hang head-up through a loop
GRAB, CLEAR, MID, READY, NF = 6, 13, 16, 20, 24


def arcf(path, name):
    r = rarc(open(path, 'rb').read())
    return [r[k] for k in r if k.endswith(name)][0]


def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def rusl_grip():
    b = BMD(arcf(BASE + 'Moi.arc', 'moi_swa.bmd'))
    P = np.concatenate([t[1] for t in b.tris])
    tip = P[P[:, 2] < -90].mean(0)
    guard = P[(P[:, 2] > -12) & (P[:, 2] < -4)].mean(0)
    d = tip - guard; d /= np.linalg.norm(d)
    x = np.array([1.0, 0, 0]); x -= d * (x @ d); x /= np.linalg.norm(x)
    G = np.eye(4); G[:3, 0], G[:3, 1], G[:3, 2] = x, np.cross(d, x), d
    G[:3, 3] = guard - d * 5.0
    return G


MOI = Rig(BMD(arcf(BASE + 'Moi.arc', 'moi.bmd')))
G_RUSL = rusl_grip()
WAIT = BCK(arcf(BASE + 'Mgeneral.arc', 'm_wait_a.bck'))
READYB = BCK(open('/home/claude/anim/out/psr_s_wait.bck', 'rb').read())


class Model:
    def __init__(self, name):
        self.name = name
        self.b = BMD(arcf(PP + 'P_%s.arc' % name, '%s.bmd' % name.lower()))
        self.rig = Rig(self.b)
        self.names = self.rig.names
        self.ix = self.rig.idx
        self.ts = np.array(TSCALE[name], float)
        g = GEAR[name]
        self.weapon = g['weapon']
        self.sheath = np.array(g['sheath_mtx_in_waist'])
        gear = rarc(open(GEAR_ARC, 'rb').read())
        self.wpn = BMD(gear['bmdr/wpn_%s.bmd' % self.weapon])
        self.pack = BMD(gear['bmdr/' + g['pack']['file']])
        self.belt = BMD(gear['bmdr/' + g['belt']['file']]) if g.get('belt') else None
        Pw = np.concatenate([t[1] for t in self.wpn.tris])
        self.zmin, self.zmax = Pw[:, 2].min(), Pw[:, 2].max()
        C = np.eye(4)
        C[:3, :3] = self.rig.rest_world[self.ix['handR']][:3, :3].T @ MOI.rest_world[MOI.idx['handR']][:3, :3]
        self.grip = C @ G_RUSL  # weapon frame in handR frame (Rusl's fist), replaced by the fitted grip
        import os
        gf = '/home/claude/anim/out/hip/grips_fit.json'
        if os.path.exists(gf) and not os.environ.get('RUSL_GRIP'):
            fits = json.load(open(gf))
            if name in fits:
                self.grip = np.array(fits[name])

    # pose = (rot[n] as Rotation, root translation in game units)
    def pose_from_bck(self, bck, t):
        rots, root = [], None
        for j in range(len(self.names)):
            jj = j if j < bck.nj else None
            if jj is None:
                rots.append(Rot.from_matrix(self.rig.rest_loc[j][:3, :3]))
                continue
            s, r, tr = bck.sample(jj, t)
            rots.append(Rot.from_matrix(srt_matrix([1, 1, 1], r, [0, 0, 0])[:3, :3]))
            if j == 0:
                root = np.array(tr) * self.ts
        return rots, root

    def locals_(self, rots, root):
        out = []
        for j, r in enumerate(rots):
            M = np.eye(4); M[:3, :3] = r.as_matrix()
            M[:3, 3] = root if j == 0 else self.rig.rest_loc[j][:3, 3]
            out.append(M)
        return out

    def world(self, rots, root):
        return world_mats(self.rig.parent, self.locals_(rots, root))


def blend(p0, p1, w):
    rots = [Slerp([0, 1], Rot.concatenate([a, b]))(w) for a, b in zip(p0[0], p1[0])]
    return rots, p0[1] * (1 - w) + p1[1] * w


def add_world_rot(m, rots, root, joint, R_add):
    """rotate joint's world orientation by R_add (world space), keep children's locals"""
    W = m.world(rots, root)
    p = m.rig.parent[joint]
    Wp = W[p][:3, :3]
    new = R_add @ W[joint][:3, :3]
    rots = list(rots)
    rots[joint] = Rot.from_matrix(Wp.T @ new)
    return rots


ARM_R = ['shoulderR', 'armR1', 'armR2', 'handR']
ARM_L = ['shoulderL', 'armL1', 'armL2', 'handL']


def ik(m, rots, root, chain, goal, prev=None, rot_w=25.0, reg=6.0, pos_only=False):
    ids = [m.ix[n] for n in chain]
    q0 = np.concatenate([rots[i].as_rotvec() for i in ids])
    qp = np.concatenate([prev[i].as_rotvec() for i in ids]) if prev is not None else q0
    eff = ids[-1]

    def res(q):
        rr = list(rots)
        for k, i in enumerate(ids):
            rr[i] = Rot.from_rotvec(q[3 * k:3 * k + 3])
        W = m.world(rr, root)
        H = W[eff]
        e = list(H[:3, 3] - goal[:3, 3])
        if not pos_only:
            dR = Rot.from_matrix(goal[:3, :3] @ H[:3, :3].T).as_rotvec()
            e += list(dR * rot_w)
        e += list((q - q0) * reg * 0.5) + list((q - qp) * reg)
        return np.array(e)

    sol = least_squares(res, qp, method='lm', max_nfev=400)
    rr = list(rots)
    for k, i in enumerate(ids):
        rr[i] = Rot.from_rotvec(sol.x[3 * k:3 * k + 3])
    W = m.world(rr, root)
    err = np.linalg.norm(W[eff][:3, 3] - goal[:3, 3])
    return rr, err


def interp_frame(A, B, w, lift=None):
    M = np.eye(4)
    M[:3, :3] = Slerp([0, 1], Rot.from_matrix([A[:3, :3], B[:3, :3]]))(w).as_matrix()
    M[:3, 3] = A[:3, 3] * (1 - w) + B[:3, 3] * w
    if lift is not None:
        M[:3, 3] += lift * math.sin(math.pi * w)
    return M


def sword_draw(m, W, sheath_w, sp):
    """tip slides up the scabbard to its mouth while the hilt swings forward across the body"""
    Zs = sheath_w[:3, 2]                      # blade direction while sheathed (down)
    O0 = sheath_w[:3, 3]
    mouth = O0 + Zs * 6.0
    T = O0 + Zs * m.zmax                      # tip, deep in the scabbard
    tip = T * (1 - sp) + mouth * sp
    neck = W[m.ix['neck']][:3, 3]
    hilt_clear = np.array([-6.0, neck[1] - 16.0, 42.0])
    u = hilt_clear - mouth; u /= np.linalg.norm(u)
    up = -Zs
    k = smooth(max(0.0, (sp - 0.25) / 0.75))
    Rup = Rot.align_vectors([u], [up])[0]  # rotation taking up -> u
    d = Rot.from_rotvec(Rup.as_rotvec() * k).apply(up)
    Zw = -d / np.linalg.norm(d)
    X = sheath_w[:3, 0] - Zw * (sheath_w[:3, 0] @ Zw); X /= np.linalg.norm(X)
    M = np.eye(4)
    M[:3, 0], M[:3, 1], M[:3, 2] = X, np.cross(Zw, X), Zw
    M[:3, 3] = tip - Zw * m.zmax
    return M


def keyed(keys, f):
    for (f0, v0), (f1, v1) in zip(keys, keys[1:]):
        if f0 <= f <= f1:
            return v0 + (v1 - v0) * smooth((f - f0) / (f1 - f0))
    return keys[-1][1]


class BodyPts:
    """rigid-skinned body points (dominant joint) for fast blade-vs-body checks"""
    def __init__(self, m):
        ex = {m.ix[j] for j in ['armR2', 'handR']}
        P, J = [], []
        for (mi, T, uv), js in zip(m.b.tris, m.b.trij):
            for p, j in zip(T, js):
                if j in ex: continue
                inv = np.linalg.inv(m.rig.rest_world[j])
                P.append(inv[:3, :3] @ p + inv[:3, 3]); J.append(j)
        self.P = np.array(P); self.J = np.array(J)
        self.m = m

    def hits(self, W, G, r=2.2, exposed=None):
        V = np.einsum('nij,nj->ni', np.array([W[j][:3, :3] for j in self.J]), self.P) + np.array([W[j][:3, 3] for j in self.J])
        o = G[:3, 3] + G[:3, 2] * 8; d = G[:3, 2]
        L = (self.m.zmax - 8) if exposed is None else max(0.0, min(self.m.zmax - 8, exposed - 2))
        t = np.clip((V - o) @ d, 0, L)
        return int((np.linalg.norm(V - (o + np.outer(t, d)), axis=1) < r).sum())


TWIST = [(0, 0), (GRAB, 22), (CLEAR, -14), (READY, 0), (NF - 1, 0)]
LEAN = [(0, 0), (GRAB, 12), (CLEAR, -6), (READY, 0), (NF - 1, 0)]
DROP = [(0, 0), (GRAB, 1.0), (CLEAR, 0.4), (READY, 0), (NF - 1, 0)]


def body_pose(m, S, E, f, drop, dtw=0.0, dle=0.0):
    w = smooth((f - 2) / (READY - 2))
    rots, root = blend(S, E, w)
    root = root + np.array([0, -drop * keyed(DROP, f), 0])
    tw = keyed(TWIST, f) + dtw; le = keyed(LEAN, f) + dle
    rots = add_world_rot(m, rots, root, m.ix['backbone'],
                         Rot.from_euler('yx', [math.radians(tw), math.radians(le)]).as_matrix())
    return rots, root


def draw_frames(m, drop=7.0, lean=14.0, twist=24.0):
    """returns per-frame (rots, root), weapon world frames, in-hand flags, hand errors"""
    S = m.pose_from_bck(WAIT, 0)
    E = m.pose_from_bck(READYB, 0)
    looped = m.weapon in LOOPED
    if not hasattr(m, '_bp'):
        m._bp = BodyPts(m)
    m.slide = [0.0] * NF
    g = 0.0
    if looped:
        W0 = m.world(*S)
        sh0 = W0[m.ix['waist']] @ m.sheath
        waist_y = W0[m.ix['waist']][1, 3]
        zy = sh0[1, 2]
        g = (waist_y - 6.0 - sh0[1, 3]) / zy if abs(zy) > 0.2 else 0.0
        g = float(min(max(g, 0.0), m.zmax - 14.0))
        pull = g + abs(m.zmin) + 10.0
    else:
        pull = m.zmax - 6.0          # tip reaches the scabbard mouth
    Tg = np.eye(4); Tg[2, 3] = g
    m.grab_offset = g
    frames, weap, inhand, errs = [], [], [], []
    prev = None
    wpn_clear = None
    dtw = dle = 0.0
    for f in range(NF):
        if GRAB < f <= CLEAR + 1 and not looped:
            # let the torso turn/lean a little more or less so the blade clears the chest
            best = None
            for a in np.arange(-12, 13, 4):
                for b in (-8, -4, 0, 4):
                    rots, root = body_pose(m, S, E, f, drop, a, b)
                    W = m.world(rots, root)
                    sw = W[m.ix['waist']] @ m.sheath
                    T = np.eye(4); T[2, 3] = -smooth((f - GRAB) / (CLEAR - GRAB)) * pull
                    sc = m._bp.hits(W, sw @ T, exposed=-T[2, 3]) + 0.15 * (abs(a) + abs(b)) + 0.3 * (abs(a - dtw) + abs(b - dle))
                    if best is None or sc < best[0]:
                        best = (sc, a, b)
            dtw, dle = best[1], best[2]
        elif f > CLEAR + 1:
            dtw *= 0.6; dle *= 0.6
        rots, root = body_pose(m, S, E, f, drop, dtw, dle)
        W = m.world(rots, root)
        sheath_w = W[m.ix['waist']] @ m.sheath
        hand_now = W[m.ix['handR']]
        if f <= GRAB:
            grab_goal = sheath_w @ Tg @ np.linalg.inv(m.grip)
            goal = interp_frame(hand_now, grab_goal, smooth(f / GRAB), lift=np.array([0, 6.0, 8.0]))
            wpn = sheath_w
        elif f <= CLEAR:
            sp = smooth((f - GRAB) / (CLEAR - GRAB))
            T = np.eye(4); T[2, 3] = (sp * pull) if looped else (-sp * pull)
            wpn = sheath_w @ T                  # straight out along the scabbard / up through the loop
            goal = wpn @ Tg @ np.linalg.inv(m.grip)
            wpn_clear = wpn
            m.slide[f] = g
        else:
            ready_w = hand_now @ m.grip         # Rusl's guard grip on the blended body
            neck = W[m.ix['neck']][:3, 3]
            Zm = np.array([-0.30, 0.35, 0.89]); Zm /= np.linalg.norm(Zm)
            Xm = np.array([0.0, 1.0, 0.0]) - Zm * Zm[1]; Xm /= np.linalg.norm(Xm)
            wm = np.eye(4); wm[:3, 0], wm[:3, 1], wm[:3, 2] = Xm, np.cross(Zm, Xm), Zm
            wm[:3, 3] = [-14.0, neck[1] - 34.0, 34.0]   # blade swung out in front, point toward the enemy
            if f <= MID:
                wpn = interp_frame(wpn_clear, wm, smooth((f - CLEAR) / (MID - CLEAR)))
            elif f < READY:
                wpn = interp_frame(wm, ready_w, smooth((f - MID) / (READY - MID)))
            else:
                wpn = ready_w
            Ts = np.eye(4); Ts[2, 3] = g * (1 - smooth((f - CLEAR) / (READY - 1 - CLEAR)))
            m.slide[f] = float(Ts[2, 3])
            goal = wpn @ Ts @ np.linalg.inv(m.grip)
        contact = GRAB - 1 <= f <= CLEAR
        rr, err = ik(m, rots, root, ARM_R, goal, prev, reg=(1.0 if contact else 3.0))
        if contact and err > 0.4:
            rr, err = ik(m, rr, root, ['backbone'] + ARM_R, goal, prev, reg=0.6)
        if not looped:   # left hand holds the scabbard mouth while the blade comes out
            hz, hx, hb, rel = getattr(m, 'lhold', (18.0, 2.0, -3.0, CLEAR))
            kL = keyed([(0, 0), (GRAB - 1, 1), (rel, 1), (rel + 4, 0), (NF - 1, 0)], f)
            if kL > 0.03:
                Wc = m.world(rr, root)
                mouth = (sheath_w @ np.array([0, 0, 6.0, 1]))[:3]
                gl = np.eye(4)
                hold = (sheath_w @ np.array([0, 0, hz, 1]))[:3] + np.array([hx, 0, hb])   # on the scabbard, below the mouth
                gl[:3, 3] = Wc[m.ix['handL']][:3, 3] * (1 - kL) + hold * kL
                rr, _ = ik(m, rr, root, ARM_L, gl, prev, pos_only=True, reg=3.0)
        prev = rr
        frames.append((rr, root))
        W2 = m.world(rr, root)
        if f <= GRAB:
            m.slide[f] = g
        if f >= GRAB:
            Ts = np.eye(4); Ts[2, 3] = -m.slide[f]
            wpn = W2[m.ix['handR']] @ m.grip @ Ts
        weap.append(wpn)
        inhand.append(f >= GRAB)
        errs.append(err)
    m.hits = [m._bp.hits(m.world(*frames[f]), weap[f],
                         exposed=(None if f > CLEAR or looped else smooth((f - GRAB) / (CLEAR - GRAB)) * pull)) if f > GRAB else 0
              for f in range(NF)]
    return frames, weap, inhand, errs


def to_bck(m, frames, loop=0):
    out = []
    for rots, root in frames:
        fr = []
        for j, r in enumerate(rots):
            t = (root / m.ts) if j == 0 else m.rig.rest_loc[j][:3, 3]
            fr.append(([1.0, 1.0, 1.0], euler_xyz(r.as_matrix()), list(t)))
        out.append(fr)
    return write_bck(out, loop=loop)


SHEATHE_MAP = [(0, READY), (8, CLEAR), (15, GRAB), (25, 0)]  # sheathe frame -> draw frame


def sheathe_src(t):
    for (a, fa), (b, fb) in zip(SHEATHE_MAP, SHEATHE_MAP[1:]):
        if a <= t <= b:
            return fa + (fb - fa) * smooth((t - a) / (b - a))
    return 0


def scene_at(m, rots, root, wpn_world):
    tr, W = posed_tris(m.b, m.locals_(rots, root))
    s = Scene(); s.add(m.b, tr)
    s.add(m.pack, list(m.pack.tris), W[m.ix['backbone']])
    if m.belt is not None:
        s.add(m.belt, list(m.belt.tris), W[m.ix['waist']])
    s.add(m.wpn, list(m.wpn.tris), wpn_world)
    return s


if __name__ == '__main__':
    import os
    os.makedirs('/home/claude/anim/out/hip', exist_ok=True)
    models = sys.argv[1].split(',') if len(sys.argv) > 1 else ['MON_a', 'MAS_a', 'MAN_a2', 'MAD_a2', 'MON_a2', 'MAN_c2', 'MAS_a2']
    picks = [0, 4, 6, 8, 10, 13, 16, 20]
    META = {}
    for name in models:
        m = Model(name)
        for drop, lean in [(7, 14), (11, 18), (15, 22), (19, 26)]:
            frames, weap, inhand, errs = draw_frames(m, drop, lean)
            if max(errs[GRAB - 1:CLEAR + 1]) < 0.5:
                break
        if m.weapon not in LOOPED and sum(m.hits) > 0:
            best = (sum(m.hits), (18.0, 2.0, -3.0, CLEAR))
            for hz in (14.0, 22.0, 30.0):
                for hx in (2.0, 7.0):
                    for hb in (-3.0, -9.0):
                        for rel in (CLEAR - 3, CLEAR):
                            m.lhold = (hz, hx, hb, rel)
                            draw_frames(m, drop, lean)
                            sc = sum(m.hits) + 0.01 * hz
                            if sc < best[0]:
                                best = (sc, m.lhold)
            m.lhold = best[1]
            frames, weap, inhand, errs = draw_frames(m, drop, lean)
            print('   left hand hold', m.lhold)
        print(f'   crouch {drop} lean {lean}  grab offset {m.grab_offset:.1f}')
        META[name] = {'weapon': m.weapon, 'grab_frame': GRAB, 'draw_frames': NF, 'sheathe_frames': SHEATHE_MAP[-1][0] + 1,
                      'sheathe_release_frame': 15, 'crouch': [drop, lean], 'lhold': list(getattr(m, 'lhold', [])), 'slide_draw': [round(x, 2) for x in m.slide]}
        open('/home/claude/anim/out/hip/psr_draw_%s.bck' % name.lower(), 'wb').write(to_bck(m, frames))
        # sheathe: resample the draw path (weapon returns to the sheath at draw-frame GRAB)
        sh = []
        for t in range(SHEATHE_MAP[-1][0] + 1):
            u = sheathe_src(t)
            a, b = int(math.floor(u)), min(int(math.floor(u)) + 1, NF - 1)
            ww = u - a
            sh.append(blend(frames[a], frames[b], ww))
        open('/home/claude/anim/out/hip/psr_sheathe_%s.bck' % name.lower(), 'wb').write(to_bck(m, sh))
        print(f'{name:7s} {m.weapon:20s} contact err {max(errs[GRAB-1:CLEAR+1]):.2f}  blade-in-body hits {sum(m.hits)} (worst {max(m.hits)})')
        ims, labs = [], []
        for yaw in (-40, 50):
            a_ = math.radians(yaw); c, s_ = math.cos(a_), math.sin(a_)
            R = np.array([[c, 0, s_], [0, 1, 0], [-s_, 0, c]])
            scs = [scene_at(m, *frames[f], weap[f]) for f in picks]
            P = np.concatenate([np.concatenate([t[1] for t in sc.tris]) for sc in scs]) @ R.T
            bd = (P.min(0), P.max(0))
            for f, sc in zip(picks, scs):
                ims.append(render(sc, yaw, W=210, H=300, bounds=bd))
                labs.append(f'{name} draw f{f}')
        sheet(ims, labs, len(picks), '/mnt/user-data/outputs/hipdraw_%s.png' % name)
    old = json.load(open('/home/claude/anim/out/hip/draw_meta.json')) if os.path.exists('/home/claude/anim/out/hip/draw_meta.json') else {}
    old.update(META)
    json.dump(old, open('/home/claude/anim/out/hip/draw_meta.json', 'w'), indent=1)
