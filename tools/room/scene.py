"""The room (interior2.pkl) plus the game's own props, as one triangle list for the renderer."""
import sys, math, pickle, numpy as np
from PIL import Image
sys.path[:0]=['/home/claude/anim','/home/claude/passer-project/tools/tents']
from rarc import rarc
from bmd import BMD
TRIS, PROPS, LAY = pickle.load(open('/home/claude/room/interior2.pkl','rb'))
L_W, L_ZB = LAY['W'], LAY['ZB']
LIB = pickle.load(open('/home/claude/stages/texlib.pkl','rb'))
OBJ = '/mnt/user-data/uploads/Object'
SKIP_PROPS={'HTable','HChair','Nagaisu'}   # the display tables (centre of the floor) and the seats by the door
def build(night=False, clear_floor=True):
    atlas = np.array(Image.open('/home/claude/room/interior2_%s_HD_2048.png'%('night' if night else 'day')).convert('RGBA')).astype(float)/255
    TEX=[atlas]; vidx={}
    def vtex(n):
        if n not in vidx:
            vidx[n]=len(TEX)
            TEX.append(np.ones((2,2,4))*np.array([0.24,0.23,0.22,1]) if n=='IRON' or n not in LIB else LIB[n][0].astype(float)/255)
        return vidx[n]
    stands=[np.asarray(p,float) for p in LAY.get('STANDS',[])]
    def on_stand(P):   # the cushions on the display tables
        c=np.asarray(P,float).mean(0)
        return any(abs(c[0]-q[0])<40 and abs(c[2]-q[2])<40 and 40<c[1]<110 for q in stands)
    S=[((0 if t[2]=='ATLAS' else vtex(t[2])), np.asarray(t[0],float), np.asarray(t[1],float)) for t in TRIS
       if not (clear_floor and on_stand(t[0]))]
    arcs={}
    props=list(PROPS)
    for arcname, model, pos, yaw, sc in props:
        if clear_floor and arcname in SKIP_PROPS: continue
        if arcname not in arcs: arcs[arcname]=rarc(open(f'{OBJ}/{arcname}.arc','rb').read())
        key=[k for k in arcs[arcname] if k.endswith('/'+model)][0]
        b=BMD(arcs[arcname][key]); b.jworld=[b.world(i) for i in range(len(b.jloc))]; b.read_shp()
        base=len(TEX); TEX.extend(np.asarray(t,float)/255 for t in b.tex)
        a=math.radians(yaw); Rm=np.array([[math.cos(a),0,math.sin(a)],[0,1,0],[-math.sin(a),0,math.cos(a)]])
        if sc<0:
            P_=np.concatenate([np.asarray(t[1],float) for t in b.tris]); sc=-sc/(P_[:,1].max()-P_[:,1].min())
        for mi,p,uv in b.tris:
            ti,col=b.mats[mi] if mi<len(b.mats) else (None,np.ones(4))
            if ti is None or ti>=len(b.tex):
                TEX.append(np.ones((2,2,4))*np.r_[np.asarray(col,float)[:3],1.0]); ti2=len(TEX)-1
            else: ti2=base+ti
            S.append((ti2,(np.asarray(p,float)*sc)@Rm.T+np.asarray(pos,float),np.asarray(uv,float)))
    # ---- Wind Waker curios, the spoils bag on its hook and Beedle's patchwork bag (as in the approved preview) ----
    WW='/mnt/user-data/uploads/Wind Waker/files/res/Object'
    def rot_y(a):
        c,s_=math.cos(a),math.sin(a); return np.array([[c,0,s_],[0,1,0],[-s_,0,c]])
    def add_tris(tris, texs, place, size, yaw=0.0, anchor='bottom', hang=False, loop_along=None):
        if hang:
            P0=np.concatenate([np.asarray(t[1],float) for t in tris]); top=P0[P0[:,1].argmax()]; com=P0.mean(0)
            a_=(top-com)/np.linalg.norm(top-com); bv=np.array([0,1.0,0]); v=np.cross(a_,bv); cth=a_@bv
            K=np.array([[0,-v[2],v[1]],[v[2],0,-v[0]],[-v[1],v[0],0]]); Rh=np.eye(3)+K+K@K/(1+cth)
            tris=[(ti,np.asarray(p,float)@Rh.T,uv) for ti,p,uv in tris]
            if loop_along is not None:
                P1=np.concatenate([np.asarray(t[1],float) for t in tris]); ytop=P1[:,1].max(); h=ytop-P1[:,1].min()
                Q=P1[P1[:,1]>ytop-0.3*h]; _,_,vt=np.linalg.svd(Q-Q.mean(0)); n=vt[2]; t_=np.asarray(loop_along,float)
                yaw=math.atan2(t_[0],t_[2])-math.atan2(n[0],n[2])+math.pi
        base=len(TEX); TEX.extend(texs)
        P=np.concatenate([np.asarray(t[1],float) for t in tris]); mn,mx=P.min(0),P.max(0); sc_=size/(mx-mn).max()
        c=np.array([(mn[0]+mx[0])/2, mn[1] if anchor=='bottom' else mx[1], (mn[2]+mx[2])/2])
        if hang: c=P[P[:,1].argmax()].copy()
        Rm_=rot_y(yaw)
        for ti,p,uv in tris: S.append((base+ti,((np.asarray(p,float)-c)*sc_)@Rm_.T+place,np.asarray(uv,float)))
    def ww(arcname, model):
        a=rarc(open(f'{WW}/{arcname}.arc','rb').read()); k=[x for x in a if x.endswith('/'+model)][0]
        b=BMD(a[k]); b.jworld=[b.world(i) for i in range(len(b.jloc))]; b.read_shp()
        texs=[np.asarray(t,float)/255 for t in b.tex]; tris=[]
        for mi,p,uv in b.tris:
            ti,col=b.mats[mi] if mi<len(b.mats) else (None,np.ones(4))
            if ti is None or ti>=len(b.tex):
                texs.append(np.ones((2,2,4))*np.r_[np.asarray(col,float)[:3],1.0]); ti=len(texs)-1
            tris.append((ti,p,uv))
        return tris,texs
    add_tris(*ww('VbagE','vbage.bdl'), LAY['HOOK']+np.array([0,2.6,0]), 46, anchor='top', hang=True, loop_along=LAY['HOOK_DIR'])
    spots=LAY['WW_SPOTS']; order=[0,3,6,9,1,4,7,10,2,5,8]
    items=[('tele_00','vtele.bdl',30),('VboxN','vboxn.bdl',24),('Vtact','vtact.bdl',34),('Vchin','vchin.bdl',22),
           ('VbagF','vbagf.bdl',22),('Vhamm','vhamm.bdl',40),('Vrope','vrope.bdl',26),('Vleaf','vleaf.bdl',32),
           ('Vho','vho.bdl',28),('VbagH','vbagh.bdl',26),('VsaiL','vsail.bdl',34)]
    for (arcname,model,size),si in zip(items,order):
        p,side=spots[si%len(spots)]
        add_tris(*ww(arcname,model), np.asarray(p,float)+np.array([0,0.5,0]), size, yaw=math.radians(90 if side<0 else -90))
    # Beedle's patchwork merchant bag, standing in the back corner (Blender z-up -> game y-up)
    bag=pickle.load(open('/home/claude/room/bag.pkl','rb'))
    btex=np.array(Image.open('/mnt/user-data/uploads/Passer Project/assets/beedle/patchwork_bag/Patchwork_Texture.png').convert('RGBA')).astype(float)/255
    btris=[(0,np.array([[x,z,-y] for x,y,z in P]),np.array([[u,1-v] for u,v in UV])) for _,P,UV,_ in bag]
    add_tris(btris,[btex],np.array([-L_W/2+95,0,L_ZB+95]),120,yaw=math.radians(35))
    if night:   # props by lamplight too
        for i in range(1,len(TEX)): TEX[i]=np.concatenate([np.clip(TEX[i][...,:3]*np.array([1.0,0.78,0.5]),0,1),TEX[i][...,3:]],-1)
    return S,TEX