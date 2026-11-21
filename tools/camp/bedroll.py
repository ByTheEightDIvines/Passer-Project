"""Bedroll: a flat mat with its head end rolled into a pillow. 12 triangles. Texture: Colin's bed blanket
(KH_Bed.arc kolin_bed, the quilted strip), alpha cut so the foot keeps its fringe."""
import sys; sys.path[:0]=['/home/claude/w/anim']
import numpy as np; from PIL import Image
from bmdwrite import write_rigid_bmd
TEX=np.asarray(Image.open('/home/claude/tex/KH_Bed_kh_bed_00_kolin_bed.png').convert('RGBA'),np.uint8)
U0,U1=8/128,63/128            # the blanket strip across
HW=37.5; Y=2.0                 # half width; just off the ground
ZF=70.0; ZH=-70.0              # foot, where the roll starts
VF=1.0; VH=0.2                 # foot fringe ... head of the flat part
ROLL=[(ZH,Y),(-72.0,12.0),(-84.0,15.0),(-88.0,Y)]   # (z, y) over the top and down the back
VR=[VH,0.165,0.13,0.095]       # stops short of the head fringe (it would hole the roll)
def quad(a,b,c,d,ua,ub,uc,ud): return [(np.array([a,b,c]),np.array([ua,ub,uc])),(np.array([a,c,d]),np.array([ua,uc,ud]))]
tris=[]
# mat top
tris+=quad((-HW,Y,ZF),(HW,Y,ZF),(HW,Y,ZH),(-HW,Y,ZH),(U0,VF),(U1,VF),(U1,VH),(U0,VH))
# roll faces
for i in range(3):
    (z0,y0),(z1,y1)=ROLL[i],ROLL[i+1]
    tris+=quad((-HW,y0,z0),(HW,y0,z0),(HW,y1,z1),(-HW,y1,z1),(U0,VR[i]),(U1,VR[i]),(U1,VR[i+1]),(U0,VR[i+1]))
# roll ends: a patch of the quilt's border
cu=[(10/128,40/128),(14/128,30/128),(22/128,28/128),(24/128,40/128)]
for x in (-HW,HW):
    p=[(x,y,z) for z,y in ROLL]
    tris+=quad(p[0],p[1],p[2],p[3],*cu)
assert len(tris)==12
# in the small tent's own space (it's drawn with the tent's matrix, scale 1.3): head just inside the back wall
K=1.3; OFF=np.array([0.0,0.0,-62.0])
write_rigid_bmd('/home/claude/bedroll/psr_bedroll.bmd',[((np.asarray(p,float)+OFF)/K,np.asarray(u,float)) for p,u in tris],TEX,
                texname='psr_bedroll',jointname='psr_bedroll',matname='psr_bedroll_m',alpha=True)
print('written',len(tris),'triangles')
