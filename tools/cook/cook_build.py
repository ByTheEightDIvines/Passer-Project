"""The cook's body, rebuilt: his recoloured texture (cream jacket, linen sash, grey hair), a weighted canvas
bib apron (chest to knees, neck strap and waist tie), and his blink kept (eye frames re-added, btp re-made)."""
import sys,math; sys.path[:0]=['/home/claude/w/anim','/home/claude/repo/tools/beedle','/home/claude/scripts']
import numpy as np; from PIL import Image
from bmd import BMD, joint_names, u32
from fix_normals import corners, renormal
from skinbmd import write_skinned
from beedle_blink_build import add_textures, btp
from rarc import rarc,yaz0
d=open('/mnt/user-data/uploads/Rom Dump/files/res/Object/MAD_a2.arc','rb').read(); A=rarc(yaz0(d) if d[:4]==b'Yaz0' else d)
b=BMD(A['bmdr/mad_a2.bmd'])
per=corners(b); names=joint_names(b); J={n:i for i,n in enumerate(names)}
body=per[0]; eyes=per[1]
# --- where his front is: a depth map seen from the front (max z at each x,y) ---------------------
X0,X1,Y0,Y1=-70,70,30,190; R=1.0
dep=np.full((int((Y1-Y0)/R)+1,int((X1-X0)/R)+1),-1e9)
for t in body:
    P=np.array([c[0] for c in t]); 
    if P[:,2].max()<-5: continue
    xs=(P[:,0]-X0)/R; ys=(P[:,1]-Y0)/R
    x0,x1=int(max(0,np.floor(xs.min()))),int(min(dep.shape[1]-1,np.ceil(xs.max())))
    y0,y1=int(max(0,np.floor(ys.min()))),int(min(dep.shape[0]-1,np.ceil(ys.max())))
    if x0>x1 or y0>y1: continue
    gx,gy=np.meshgrid(np.arange(x0,x1+1),np.arange(y0,y1+1))
    den=(ys[1]-ys[2])*(xs[0]-xs[2])+(xs[2]-xs[1])*(ys[0]-ys[2])
    if abs(den)<1e-9: continue
    w0=((ys[1]-ys[2])*(gx-xs[2])+(xs[2]-xs[1])*(gy-ys[2]))/den; w1=((ys[2]-ys[0])*(gx-xs[2])+(xs[0]-xs[2])*(gy-ys[2]))/den; w2=1-w0-w1
    m=(w0>=-0.05)&(w1>=-0.05)&(w2>=-0.05); z=w0*P[0,2]+w1*P[1,2]+w2*P[2,2]
    sub=dep[y0:y1+1,x0:x1+1]; sub[m]=np.maximum(sub[m],z[m])
def front(x,y,hang=0.0,win=3):
    """z just in front of him at (x,y); with hang, the most forward point from y up to y+hang (cloth falls straight)"""
    xi=int(round((np.clip(x,X0,X1)-X0)/R)); y0=int(round((np.clip(y,Y0,Y1)-Y0)/R)); y1=int(round((np.clip(y+hang,Y0,Y1)-Y0)/R))
    col=dep[max(0,y0-win):y1+win+1,max(0,xi-win):xi+win+1]
    return col.max() if col.size else 0.0
# --- the apron ----------------------------------------------------------------------------------
GAP=2.2
rows=[(176,29),(160,32),(140,34),(116,40),(96,44),(72,45),(50,46)]   # (height, half width): bib, then the skirt flaring
cols=[-1.0,-0.5,0.0,0.5,1.0]
def wts(x,y):
    if y>=120: return {J['backbone']:1.0}
    t=np.clip((118-y)/60.0,0,1)*0.5                       # how much the legs carry it, growing down the skirt
    s=np.clip((x+60)/120.0,0,1); s=s*s*(3-2*s)              # +x is his left leg
    w={J['waist']:1-t}
    if t>0: w[J['legL1']]=t*s; w[J['legR1']]=t*(1-s)
    if 116<y<120: w={J['backbone']:0.5,J['waist']:0.5}
    return {k:v for k,v in w.items() if v>1e-3}
V=[]
for yi,(y,hw) in enumerate(rows):
    row=[]
    for c in cols:
        x=c*hw
        z=(front(x,y,win=2)+GAP if y>=170 else front(x,y,hang=0 if y>=116 else 40,win=9)+GAP+1.5)+(0 if y>=116 else 3.5+4.0*max(0,96-y)/46)
        row.append((np.array([x,y,z]),((c+1)/2*0.9+0.05, 1-(y-40)/140.0),wts(x,y)))
    V.append(row)
tris=[]
for r in range(len(rows)-1):
    for c in range(len(cols)-1):
        a,bb,cc,dd=V[r][c],V[r][c+1],V[r+1][c+1],V[r+1][c]
        tris+= [[a,dd,cc],[a,cc,bb]]
