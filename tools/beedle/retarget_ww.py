"""Beedle's Wind Waker animations (Bs.arc, his own 20-joint skeleton, all made seated) on his game model
(the men's 19 joints: MAN_a's joint frames, Beedle's bone positions; WW backbone1+2 = the game's backbone).

Two sets:
  field ('<name>_up'): his upper body (backbone and up) from the WW animation, his hips and legs from the
         game's standing animation (Mgeneral m_wait_a), so he talks like himself while standing in the field;
  tent  ('<name>'):     the whole WW animation (he sits on the counter).
Each joint is turned by the same amount, relative to his hips, as it turns in WW from WW's bind pose.
Usage: python3 -I retarget_ww.py Bs.arc beedle.bmd Mgeneral.arc OUT_DIR"""
import sys, os
sys.path[:0] = ['/home/claude/w/anim']
import numpy as np, rarc
from bmd import BMD, joint_names
from bck import BCK, srt_matrix, euler_xyz, write_bck

bs_arc, ours_bmd, mg_arc, out = sys.argv[1:5]
def arc(p):
    d = open(p, 'rb').read(); return rarc.rarc(rarc.yaz0(d) if d[:4] == b'Yaz0' else d)
BS = arc(bs_arc); MG = arc(mg_arc)
ww = BMD(BS['bdlm/bs.bdl']); wwn = joint_names(ww)
tp = BMD(open(ours_bmd, 'rb').read()); tpn = joint_names(tp)
stand = BCK([v for k, v in MG.items() if k.endswith('m_wait_a.bck')][0])
# which WW joint drives each of ours (the game's backbone = WW's upper backbone, which carries both turns)
SRC = {n: n for n in tpn}; SRC['backbone'] = 'backbone2'
UPPER = {'backbone', 'neck', 'head', 'shoulderL', 'armL1', 'armL2', 'handL', 'shoulderR', 'armR1', 'armR2', 'handR'}

def worlds(b, loc):
    W = [None] * len(loc)
    for i in range(len(loc)):
        W[i] = loc[i] if b.parent[i] < 0 else W[b.parent[i]] @ loc[i]
    return W
def anim_locals(b, t, f):
    L = []
    for j in range(len(b.jloc)):
        m = srt_matrix(*t.sample(j, f))
        if j > 0: m[:3, 3] = b.jloc[j][:3, 3]
        L.append(m)
    return L

ww_bind = worlds(ww, ww.jloc); tp_bind = worlds(tp, tp.jloc)
Rc_ww_b = ww_bind[0][:3, :3]; Rc_tp_b = tp_bind[0][:3, :3]

def retarget(t, upper_only):
    frames = []
    for f in range(t.dur):
        wl = anim_locals(ww, t, f); wp = worlds(ww, wl)
        sl = anim_locals(tp, stand, f % stand.dur); sp = worlds(tp, sl)
        Rc_ww_p = wp[0][:3, :3]
        base_loc = sl if upper_only else [m.copy() for m in tp.jloc]
        if not upper_only:   # his hips as in WW (the seated tilt), at the game model's own height
            E = (Rc_ww_p @ Rc_ww_b.T)
            R0 = E @ Rc_tp_b
            M0 = np.eye(4); M0[:3, :3] = R0
            M0[:3, 3] = tp.jloc[0][:3, 3] + (wl[0][:3, 3] - ww.jloc[0][:3, 3])
            base_loc[0] = M0
        Rc_tp_p = worlds(tp, base_loc)[0][:3, :3]
        targ = {}
        for j, nm in enumerate(tpn):
            if upper_only and nm not in UPPER: continue
            if nm == 'center': continue
            wj = wwn.index(SRC[nm])
            rel_p = Rc_ww_p.T @ wp[wj][:3, :3]; rel_b = Rc_ww_b.T @ ww_bind[wj][:3, :3]
            E = rel_p @ rel_b.T                                  # in WW's hip frame
            E_model = Rc_ww_b @ E @ Rc_ww_b.T; E_tp = Rc_tp_b.T @ E_model @ Rc_tp_b
            targ[j] = Rc_tp_p @ E_tp @ (Rc_tp_b.T @ tp_bind[j][:3, :3])
        loc = [m.copy() for m in base_loc]
        W = [None] * len(loc)
        for j in range(len(loc)):
            p = tp.parent[j]
            if j in targ:
                Rp = W[p][:3, :3] if p >= 0 else np.eye(3)
                loc[j][:3, :3] = Rp.T @ targ[j]
            W[j] = loc[j] if p < 0 else W[p] @ loc[j]
        fr = []
        for j in range(len(loc)):
            fr.append(([1.0, 1.0, 1.0], euler_xyz(loc[j][:3, :3]), list(loc[j][:3, 3])))
        frames.append(fr)
    return frames

os.makedirs(out, exist_ok=True)
made = []
for k, v in sorted(BS.items()):
    if not k.endswith('.bck'): continue
    t = BCK(v); name = os.path.basename(k)[:-4]
    for upper, suffix in ((True, '_up'), (False, '')):
        data = write_bck(retarget(t, upper), loop=t.loop)
        fn = os.path.join(out, 'psr_' + name + suffix + '.bck'); open(fn, 'wb').write(data); made.append((fn, t.dur, len(data)))
for m in made: print(m)
