import sys; sys.path[:0]=['/home/claude/w/anim','/home/claude/repo/tools/beedle']
from rarc import rarc,yaz0; from bmd import BMD; from bck import BCK, srt_matrix; from render import render
from fix_normals import corners
import numpy as np; from PIL import Image
b=BMD(open(sys.argv[5] if len(sys.argv)>5 else '/home/claude/cook/psr_cook.bmd','rb').read()); per=corners(b)
d=open('/mnt/user-data/uploads/Rom Dump/files/res/Object/Mgeneral.arc','rb').read(); A=rarc(yaz0(d) if d[:4]==b'Yaz0' else d)
anim=BCK(A['bck/%s.bck'%sys.argv[1]])
Wb=[b.world(i) for i in range(len(b.jloc))]; inv=[np.linalg.inv(m) for m in Wb]
def worlds(L):
    W=[None]*len(L)
    for i in range(len(L)): W[i]=L[i] if b.parent[i]<0 else W[b.parent[i]]@L[i]
    return W
class S: pass
frames=[]
n=int(sys.argv[2]) if len(sys.argv)>2 else 16
yaw=float(sys.argv[3]) if len(sys.argv)>3 else 30
for k in range(n):
    f=k*anim.dur/n
    # as the game plays townsfolk animations (daNpcCd2: offTranslate + setTranslateScale): rotations from the
    # animation, every joint's offset from the body itself, the root's motion scaled for this body (MAD_a2)
    L=[]
    for j in range(len(b.jloc)):
        if j>=anim.nj: L.append(b.jloc[j]); continue
        sc,rot,tr=anim.sample(j,f)
        tr=np.array(tr,float)*np.array([1.0,1.1983438,2.173357]) if j==0 else b.jloc[j][:3,3]
        L.append(srt_matrix(sc,rot,tr))
    W=worlds(L); Mj=[W[j]@inv[j] for j in range(len(W))]
    s=S(); s.tex=b.tex; s.mats=b.mats; s.tris=[]
    for mi in sorted(per):
        if len(sys.argv)>6 and mi!=int(sys.argv[6]): continue
        for t in per[mi]:
            P=[]
            for p,nr,uv,w in t:
                M=sum(wt*Mj[j] for j,wt in w.items()); P.append((M@np.append(p,1))[:3])
            s.tris.append((mi,np.array(P),np.array([c[2] for c in t])))
    import math, os
    pitch=math.radians(float(os.environ.get('PITCH','0')))
    if pitch:
        ya=math.radians(yaw); Ry=np.array([[math.cos(ya),0,math.sin(ya)],[0,1,0],[-math.sin(ya),0,math.cos(ya)]])
        Rx=np.array([[1,0,0],[0,math.cos(pitch),-math.sin(pitch)],[0,math.sin(pitch),math.cos(pitch)]])
        s.tris=[(mi,(P@Ry.T)@Rx.T,uv) for mi,P,uv in s.tris]
        frames.append(render(s,0,W=360,H=440,bounds=(np.array([-110,-5,-1e9]),np.array([110,250,1e9]))))
    else:
        frames.append(render(s,yaw,W=360,H=440,bounds=(np.array([-110,-5,-1e9]),np.array([110,250,1e9]))))
frames[0].save(sys.argv[4],save_all=True,append_images=frames[1:],duration=70,loop=0)
