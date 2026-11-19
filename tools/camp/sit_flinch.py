"""A flinch for someone sitting: from the townsfolk's own sit (m_sitwait_a / w_sitwait_a), the upper
body jerks back, the head ducks and both arms come up in front, then it settles back into the sit.

The recoil is worked out in world space (lean back about the body's right axis at the backbone,
arms raised about their shoulders) and put back into each joint's own rotation, so it fits whatever
the sit's joints look like. Legs and hips stay as they sit.

usage: sit_flinch.py <Mgeneral.arc> <Wgeneral.arc> <out dir>   -> psr_m_sitflinch.bck, psr_w_sitflinch.bck
"""
import sys, os, math, numpy as np
sys.path.insert(0, '/home/claude/w/anim')
from rarc import rarc
from bck import BCK, srt_matrix, euler_xyz, write_bck

kFrames = 28


def curve(f):
    """0 -> 1 fast (by frame 4), held, eased back to 0 by the end"""
    if f <= 4: return math.sin(f / 4 * math.pi / 2)
    if f <= 11: return 1.0
    t = (f - 11) / (kFrames - 1 - 11)
    return 0.5 + 0.5 * math.cos(min(1.0, t) * math.pi)


def rotX(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def build(base_bck, names, parents, amounts):
    k = BCK(base_bck)
    base = [k.sample(j, 0) for j in range(k.nj)]
    frames = []
    for f in range(kFrames):
        w = curve(f)
        L = [srt_matrix(*b) for b in base]
        for name, ang in amounts:
            if name not in names: continue
            j = names.index(name)
            # world rotation about the joint (the body faces +Z, so -X rotation leans back / raises
            # forward): L' = L * (Pw^-1 R Pw) restricted to rotation
            W = np.eye(4)
            chain = []
            i = j
            while i >= 0:
                chain.append(i); i = parents[i]
            for i in reversed(chain[1:]): W = W @ L[i]
            Pr = W[:3, :3]
            Rl = np.linalg.inv(Pr) @ rotX(math.radians(ang * w)) @ Pr
            L[j] = L[j].copy()
            L[j][:3, :3] = Rl @ L[j][:3, :3]
        frame = []
        for j in range(k.nj):
            s = base[j][0]
            R3 = L[j][:3, :3] @ np.diag(1.0 / np.array(s))
            frame.append((s, list(euler_xyz(R3)), base[j][2]))
        frames.append(frame)
    return write_bck(frames, loop=0)


RECOIL = [('backbone', -16.0), ('neck', 8.0), ('head', 10.0), ('armL1', -55.0), ('armR1', -55.0),
          ('armL2', -35.0), ('armR2', -35.0)]


def skeleton(bmd_bytes):
    from bmd import BMD, joint_names
    m = BMD(bmd_bytes)
    return joint_names(m), list(m.parent)


if __name__ == '__main__':
    import zipfile
    mg, wg, dusk, out = sys.argv[1:5]
    os.makedirs(out, exist_ok=True)
    z = zipfile.ZipFile(dusk)
    def body(arc):
        r = rarc(z.read('overlay/res/Object/%s.arc' % arc))
        return [v for k, v in r.items() if k.endswith('.bmd')][0]
    for tag, gen, anim, arc in (('m', mg, 'bck/m_sitwait_a.bck', 'P_MON_a'), ('w', wg, 'bck/w_sitwait_a.bck', 'P_WGN_a2')):
        names, parents = skeleton(body(arc))
        data = build(rarc(open(gen, 'rb').read())[anim], names, parents, RECOIL)
        open(os.path.join(out, 'psr_%s_sitflinch.bck' % tag), 'wb').write(data)
        print('psr_%s_sitflinch.bck' % tag, len(names), 'joints')
