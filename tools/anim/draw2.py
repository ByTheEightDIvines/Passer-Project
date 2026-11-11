"""Hip draw v2: rig-style posing.

- Arms and legs are two-bone chains with a real hinge (axes measured from the stock townsfolk
  anims: right elbow bends about local -Z, left elbow and both knees about +Z; bones run along +X).
  Elbows/knees aim at a pole taken from the base animation, so they bend where the game's own
  animations bend them.
- The body twist is shared between the hips (centre) and the chest, with a side-bend toward the
  weapon hip and the head looking at the hilt; feet stay planted and the crouch bends the knees.
- Weapon path: reach -> grip -> straight out of the scabbard (or up through the belt loop) ->
  swing forward -> Rusl's guard pose.
"""
import sys, math, json, os
import numpy as np
from scipy.spatial.transform import Rotation as Rot
ARGV = list(sys.argv)
sys.path.insert(0, '/home/claude/anim')
import hipdraw as H
from hipdraw import Model, smooth, keyed, interp_frame, WAIT, READYB, LOOPED, scene_at, blend
from bck import write_bck, euler_xyz
from render import render
from PIL import Image, ImageDraw

GRAB, CLEAR, MID, READY, NF = 6, 13, 16, 20, 24
HINGE = {'R': -1.0, 'L': 1.0, 'leg': 1.0}   # sign of local Z for the hinge


