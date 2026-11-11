"""Retarget Rusl (26-joint moi rig) animations onto the 19-joint townsfolk rig.

World-space rotation delta: for target joint j driven by source joint s,
    W_t(j) = W_s_anim(s) . W_s_rest(s)^-1 . W_t_rest(j)
then local = W_t(parent)^-1 . W_t(j). Bone lengths come from the target rest pose
(stock Mspecial anims bake MAN_a2's lengths, so we do the same). The centre translation is
scaled by the hip-height ratio.
"""
import sys, math
import numpy as np
sys.path.insert(0, '/home/claude/anim')
sys.path.insert(0, '/home/claude/ptb/Passer_Travellers_Build/tools')
from bmd import joint_names
from bck import srt_matrix, euler_xyz

# target joint -> source joint
MAP = {
    'center': 'center', 'backbone': 'backbone2', 'neck': 'neck', 'head': 'head',
    'shoulderL': 'shoulderL', 'armL1': 'armL1', 'armL2': 'armL2', 'handL': 'handL',
    'shoulderR': 'shoulderR', 'armR1': 'armR1', 'armR2': 'armR2', 'handR': 'handR',
    'waist': 'waist', 'legL1': 'legL1', 'legL2': 'legL2', 'footL': 'footL',
    'legR1': 'legR1', 'legR2': 'legR2', 'footR': 'footR',
}


def world_mats(parent, locs):
    W = [None] * len(locs)
    for i in range(len(locs)):
        W[i] = locs[i] if parent[i] < 0 else W[parent[i]] @ locs[i]
    return W


def anim_locals(bck, nj, t):
    return [srt_matrix(*bck.sample(j, t)) for j in range(nj)]


class Rig:
    def __init__(self, bmd):
        self.b = bmd
        self.names = joint_names(bmd)
        self.idx = {n: i for i, n in enumerate(self.names)}
        self.parent = bmd.parent
        self.rest_loc = [m.copy() for m in bmd.jloc]
        self.rest_world = world_mats(self.parent, self.rest_loc)


def retarget_frame(src, tgt, src_locals, hip_ratio, override=None, base_locals=None):
    """returns list of (s,r,t) for the target joints.
    override: set of target joint names to take from the source (others from base_locals)."""
    Ws = world_mats(src.parent, src_locals)
    nt = len(tgt.names)
    Wt = [None] * nt
    out = [None] * nt
    if base_locals is not None:
        Wb = world_mats(tgt.parent, base_locals)
    for j, n in enumerate(tgt.names):
        p = tgt.parent[j]
        use_src = override is None or n in override
        if use_src:
            s = src.idx[MAP[n]]
            Rw = Ws[s][:3, :3] @ np.linalg.inv(src.rest_world[s][:3, :3]) @ tgt.rest_world[j][:3, :3]
        else:
            Rw = Wb[j][:3, :3]
        Rp = np.eye(3) if p < 0 else Wt[p][:3, :3]
        Rl = Rp.T @ Rw
        # orthonormalise (rest matrices are pure rotations already; guard numeric drift)
        U, _, Vt = np.linalg.svd(Rl)
        Rl = U @ Vt
        if p < 0:
            if use_src:
                t = src_locals[src.idx['center']][:3, 3] * hip_ratio
            else:
                t = base_locals[j][:3, 3]
        else:
            t = tgt.rest_loc[j][:3, 3]
        M = np.eye(4)
        M[:3, :3] = Rl
        M[:3, 3] = t
        Wt[j] = M if p < 0 else Wt[p] @ M
        out[j] = ([1.0, 1.0, 1.0], euler_xyz(Rl), list(t))
    return out


def frames_to_locals(frame):
    return [srt_matrix(*f) for f in frame]
