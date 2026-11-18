"""The guards' fight set and holds for the game: from the approved refits (escort/sol_retarget.py poses A, C, D;
escort/thrust.py) and holds (escort/holds.pkl, spearhold.hold_forward2). Writes BCKs to OUT and prints the
hold matrices for src/guard_gear.inc.  Run from the escort work folder:  python3 guard_anims.py OUT"""
import sys, pickle, numpy as np
OUT = sys.argv[1]
src = open('/home/claude/w/escort/thrust.py').read().split("if __name__=='__main__':")[0]
sys.path[:0] = ['/home/claude/w/anim']
from bck import write_bck, euler_xyz, BCK, srt_matrix
from bmd import BMD, joint_names
HOLD = pickle.load(open('/home/claude/w/escort/holds.pkl', 'rb'))
ARMS = pickle.load(open('/home/claude/w/escort/arms.pkl', 'rb'))
def frames_of(Ls):
    return [[([1.0, 1.0, 1.0], euler_xyz(np.asarray(L)[:3, :3]), list(np.asarray(L)[:3, 3])) for L in Lf] for Lf in Ls]
def save(name, Ls, loop=2):
    open('%s/%s' % (OUT, name), 'wb').write(write_bck(frames_of(Ls), loop=loop)); print('wrote', name, len(Ls), 'frames')
mats = {}
for body in ('MAD_a', 'MAN_a'):
    m = body.lower()
    g = {}
    exec("import sys; exec(open('/home/claude/w/escort/guard_check.py').read().split('if __name__')[0])", g)
    GB = g['arc']('/mnt/user-data/uploads/Object/%s.arc' % body)['bmdr/%s.bmd' % m]
    g['GUARD'] = GB
    exec(src.replace("exec(open('/home/claude/escort/guard_check.py').read().split(\"if __name__\")[0])", ""), g)
    b = BMD(GB); nt = joint_names(b)
    D = g['D']; refit = g['refit']
    # weapons as the cutscene soldiers hold them in a fight (relative to the guard's hand), from pose A
    # (spear, tall shield) and pose D (sword, small shield)
    from bmd import BMD as _B
    def fight_hold(mk, bk, f, wj, hand):
        L, Wt, Ws, ns, k = refit(GB, mk, D[bk], f)
        s_ = _B(g['MODELS'][mk]); Bs = g['worlds'](s_, s_.jloc); Bt = g['worlds'](b, b.jloc)
        j = ns.index(wj); hs = ns.index(hand); ht = nt.index(hand)
        Rel = np.linalg.inv(Ws[hs]) @ Ws[j]; Rel[:3, 3] *= k
        C = np.eye(4); C[:3, :3] = np.linalg.inv(Bt[ht][:3, :3]) @ Bs[hs][:3, :3]
        return C @ Rel
    mats['fspear_' + m] = g['GRIP']
    mats['ftall_' + m] = fight_hold('solb', 'bck/demo07_solb_cut02_gp_1_l.bck', 20, 'sol_SHE', 'handL')
    mats['fsword_' + m] = fight_hold('sold', 'bck/demo07_sold_cut06_gp_1_o.bck', 30, 'Sol_SW', 'handR')
    mats['fsmall_' + m] = fight_hold('sold', 'bck/demo07_sold_cut06_gp_1_o.bck', 30, 'sol_SHE_s', 'handL')
    # the stances (one frame each): A spear on guard, D sword crouched, C shield block
    for name, mk, bk, f in (('spear', 'solb', 'bck/demo07_solb_cut02_gp_1_l.bck', 20),
                            ('sword', 'sold', 'bck/demo07_sold_cut06_gp_1_o.bck', 30),
                            ('block', 'solc', 'bck/demo07_solc_cut13_gp_1_o.bck', 150)):
        L = refit(GB, mk, D[bk], f)[0]
        if name == 'block': save('psr_g_%s_%s.bck' % (name, m), [L] * 18, loop=0)   # held a moment (played once)
        else: save('psr_g_%s_%s.bck' % (name, m), [L])
    # the thrust (frames 0..32)
    save('psr_g_thrust_%s.bck' % m, [g['pose'](f) for f in range(33)], loop=0)
    # the shield arm while walking: bind everywhere, the left arm as in pose A (tall shield) or D (small)
    for pose in ('A', 'D'):
        L = [np.array(j, float) for j in b.jloc]
        for j, Lj in ARMS[pose][body].items(): L[nt.index(j)] = np.array(Lj, float)
        save('psr_garm_%s_%s.bck' % (pose, m), [L])
    # the spear in the right hand, as it's held on the game's own spear walk
    gg = {}
    exec(open('/home/claude/w/escort/base.py').read(), gg)
    exec(open('/home/claude/w/escort/royal11.py').read().split("def body_at")[0].split("HOLD=pickle")[0], gg)
    for k in ('wl', 'mirrored', 'SW', 'Mx'):
        pass
    exec(open('/home/claude/w/escort/royal11.py').read().split("def place(data,W):")[1].split("def body_at")[0].join(["def place(data,W):", ""]) if False else "", gg)
    seg = open('/home/claude/w/escort/royal11.py').read()
    seg = seg[seg.index("SW=lambda"):seg.index("def body_at")]
    exec(seg, gg)
    exec(open('/home/claude/w/escort/spearhold.py').read(), gg)
    mats['spear_' + m] = gg['hold_forward2'](GB, body)
for k in ('tall', 'small', 'sword', 'scab'): mats[k] = np.asarray(HOLD[k], float)
pickle.dump(mats, open('%s/holds_out.pkl' % OUT, 'wb'))
for k, M in mats.items():
    M = np.asarray(M)
    print('const Mtx kHold_%s = {%s};' % (k, ', '.join('{%s}' % ', '.join('%.5ff' % v for v in M[r]) for r in range(3))))