def norm(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-9 else v


def two_bone(m, rots, root, upper, lower, end, target, pole, sign, end_rot_world=None):
    """Solve upper/lower so `end` reaches target; elbow/knee toward pole. Returns new rots."""
    rots = list(rots)
    W = m.world(rots, root)
    iu, il, ie = m.ix[upper], m.ix[lower], m.ix[end]
    Pu = W[iu][:3, 3]
    L1 = np.linalg.norm(m.rig.rest_loc[il][:3, 3]); L2 = np.linalg.norm(m.rig.rest_loc[ie][:3, 3])
    v = target - Pu; d = np.linalg.norm(v)
    d = min(max(d, abs(L1 - L2) + 1e-3), (L1 + L2) * 0.999)
    a = norm(v)
    b = pole - Pu; b = norm(b - a * (b @ a))
    cos_t = (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d)
    t = math.acos(min(1, max(-1, cos_t)))
    e_dir = a * math.cos(t) + b * math.sin(t)
    E = Pu + e_dir * L1
    T = Pu + a * d
    f_dir = norm(T - E)
    h = np.cross(e_dir, f_dir)
    h = norm(h) if np.linalg.norm(h) > 1e-6 else norm(np.cross(a, b))
    Z = sign * h
    Ru = np.stack([e_dir, np.cross(Z, e_dir), Z], 1)
    p = m.rig.parent[iu]
    rots[iu] = m.anim_rot(iu, W[p][:3, :3].T @ Ru)
    phi = math.acos(min(1, max(-1, e_dir @ f_dir)))
    Rh = Rot.from_rotvec(np.array([0, 0, sign]) * phi).as_matrix()
    rots[il] = m.anim_rot(il, Rh)
    Rl = Ru @ Rh
    if end_rot_world is not None:
        rots[ie] = m.anim_rot(ie, Rl.T @ end_rot_world)
    return rots


def world_rot_on(m, rots, root, joint, R_add):
    return H.add_world_rot(m, rots, root, m.ix[joint], R_add)


def ry(deg): return Rot.from_euler('y', math.radians(deg)).as_matrix()


# body keys (degrees / units). twist + = turn toward the left (weapon) hip
TWIST = [(0, 0), (GRAB, 26), (CLEAR - 2, 6), (CLEAR, -10), (READY, 0), (NF - 1, 0)]
LEAN = [(0, 0), (GRAB, 16), (CLEAR, 2), (READY, 0), (NF - 1, 0)]
SIDE = [(0, 0), (GRAB, -9), (CLEAR, 3), (READY, 0), (NF - 1, 0)]
LOOK = [(0, 0), (GRAB - 1, 22), (CLEAR - 3, 10), (CLEAR, 0), (NF - 1, 0)]
CROUCH = [(0, 0), (GRAB, 1.0), (CLEAR, 0.5), (READY, 0.25), (NF - 1, 0.25)]


def base_body(m, S, E, f):
    w = smooth((f - 2) / (READY - 2))
    return blend(S, E, w)


def default_keys(side=1.0):
    if side < 0:   # right-hip draw: small turn toward the right hip, then square up
        return {'ht': [(0, 0.0), (GRAB, 6.0), (CLEAR, -2.0), (READY, 0.0), (NF - 1, 0.0)],
                'tw': [(0, 0), (GRAB, -16), (CLEAR, 4), (READY, 0), (NF - 1, 0)],
                'le': [(0, 0), (GRAB, 12), (CLEAR, 0), (READY, 0), (NF - 1, 0)],
                'sd': [(0, 0), (GRAB, 8), (CLEAR, -2), (READY, 0), (NF - 1, 0)],
                'cr': [(k, v * 8.0) for k, v in CROUCH]}
    return {'ht': [(0, 0.0), (GRAB, -8.0), (CLEAR, 4.0), (READY, 0.0), (NF - 1, 0.0)], 'tw': list(TWIST), 'le': list(LEAN), 'sd': list(SIDE), 'cr': [(k, v * 8.0) for k, v in CROUCH]}


def pose_body(m, S, E, f, crouch=None, keys=None, override=None):
    keys = keys or getattr(m, 'keys', None) or default_keys(getattr(m, 'side', 1.0))
    rots, root = base_body(m, S, E, f)
    Wb = m.world(rots, root)
    feet = {s: Wb[m.ix['foot' + s]].copy() for s in 'LR'}
    knees = {s: Wb[m.ix['leg%s2' % s]][:3, 3] for s in 'LR'}
    ht, tw, le, sd, cr = (keyed(keys[k], f) for k in ('ht', 'tw', 'le', 'sd', 'cr'))
    if override is not None:
        ht, tw, le, sd, cr = override
    # hips take ~35% of the turn and a little of the lean, the chest the rest
    hip = Rot.from_euler('yx', [math.radians(ht), math.radians(0.25 * le)]).as_matrix()
    root = root + np.array([0, -cr, 0])
    rots = list(rots)
    rots[0] = Rot.from_matrix(hip @ rots[0].as_matrix())   # (centre tweak is applied after; the hip turn is about the world)
    root = hip @ root  # pivot about the ground point under the hips
    chest = Rot.from_euler('yxz', [math.radians(tw - ht), math.radians(0.75 * le), math.radians(sd)]).as_matrix()
    rots = world_rot_on(m, rots, root, 'backbone', chest)
    # head looks down at the hilt, then back up at the enemy
    rots = world_rot_on(m, rots, root, 'neck', Rot.from_euler('yx', [math.radians(0.3 * getattr(m, 'side', 1.0) * keyed(LOOK, f)),
                                                                     math.radians(0.8 * keyed(LOOK, f))]).as_matrix())
    # keep feet planted, knees forward like the base pose
    for s in 'LR':
        pole = knees[s] + np.array([0, 0, 25.0])
        rots = two_bone(m, rots, root, 'leg%s1' % s, 'leg%s2' % s, 'foot' + s, feet[s][:3, 3], pole,
                        HINGE['leg'], end_rot_world=feet[s][:3, :3])
    return rots, root


ARM_LEN = lambda m: (np.linalg.norm(m.rig.rest_loc[m.ix['armR2']][:3, 3]) + np.linalg.norm(m.rig.rest_loc[m.ix['handR']][:3, 3]))


def weapon_goal(m, W, f, g, pull, looped):
    sheath_w = W[m.ix['waist']] @ m.sheath
    Tg = np.eye(4); Tg[2, 3] = g
    if f <= GRAB:
        return (sheath_w @ Tg @ np.linalg.inv(m.grip))[:3, 3]
    sp = smooth((f - GRAB) / (CLEAR - GRAB))
    if not looped:
        return (sword_path(m, W, sheath_w, sp) @ np.linalg.inv(m.grip))[:3, 3]
    T = np.eye(4); T[2, 3] = sp * pull
    return (sheath_w @ T @ Tg @ np.linalg.inv(m.grip))[:3, 3]


def sword_path(m, W, sheath_w, sp):
    """tip slides up the scabbard to the mouth; after the first third the hilt swings
    forward across the body, pivoting at the mouth"""
    Zs = sheath_w[:3, 2]; O0 = sheath_w[:3, 3]
    mouth = O0 + Zs * 6.0
    tip = (O0 + Zs * m.zmax) * (1 - sp) + mouth * sp
    neck = W[m.ix['neck']][:3, 3]
    hilt_clear = np.array([W[0][0, 3] - 8.0, neck[1] - 18.0, W[0][2, 3] + 40.0])   # in front of the chest, a little right
    up = -Zs; u = norm(hilt_clear - mouth)
    k = smooth(max(0.0, (sp - 0.3) / 0.7))
    d = Rot.from_rotvec(Rot.align_vectors([u], [up])[0].as_rotvec() * k).apply(up)
    Zw = -norm(d)
    X = norm(sheath_w[:3, 0] - Zw * (sheath_w[:3, 0] @ Zw))
    M = np.eye(4); M[:3, 0], M[:3, 1], M[:3, 2] = X, np.cross(Zw, X), Zw
    M[:3, 3] = tip - Zw * m.zmax
    return M


def solve_keys(m, S, E, g, pull, looped):
    """per fighter: least body motion that lets the right hand reach the weapon at the grab,
    mid-pull and clear moments"""
    L = ARM_LEN(m) * 0.97
    keys = default_keys(m.side)
    sol = {}
    prev = None
    for f in (GRAB, GRAB + 3, CLEAR):
        best = None
        base = tuple(keyed(keys[k], f) for k in ('ht', 'tw', 'le', 'sd', 'cr'))
        for dht in (-16, -8, 0, 8):
            for dtw in (-10, 0, 10, 20):
                for dle in (-8, 0, 8):
                    for dsd in (-6, 0, 6):
                        cand = (base[0] + dht, base[1] + dtw, base[2] + dle, base[3] + dsd, base[4])
                        rots, root = pose_body(m, S, E, f, override=cand)
                        W = m.world(rots, root)
                        tgt = weapon_goal(m, W, f, g, pull, looped)
                        dist = np.linalg.norm(tgt - W[m.ix['armR1']][:3, 3])
                        dist = max(0.0, dist - min(9.0, 0.5 * dist))
                        deficit = max(0.0, dist - L)
                        effort = 0.03 * (abs(dht) * 1.3 + abs(dtw) + abs(dle) + abs(dsd))
                        if prev is not None:
                            effort += 0.01 * sum(abs(a - b) for a, b in zip(cand, prev))
                        sc = deficit * 3.0 + effort
                        if best is None or sc < best[0]:
                            best = (sc, cand, deficit)
        sol[f] = best[1]; prev = best[1]
        m.key_deficit = getattr(m, 'key_deficit', {}); m.key_deficit[f] = best[2]
    for i, k in enumerate(['ht', 'tw', 'le', 'sd', 'cr']):
        keys[k] = [(0, 0.0), (GRAB, sol[GRAB][i]), (GRAB + 3, sol[GRAB + 3][i]), (CLEAR, sol[CLEAR][i]),
                   (READY, [0, 0, 0, 0, 2.0][i]), (NF - 1, [0, 0, 0, 0, 2.0][i])]
    m.key_base = {f: tuple(round(keyed(default_keys(m.side)[k], f), 1) for k in ('ht', 'tw', 'le', 'sd', 'cr')) for f in sol}
    return keys


def shoulder_reach(m, rots, root, side, target, max_deg=16.0):
    """rotate the clavicle so the arm root moves toward the target when the arm is short of it"""
    W = m.world(rots, root)
    js, ja = m.ix['shoulder' + side], m.ix['arm%s1' % side]
    S0 = W[js][:3, 3]; A0 = W[ja][:3, 3]
    L = ARM_LEN(m)
    need = np.linalg.norm(target - A0) - 0.93 * L
    if need <= 0:
        return rots
    u = norm(A0 - S0); v = norm(target - S0)
    ax = np.cross(u, v)
    if np.linalg.norm(ax) < 1e-6:
        return rots
    ang = min(math.radians(max_deg), math.acos(min(1, max(-1, u @ v))), need / max(np.linalg.norm(A0 - S0), 1.0))
    Rw = Rot.from_rotvec(norm(ax) * ang).as_matrix()
    rots = list(rots)
    p = m.rig.parent[js]
    rots[js] = m.anim_rot(js, W[p][:3, :3].T @ Rw @ W[js][:3, :3])
    return rots


def elbow_pole(m, W, side, bias):
    return W[m.ix['arm%s2' % side]][:3, 3] + bias


def draw_frames(m, crouch=8.0):
    S = m.pose_from_bck(WAIT, 0)
    E = m.compensated(m.pose_from_bck(READYB, 0))
    looped = m.weapon in LOOPED
    m.side = 1.0 if (m.world(*S)[m.ix['waist']] @ m.sheath)[0, 3] > 0 else -1.0
    g = 0.0
    if looped:
        W0 = m.world(*S)
        sh0 = W0[m.ix['waist']] @ m.sheath
        zy = sh0[1, 2]
        g = (W0[m.ix['waist']][1, 3] - 6.0 - sh0[1, 3]) / zy if abs(zy) > 0.2 else 0.0
        g = float(min(max(g, 0.0), m.zmax - 14.0))
        pull = g + abs(m.zmin) + 10.0
    else:
        pull = m.zmax - 6.0
    Tg = np.eye(4); Tg[2, 3] = g
    m.grab_offset = g
    m.keys = solve_keys(m, S, E, g, pull, looped)
    m.slide = [0.0] * NF
    frames, weap, reach = [], [], []
    wpn_clear = None
    for f in range(NF):
        rots, root = pose_body(m, S, E, f)
        W = m.world(rots, root)
        sheath_w = W[m.ix['waist']] @ m.sheath
        hand_base = W[m.ix['handR']]
        if f <= GRAB:
            goal_w = sheath_w @ Tg
            hand_goal = interp_frame(hand_base, goal_w @ np.linalg.inv(m.grip), smooth(f / GRAB),
                                     lift=np.array([0, 5.0, 10.0]))
            m.slide[f] = g
            wpn = sheath_w
        elif f <= CLEAR:
            sp = smooth((f - GRAB) / (CLEAR - GRAB))
            if looped:
                T = np.eye(4); T[2, 3] = sp * pull
                wpn = sheath_w @ T
            else:
                wpn = sword_path(m, W, sheath_w, sp)
            wpn_clear = wpn
            hand_goal = wpn @ Tg @ np.linalg.inv(m.grip)
            m.slide[f] = g
        else:
            ready_w = hand_base @ m.grip
            neck = W[m.ix['neck']][:3, 3]
            Zm = norm(np.array([-0.30, 0.35, 0.89]))
            Xm = norm(np.array([0.0, 1.0, 0.0]) - Zm * Zm[1])
            wm = np.eye(4); wm[:3, 0], wm[:3, 1], wm[:3, 2] = Xm, np.cross(Zm, Xm), Zm
            wm[:3, 3] = [-14.0, neck[1] - 34.0, 34.0]
            if f <= MID:
                wpn = interp_frame(wpn_clear, wm, smooth((f - CLEAR) / (MID - CLEAR)))
            elif f < READY:
                wpn = interp_frame(wm, ready_w, smooth((f - MID) / (READY - MID)))
            else:
                wpn = ready_w
            sl = g * (1 - smooth((f - CLEAR) / (READY - 1 - CLEAR)))
            m.slide[f] = sl
            Ts = np.eye(4); Ts[2, 3] = sl
            hand_goal = wpn @ Ts @ np.linalg.inv(m.grip)
        # right arm: elbow out to the side and a little back while reaching across, like the base pose
        crossing = keyed([(0, 0), (GRAB, 1), (CLEAR, 1), (MID, 0), (NF - 1, 0)], f)
        if m.side > 0:
            pole = elbow_pole(m, W, 'R', np.array([-22.0 * crossing, -10.0, -14.0 * crossing]))
        else:   # same-side draw: elbow back and out, like reaching for a holster
            pole = elbow_pole(m, W, 'R', np.array([-12.0 * crossing, -6.0, -20.0 * crossing]))
        rots = shoulder_reach(m, rots, root, 'R', hand_goal[:3, 3])
        rots = two_bone(m, rots, root, 'armR1', 'armR2', 'handR', hand_goal[:3, 3], pole, HINGE['R'],
                        end_rot_world=hand_goal[:3, :3])
        # left hand steadies the scabbard below its mouth while a sword comes out
        if not looped:
            kL = keyed([(0, 0), (GRAB - 1, 1), (CLEAR - 2, 1), (MID, 0), (NF - 1, 0)], f)
            if kL > 0.02:
                W2 = m.world(rots, root)
                hold = (sheath_w @ np.array([0, 0, 16.0, 1]))[:3] + np.array([3.0, 0, -4.0])
                tgt = W2[m.ix['handL']][:3, 3] * (1 - kL) + hold * kL
                poleL = elbow_pole(m, W2, 'L', np.array([14.0, 0, -18.0]) * kL)
                rots = two_bone(m, rots, root, 'armL1', 'armL2', 'handL', tgt, poleL, HINGE['L'],
                                end_rot_world=W2[m.ix['handL']][:3, :3])
        W3 = m.world(rots, root)
        reach.append(float(np.linalg.norm(W3[m.ix['handR']][:3, 3] - hand_goal[:3, 3])))
        if f >= GRAB:
            Ts = np.eye(4); Ts[2, 3] = -m.slide[f]
            wpn = W3[m.ix['handR']] @ m.grip @ Ts
        frames.append((rots, root))
        weap.append(wpn)
    return frames, weap, reach


def gif(m, frames, weap, path, yaws=(-55, 25), W=260, H=330, ms=50):
    a_s = [math.radians(y) for y in yaws]
    scs = [scene_at(m, *frames[f], weap[f]) for f in range(len(frames))]
    bds = []
    for a_ in a_s:
        c, s = math.cos(a_), math.sin(a_)
        R = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
        P = np.concatenate([np.concatenate([t[1] for t in sc.tris]) for sc in scs]) @ R.T
        bds.append((P.min(0), P.max(0)))
    out = []
    for f, sc in enumerate(scs):
        im = Image.new('RGB', (W * len(yaws), H + 20), (46, 46, 52))
        for k, y in enumerate(yaws):
            im.paste(render(sc, y, W=W, H=H, bounds=bds[k]), (k * W, 20))
        ImageDraw.Draw(im).text((8, 4), f'{m.name}  frame {f}', fill=(220, 220, 220))
        out.append(im)
    out = out + [out[-1]] * 8 + [out[0]] * 6   # hold the guard, then reset
    out[0].save(path, save_all=True, append_images=out[1:], duration=ms, loop=0)


def to_bck(m, frames):
    out = []
    for rots, root in frames:
        fr = []
        for j, r in enumerate(rots):
            t = (root / m.ts) if j == 0 else m.rig.rest_loc[j][:3, 3]
            fr.append(([1.0, 1.0, 1.0], euler_xyz(r.as_matrix()), list(t)))
        out.append(fr)
    return write_bck(out, loop=0)


if __name__ == '__main__':
    names = ARGV[1].split(',') if len(ARGV) > 1 else ['MON_a', 'MAS_a', 'MAN_a2', 'MAD_a2', 'MON_a2', 'MAN_c2', 'MAS_a2']
    for n in names:
        m = Model(n)
        frames, weap, reach = draw_frames(m)
        print('   designed:', m.key_base)
        print('   solved:  ', {f: tuple(round(m.keys[k][i][1], 1) for k in ('ht', 'tw', 'le', 'sd', 'cr')) for i, f in ((1, GRAB), (2, GRAB + 3), (3, CLEAR))})
        print(f'{n:7s} {m.weapon:20s} hand-on-hilt miss during contact: {max(reach[GRAB - 1:CLEAR + 1]):.2f}  worst anywhere {max(reach):.2f}')
        gif(m, frames, weap, '/mnt/user-data/outputs/draw_%s.gif' % n)
