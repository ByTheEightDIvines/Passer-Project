"""Find the face skin (in the body atlas t1) right where each eye sits, and how big an eye is in that atlas."""
import sys; sys.path[:0]=['/home/claude/w/anim']
from bmd import BMD; import numpy as np
b=BMD(open('/home/claude/beedle/beedle.bmd','rb').read())
eye=[(np.asarray(P),np.asarray(uv)) for mi,P,uv in b.tris if mi==0]
E=np.array([p for p,_ in eye]).reshape(-1,3)
left=E[E[:,0]<0]; right=E[E[:,0]>0]
for name,pts in (('L',left),('R',right)):
    c=pts.mean(0); w=np.ptp(pts[:,0])
    best=None
    for mi,P,uv in b.tris:
        if mi!=1: continue
        P=np.asarray(P); uv=np.asarray(uv)
        a,bb,cc=P[:,:2]; v0,v1,v2=bb-a,cc-a,c[:2]-a
        den=v0[0]*v1[1]-v1[0]*v0[1]
        if abs(den)<1e-9: continue
        s=(v2[0]*v1[1]-v1[0]*v2[1])/den; t=(v0[0]*v2[1]-v2[0]*v0[1])/den
        if s<0 or t<0 or s+t>1: continue
        z=P[0,2]+s*(P[1,2]-P[0,2])+t*(P[2,2]-P[0,2])
        if z>c[2]+2 or (best and z<best[0]): continue
        UV=uv[0]+s*(uv[1]-uv[0])+t*(uv[2]-uv[0])
        # atlas pixels per model unit across x, from this triangle
        J=np.linalg.lstsq(np.c_[P[:,:2],np.ones(3)],uv,rcond=None)[0][:2]   # d(uv)/d(xy)
        best=(z,UV,J)
    z,UV,J=best
    print(name,'eye centre',c.round(2),'width',round(w,2),'face uv',UV.round(4),'px',(UV*512).round(1),'eye px wide',round(np.linalg.norm(J[0])*w*512,1),'J',J.round(4).tolist())

# the right eye's quad: texture (u,v) -> model (x,y)
q=[(p,u) for p,u in eye if p[:,0].mean()>0]
Pq=np.concatenate([p for p,_ in q]); Uq=np.concatenate([u for _,u in q])
A=np.linalg.lstsq(np.c_[Uq,np.ones(len(Uq))],Pq[:,:2],rcond=None)[0]     # [u v 1] -> x y
print('eye uv->xy',A.round(3).tolist(), 'uv range',Uq.min(0),Uq.max(0))
np.savez('/home/claude/beedle/eye_map.npz',A=A,J=J,UV=UV,C=c[:2])
