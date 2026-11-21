"""The cook's wooden spoon, one closed piece: a diamond-section handle that widens smoothly into an
egg-shaped bowl (narrow at the handle, round at the tip), dished on top and rounded underneath.
Modelled along +X with the grip at the origin, as the old one. Wood: the old spoon's own texture."""
import sys,math; sys.path[:0]=['/home/claude/w/anim']
import numpy as np; from bmd import BMD; from bmdwrite import write_rigid_bmd
old=BMD(open('/home/claude/cook/psr_spoon_old.bmd','rb').read()); TEX=np.asarray(old.tex[0]).astype(np.uint8)[...,:3]
# stations along the spoon: (x, half width, top centre y, bottom centre y); rims at y=0.6
st=[(-16.5,2.0,1.6,-1.6),(40.0,2.8,2.0,-2.0),       # handle
    (48.0,4.2,1.0,-2.6),(53.0,7.0,-0.4,-3.5),(59.0,9.0,-1.5,-4.3),(65.0,9.4,-1.7,-4.4),(70.0,8.3,-1.2,-4.0),
    (74.0,5.8,-0.3,-3.0),(76.4,2.8,0.3,-1.6)]
TIP=np.array([77.4,0.4,0.0])
def sec(x,w,yt,yb): return [np.array([x,0.6 if x>41 else 0.0,-w]),np.array([x,yt,0.0]),np.array([x,0.6 if x>41 else 0.0,w]),np.array([x,yb,0.0])]
S=[sec(*s) for s in st]
def uv(p): return ((p[0]+20)/100.0,(p[2]+10)/20.0)
tris=[]
def T(a,b,c): tris.append((np.array([a,b,c],float),np.array([uv(a),uv(b),uv(c)])))
for k in range(len(S)-1):
    A,B=S[k],S[k+1]
    for i in range(4):
        j=(i+1)%4; T(A[i],B[i],B[j]); T(A[i],B[j],A[j])
E=S[0]; T(E[0],E[2],E[1]); T(E[0],E[3],E[2])           # the handle's end
L=S[-1]
for i in range(4): T(L[i],TIP,L[(i+1)%4])                  # the bowl's tip
print(len(tris),'triangles')
write_rigid_bmd('/home/claude/cook/psr_spoon.bmd',tris,TEX,texname='psr_spoon',jointname='psr_spoon',matname='psr_spoon_m')
