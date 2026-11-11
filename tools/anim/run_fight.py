import sys, math, json
import numpy as np
sys.path.insert(0, '/home/claude/anim')
sys.path.insert(0, '/home/claude/ptb/Passer_Travellers_Build/tools')
from rarc import rarc
from bmd import BMD
from bck import BCK, srt_matrix, write_bck
from retarget import Rig, retarget_frame, anim_locals, frames_to_locals, world_mats
from pose import posed_tris, Scene, sheet
from render import render

BASE = '/mnt/user-data/uploads/New Modding/Rom Dump/files/res/Object/'
PP = '/mnt/user-data/outputs/pp/obj/'


def arcf(path, name):
    r = rarc(open(path, 'rb').read())
    return [r[k] for k in r if k.endswith(name)][0]


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    M = np.eye(4); M[1:3, 1:3] = [[c, -s], [s, c]]
    return M


# ---- grip: our weapons are grip-local, blade +Z; Rusl's sword is drawn at handR identity
def rusl_grip():
    b = BMD(arcf(BASE + 'Moi.arc', 'moi_swa.bmd'))
    P = np.concatenate([t[1] for t in b.tris])
    tip = P[P[:, 2] < -90].mean(0)
    guard = P[(P[:, 2] > -12) & (P[:, 2] < -4)].mean(0)
    d = tip - guard; d /= np.linalg.norm(d)
    x = np.array([1.0, 0, 0]); x -= d * (x @ d); x /= np.linalg.norm(x)
    y = np.cross(d, x)
    G = np.eye(4); G[:3, 0], G[:3, 1], G[:3, 2] = x, y, d
    G[:3, 3] = guard - d * 5.0  # our guard sits ~5 units up the blade from the grip origin
    return G


GRIP = rusl_grip()


def load_rigs():
    moi = BMD(arcf(BASE + 'Moi.arc', 'moi.bmd'))
    man_rig = BMD(arcf(BASE + 'MAN_a2.arc', 'man_a2.bmd'))
    return Rig(moi), Rig(man_rig), moi


def retarget_anim(src, tgt, arc, name, hip_ratio):
    a = BCK(arcf(BASE + arc + '.arc', name))
    frames = []
    for f in range(a.dur + 1):
        frames.append(retarget_frame(src, tgt, anim_locals(a, len(src.names), f), hip_ratio))
    return a, frames


if __name__ == '__main__':
    src, tgt, moi = load_rigs()
    hip = tgt.rest_loc[0][1, 3] / src.rest_loc[0][1, 3]
    a, frames = retarget_anim(src, tgt, 'Moi2', 'moi_s_fight_a.bck', hip)
    data = write_bck(frames, loop=a.loop)
    open('/home/claude/anim/out_psr_s_fight_a.bck', 'wb').write(data)

    swa = BMD(arcf(BASE + 'Moi.arc', 'moi_swa.bmd'))
    body = BMD(arcf(PP + 'P_MAN_a2.arc', 'man_a2.bmd'))
    gear = rarc(open(PP + 'PsrGear.arc', 'rb').read())
    pack = BMD(gear['bmdr/pack_man_a2.bmd'])
    belt = BMD(gear['bmdr/belt_man_a2.bmd'])
    wpn = BMD(gear['bmdr/wpn_old_broadsword.bmd'])
    names = tgt.names
    C = np.eye(4)
    C[:3, :3] = tgt.rest_world[tgt.idx['handR']][:3, :3].T @ src.rest_world[src.idx['handR']][:3, :3]
    GRIP_T = C @ GRIP

    picks = [int(x) for x in sys.argv[1].split(',')] if len(sys.argv) > 1 else [0, 4, 6, 9, 13, 18, 21, 25, 31, 38]
    yaw = float(sys.argv[2]) if len(sys.argv) > 2 else 35
    scenes = []
    for f in picks:
        # Rusl
        sl = anim_locals(a, len(src.names), f)
        tr, W = posed_tris(moi, sl)
        s1 = Scene(); s1.add(moi, tr); s1.add(swa, list(swa.tris), W[src.idx['handR']])
        # passer
        tl = frames_to_locals(frames[f])
        tr, W = posed_tris(body, tl)
        s2 = Scene(); s2.add(body, tr)
        s2.add(pack, list(pack.tris), W[names.index('backbone')])
        s2.add(belt, list(belt.tris), W[names.index('waist')])
        s2.add(wpn, list(wpn.tris), W[names.index('handR')] @ GRIP_T)
        scenes.append((f, s1, s2))
    # common bounds
    a_ = math.radians(yaw); c, s = math.cos(a_), math.sin(a_)
    R = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    allp = np.concatenate([np.concatenate([t[1] for t in sc.tris]) for _, s1, s2 in scenes for sc in (s1, s2)]) @ R.T
    bounds = (allp.min(0), allp.max(0))
    print('bounds', bounds)
    ims, labs = [], []
    for row in (1, 2):
        for f, s1, s2 in scenes:
            sc = s1 if row == 1 else s2
            ims.append(render(sc, yaw, W=320, H=230, bounds=bounds))
            labs.append(('Rusl ' if row == 1 else 'passer ') + f'f{f}')
    sheet(ims, labs, len(scenes), '/mnt/user-data/outputs/fight_a_sheet.png')
    print('ok', len(data), 'bytes; grip', GRIP_T.round(3).tolist())
