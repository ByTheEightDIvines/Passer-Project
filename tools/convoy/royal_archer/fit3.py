import sys;sys.path.insert(0,'/home/claude/rb')
from fit2 import Told, frame, pts_of, dense, case_tris
from fit import *
def solve(f, obst, step0=0.0):
    x=step0
    for it in range(40):
        p=f(x, obst)
        if abs(p+1.0)<0.3: return x
        x+=p+1.0
    return x
bw=BMD(PK['bmdr/psr_bow_royal.bmd'])
bw.jloc=[np.eye(4),M('kBowCenterBack'),Tr(-5,0,0)@rz(160)@RX,Tr(5,0,0)@rz(20),Tr(0,30,0)]
# the bow laid flat on his back: its arms along the old diagonal, its flat side out
Bo=M('kBowOnBack'); x=Bo[:3,0].copy(); x[1]=0; x/=np.linalg.norm(x)
z=np.array([0,1.0,0]); y=np.cross(z,x)
if np.dot(y,Bo[:3,1])<0: z=-z; y=np.cross(z,x)
BF=np.eye(4); BF[:3,0],BF[:3,1],BF[:3,2]=x,y,z; BF[:3,3]=Bo[:3,3]; BF[1,3]=0.0
bw.jworld=[BF@bw.world(i) for i in range(5)]; bw.read_shp()
BT=np.array([t[1] for t in bw.tris[:35]])
bowd=np.concatenate([BT.reshape(-1,3),np.einsum('kj,njd->nkd',np.random.default_rng(4).dirichlet([1,1,1],8),BT).reshape(-1,3)])
out={}
for (name,a) in BODIES[1:]:
    Pl=bodyB(name,a); nm=name.lower()
    cy=solve(lambda c,o: pen(o,pts_of(frame(c)@np.linalg.inv(Told),dense)), Pl)       # the quiver on his back
    G=frame(cy)@np.linalg.inv(Told); qd=pts_of(G,dense)
    push=solve(lambda d,o: pen(o,bowd+np.array([0,d,0])), np.concatenate([Pl,qd]))    # the bow over it
    S=MI('kHold_back_small_%s'%nm); sh=BMD(PK['bmdr/psr_g_shield_s.bmd'])
    sh.jworld=[S@sh.world(i) for i in range(len(sh.jloc))]; sh.read_shp()
    ST=np.array([t[1] for t in sh.tris])
    shd=np.concatenate([ST.reshape(-1,3),np.einsum('kj,njd->nkd',np.random.default_rng(2).dirichlet([1,1,1],12),ST).reshape(-1,3)])
    # the shield over everything (its back face must clear them: pen measured from the shield's side)
    ob=np.concatenate([Pl,qd,bowd+np.array([0,push,0])])
    sp=solve(lambda d,o: pen(o,shd+np.array([0,d,0])), ob)
    out[nm]=(G,push,Tr(0,sp,0)@S,BF)
    print(name,'quiver centre y',round(cy,1),'bow push',round(push,1),'shield out by',round(sp,1))
import pickle; pickle.dump(out,open('fits3.pkl','wb'))
