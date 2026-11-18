import sys;sys.path.insert(0,'/home/claude/passer-project/tools/convoy/royal_archer')
from fit import *
q=BMD(PK['bmdr/psr_quiver_royal.bmd'])
allp=np.array([t[1] for t in q.tris]).reshape(-1,3)
case_tris=np.array([t[1] for t in q.tris if t[0]==0])
C=case_tris.reshape(-1,3).mean(0); _,_,V=np.linalg.svd(case_tris.reshape(-1,3)-C)
E0,E1,E2=V
if np.dot(np.cross(E0,E1),E2)<0: E2=-E2
# the mouth end: where the arrows are
arrowsC=np.array([t[1] for t in q.tris if t[0]==1]).reshape(-1,3).mean(0)
if np.dot(arrowsC-C,E0)>0: E0=-E0; E1=-E1      # keep E0 pointing from the mouth to the foot, right handed
Told=np.eye(4); Told[:3,0],Told[:3,1],Told[:3,2],Told[:3,3]=E0,E1,E2,C
print('old axis',E0.round(3),'flat',E2.round(3))
def frame(cy):
    a=E0.copy(); a[1]=0; a/=np.linalg.norm(a)          # along it, in the back's plane
    f=np.array([0,1.0,0])                              # flat side out
    w=np.cross(f,a)
    T=np.eye(4); T[:3,0],T[:3,1],T[:3,2]=a,w,f
    T[:3,3]=(C[0],cy,C[2])
    if np.linalg.det(T[:3,:3])<0: T[:3,1]=-T[:3,1]
    return T
def pts_of(G, tris):
    P=np.array(tris).reshape(-1,3)
    return (G@np.c_[P,np.ones(len(P))].T).T[:,:3]
dense=np.concatenate([case_tris.reshape(-1,3),np.einsum('kj,njd->nkd',np.random.default_rng(1).dirichlet([1,1,1],8),case_tris).reshape(-1,3)])
out={}
for (name,a) in BODIES[1:]:
    Pl=bodyB(name,a); nm=name.lower()
    S=MI('kHold_back_small_%s'%nm); sh=BMD(PK['bmdr/psr_g_shield_s.bmd'])
    sh.jworld=[S@sh.world(i) for i in range(len(sh.jloc))]; sh.read_shp()
    shT=np.array([t[1] for t in sh.tris])
    Ps=np.concatenate([shT.reshape(-1,3),np.einsum('kj,njd->nkd',np.random.default_rng(2).dirichlet([1,1,1],12),shT).reshape(-1,3)])
    for shield in (False,True):
        obst=np.concatenate([Pl,Ps]) if shield else Pl
        bw=BMD(PK['bmdr/psr_bow_royal.bmd'])
        bw.jloc=[np.eye(4),M('kBowCenterBack'),Tr(-5,0,0)@rz(160)@RX,Tr(5,0,0)@rz(20),Tr(0,30,0)]
        bw.jworld=[M('kBowOnBack')@bw.world(i) for i in range(5)]; bw.read_shp()
        bowp=np.array([t[1] for t in bw.tris[:35]]).reshape(-1,3)
        bowd=np.concatenate([bowp,np.einsum('kj,njd->nkd',np.random.default_rng(4).dirichlet([1,1,1],8),np.array([t[1] for t in bw.tris[:35]])).reshape(-1,3)])
        push=0.0
        for it in range(30):           # the bow first, against the back (or the shield)
            p=pen(obst,bowd+np.array([0,push,0]))
            if abs(p+1.0)<0.3: break
            push+=p+1.0
        ob2=np.concatenate([obst,bowd+np.array([0,push,0])])
        cy=0.0
        for it in range(30):           # then the quiver over it, clear by 1 everywhere
            G=frame(cy)@np.linalg.inv(Told)
            p=pen(ob2,pts_of(G,dense))
            if abs(p+1.0)<0.3: break
            cy+=p+1.0
        Gq=G
        out[(nm,shield)]=(Gq,push)
        print(name,'shield on back' if shield else 'shield on arm','quiver centre y',round(cy,1),'bow push',round(push,1))
import pickle; pickle.dump(out,open('fits.pkl','wb'))
