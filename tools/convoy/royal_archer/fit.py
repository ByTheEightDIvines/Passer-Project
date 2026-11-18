import sys;sys.path.insert(0,'/home/claude/passer-project/tools/convoy/royal_archer')
from guardview import *
def bodyB(name, a):
    A=arc(a); b=BMD([v for k,v in A.items() if k.endswith('/%s.bmd'%name.lower())][0])
    t=BCK([v for k,v in MG.items() if k.endswith('m_wait_a.bck')][0]); bind=[m.copy() for m in b.jloc]; L=[]
    for j in range(len(b.jloc)):
        if j>=t.nj: L.append(b.jloc[j]); continue
        m=srt_matrix(*t.sample(j,0))
        if j>0: m[:3,3]=bind[j][:3,3]
        L.append(m)
    nt=joint_names(b); tw=tweak(name); b.jloc=[L[i]@tw.get(nt[i],np.eye(4)) for i in range(len(L))]
    b.jworld=[None]*len(b.jloc)
    for i in range(len(b.jloc)): b.jworld[i]=b.world(i)
    b.read_shp(); BB=b.jworld[nt.index('backbone')]
    P=np.array([t[1] for t in b.tris]).reshape(-1,3)
    # dense samples on the body's triangles (the corners alone are too sparse)
    T=np.array([t[1] for t in b.tris]); w=np.random.default_rng(0).dirichlet([1,1,1],12)
    S=np.concatenate([P, np.einsum('kj,njd->nkd',w,T).reshape(-1,3)])
    return (np.linalg.inv(BB)@np.c_[S,np.ones(len(S))].T).T[:,:3]
def pen(Pl, pts, r=3.0):
    worst=-1e9
    for p in pts:
        m=(np.abs(Pl[:,0]-p[0])<r)&(np.abs(Pl[:,2]-p[2])<r)&(Pl[:,1]>p[1]-40)
        if m.any(): worst=max(worst, Pl[m,1].max()-p[1])
    return worst
def gear_pts(name):
    q=BMD(PK['bmdr/psr_quiver_royal.bmd'])
    case=np.array([t[1] for t in q.tris if t[0]==0]).reshape(-1,3)
    case=np.concatenate([case, np.einsum('kj,njd->nkd',np.random.default_rng(1).dirichlet([1,1,1],6),np.array([t[1] for t in q.tris if t[0]==0])).reshape(-1,3)])
    bw=BMD(PK['bmdr/psr_bow_royal.bmd'])
    bw.jloc=[np.eye(4),M('kBowCenterBack'),Tr(-5,0,0)@rz(160)@RX,Tr(5,0,0)@rz(20),Tr(0,30,0)]
    base=M('kBowOnBack'); bw.jworld=[base@bw.world(i) for i in range(5)]; bw.read_shp()
    bow=np.array([t[1] for t in bw.tris[:35]]).reshape(-1,3)   # the arms and grip (not the string)
    return case, bow
BODIES=[('MAN_a2','/home/claude/h3/DT_handover_2026-11-18/assets/overlay/res/Object/P_MAN_a2.arc'),('MAN_a',U+'MAN_a.arc'),('MAD_a',U+'MAD_a.arc')]
if __name__=='__main__':
    case,bow=gear_pts(None)
    for name,a in BODIES:
        Pl=bodyB(name,a)
        print(name,'quiver into body by',round(pen(Pl,case),1),' bow by',round(pen(Pl,bow),1),'(before the -8 push)')
    for nm in ('MAN_a','MAD_a'):
        S=MI('kHold_back_small_%s'%nm.lower()); sh=BMD(PK['bmdr/psr_g_shield_s.bmd'])
        sh.jworld=[S@sh.world(i) for i in range(len(sh.jloc))]; sh.read_shp()
        Y=np.array([t[1] for t in sh.tris]).reshape(-1,3)
        print(nm,'shield on back: y',Y[:,1].min().round(1),Y[:,1].max().round(1),'x',Y[:,0].min().round(1),Y[:,0].max().round(1),'z',Y[:,2].min().round(1),Y[:,2].max().round(1))
        Ps=np.concatenate([Y,np.einsum('kj,njd->nkd',np.random.default_rng(2).dirichlet([1,1,1],12),np.array([t[1] for t in sh.tris])).reshape(-1,3)])
        print('   quiver into shield by',round(pen(Ps,case),1),' bow by',round(pen(Ps,bow),1))