# neck strap: from the bib's top corners up and round the back of his neck (a flat band, both sides seen)
def band(pts,w,uv0):
    out=[]
    for i in range(len(pts)-1):
        p,q=np.array(pts[i][0]),np.array(pts[i+1][0]); dirv=q-p; side=np.cross(dirv,[0,0,1.0]); 
        if np.linalg.norm(side)<1e-6: side=np.cross(dirv,[1.0,0,0])
        side=side/np.linalg.norm(side)*w/2
        A_,B_,C_,D_=p-side,p+side,q+side,q-side
        u=lambda k: (0.02+0.04*k, uv0+0.05*i)
        wa,wb=pts[i][1],pts[i+1][1]
        out+= [[(A_,u(0),wa),(B_,u(1),wa),(C_,u(1),wb)],[(A_,u(0),wa),(C_,u(1),wb),(D_,u(0),wb)]]
    return out
bb_=J['backbone']; nk=J['neck']
zb=front(0,170)
# The neck strap: ONE flat band, from the bib's left corner up to his neck, right round the back of his
# neck just above the neckerchief, and down to the bib's right corner. The band lies flat against him
# (its width runs along his neck, at right angles to the strap's path), so it never twists or folds.
BC=[(c[0],c[3]) for t in body for c in t]
BP=np.array([p for p,_ in BC])
def near_w(p):
    return dict(BC[int(np.argmin(((BP-p)**2).sum(1)))][1])
NC=np.array([0.0,0.0,2.0])                                   # his neck's axis (x=0, z=2), vertical
# Like a real bib apron: from each top corner of the bib straight up beside his neck, then draped over the
# back of his neck at its base, lying on his neckerchief. Its radius at each angle: just outside him there.
YS=194.0
NV=BP[(BP[:,1]>YS-6)&(BP[:,1]<YS+6)&(np.abs(BP[:,0])<45)]
nang=np.degrees(np.arctan2(NV[:,2]-NC[2],NV[:,0])); nrad=np.hypot(NV[:,0],NV[:,2]-NC[2])
BT=np.array([[c[0] for c in t] for t in body])               # his triangles, for exact surface hits
def surface_r(y,deg):
    """how far out from his neck axis his surface is, at height y in direction deg (a ray cast on his mesh)"""
    a=math.radians(deg); o=np.array([0.0,y,NC[2]]); d=np.array([math.cos(a),0.0,math.sin(a)])
    e1=BT[:,1]-BT[:,0]; e2=BT[:,2]-BT[:,0]; h=np.cross(d,e2); det=(e1*h).sum(1); ok=np.abs(det)>1e-9
    f=np.where(ok,1.0/np.where(ok,det,1),0); sv=o-BT[:,0]; u=f*(sv*h).sum(1); q=np.cross(sv,e1); v=f*(q*d).sum(1); t=f*(e2*q).sum(1)
    hit=ok&(u>=0)&(v>=0)&(u+v<=1)&(t>0)&(t<70)
    return t[hit].max() if hit.any() else 25.0
# The neck strap, as on a real bib apron: two straight bands, straight up from the bib's top corners,
# lying on his chest and then over his shoulders, and one band joining them round the back of his neck.
# Every point sits on his actual surface (ray casts on his mesh) a hair out, and the band lies flat on it
# (its width at right angles to the strap, along the surface), so it follows him without twisting.
def cast(o,d):
    e1=BT[:,1]-BT[:,0]; e2=BT[:,2]-BT[:,0]; h=np.cross(d,e2); det=(e1*h).sum(1); ok=np.abs(det)>1e-9
    f=np.where(ok,1.0/np.where(ok,det,1),0); sv=o-BT[:,0]; u=f*(sv*h).sum(1); q=np.cross(sv,e1); v=f*(q*d).sum(1); t=f*(e2*q).sum(1)
    hit=ok&(u>=0)&(v>=0)&(u+v<=1)&(t>0)&(t<120)
    if not hit.any(): return None,None
    k=np.argmax(np.where(hit,t,-1)); n=np.cross(e1[k],e2[k]); n/=np.linalg.norm(n)
    if n@d<0: n=-n
    return o+d*t[k],n
CX=V[0][-1][0][0]                                              # the bib's top corners are at x = +-CX
OFF=1.3
def side_path(sx):
    """up his chest and over his shoulder, in the plane x = sx*CX: rays from inside his upper chest"""
    C=np.array([sx*CX,176.0,0.0]); out=[]
    for ph in range(0,181,15):
        a=math.radians(ph); d=np.array([0.0,math.sin(a),math.cos(a)])
        p_,n=cast(C,d)
        if p_ is not None: out.append((p_+n*OFF,n))
    return out
R_=side_path(1); L_=side_path(-1)
# keep the part from the bib up and over: from his chest round to where it starts down his back
def trim(path):
    keep=[q for q in path if q[0][1]>=179.0]
    return keep[: next((i for i,q in enumerate(keep) if q[0][2]< -6.0), len(keep))+1]
R_=trim(R_); L_=trim(L_)
yb=(R_[-1][0][1]+L_[-1][0][1])/2
# round the back of his neck, from the right strap's end to the left's, on his surface
back=[]
for k in range(1,6):
    x=CX-2*CX*k/6.0
    p_,n=cast(np.array([x,yb,0.0]),np.array([0.0,0.0,-1.0]))
    if p_ is not None: back.append((p_+n*OFF,n))
path=[(V[0][-1][0],np.array([0,0,1.0]))]+R_+back+list(reversed(L_))+[(V[0][0][0],np.array([0,0,1.0]))]
# smooth the surface directions a little along the strap, so it never flips on one odd triangle
P_=np.array([q[0] for q in path]); N_=np.array([q[1] for q in path])
N_=np.array([(N_[max(i-1,0)]+2*N_[i]+N_[min(i+1,len(N_)-1)]) for i in range(len(N_))]); N_/=np.linalg.norm(N_,axis=1)[:,None]
W2=1.7
for i in range(len(P_)-1):
    def edge(k):
        d=P_[min(k+1,len(P_)-1)]-P_[max(k-1,0)]; d/=np.linalg.norm(d)
        w=np.cross(N_[k],d); w/=np.linalg.norm(w); return P_[k]-w*W2,P_[k]+w*W2
    a0,a1=edge(i); b0,b1=edge(i+1)
    wa=near_w(P_[i]) if 0<i<len(P_)-1 else {bb_:1.0}; wb=near_w(P_[i+1]) if 0<i+1<len(P_)-1 else {bb_:1.0}
    tris+=[[(a0,(0.02,0.03*i),wa),(a1,(0.06,0.03*i),wa),(b1,(0.06,0.03*(i+1)),wb)],
           [(a0,(0.02,0.03*i),wa),(b1,(0.06,0.03*(i+1)),wb),(b0,(0.02,0.03*(i+1)),wb)]]
# waist tie: from the apron's waist corners round his back
yw=116; wl=V[3][0][0]; wr=V[3][-1][0]
# hugging his own waist: at each angle round the back, just outside the furthest of his body there
BV=np.array([c[0] for t in body for c in t]); BV=BV[(BV[:,1]>yw-6)&(BV[:,1]<yw+6)]
RX=np.abs(BV[:,0]).max()+2.0; ZB=BV[:,2].min()-2.0             # an ellipse just outside his waist: sides and back
ring=[]
for k in range(9):
    a=math.radians(-10-k*160/8)                              # from his left side (+x) round the back to his right
    ring.append(np.array([RX*math.cos(a),yw,2.2+(2.2-ZB)*math.sin(a)*-1*-1 if False else 2.2+(2.2-ZB)*math.sin(a)]))
pts=[(wl,{J['waist']:1.0})]+[(p,{J['waist']:1.0}) for p in ring]+[(wr,{J['waist']:1.0})]
tris+=band(pts,3.0,0.5)
print('apron triangles',len(tris))
ap=[]
for t in tris:
    ap.append([(np.asarray(p,float),np.array([0,0,1.0]),np.asarray(uv,float),w) for p,uv,w in t])
ap=renormal(ap,60)
canvas=np.asarray(Image.open('/home/claude/cook/tent_tex.png').convert('RGB').crop((4,0,248,512)).resize((128,256),Image.LANCZOS))
canvas=np.concatenate([canvas,np.full(canvas.shape[:2]+(1,),255,np.uint8)],-1)
# --- write it -------------------------------------------------------------------------------------
dj=b.d; o=b.sec['JNT1']; eo=o+u32(dj,o+0xC)
entries=[bytes(dj[eo+i*0x40:eo+(i+1)*0x40]) for i in range(len(names))]
skel=dict(names=names,parent=b.parent,jnt_entries=entries,world_pos=[b.jworld[i][:3,3] for i in range(len(names))])
cook=np.asarray(Image.open('/home/claude/cook/t0_cook.png').convert('RGBA'),np.uint8)
eye1=np.asarray(b.tex[b.mats[1][0]],np.uint8)
grp=lambda img,tr,wrap=(1,1): (img,bool((img[...,3]<128).any()),[[(c[0],c[1],c[2],c[3]) for c in t] for t in tr],wrap)
tmp='/home/claude/cook/psr_cook_tmp.bmd'
write_skinned(tmp,skel,[grp(cook,body,tuple(getattr(b.tex[b.mats[0][0]],'wrap',(1,1)))),grp(eye1,eyes,tuple(getattr(b.tex[b.mats[1][0]],'wrap',(1,1)))),grp(canvas,ap)])
# his blink: eye frames 2 and 3 back in, and the vanilla sequence (1,2,3,3,3,2) on our eye material m1
raw=open(tmp,'rb').read()
e2=Image.fromarray(np.asarray(b.tex[2],np.uint8)); e3=Image.fromarray(np.asarray(b.tex[3],np.uint8))
nb,idx=add_textures(raw,[e2,e3],['eye2','eye3'],like=1,alpha=bool((eye1[...,3]<128).any()))
open('/home/claude/cook/psr_cook.bmd','wb').write(nb)
seq=[1,idx[0],idx[1],idx[1],idx[1],idx[0]]
open('/home/claude/cook/psr_cook.btp','wb').write(btp('m1',seq))
print('eye frames at',idx,'blink',seq)
