import sys,pickle,numpy as np,math
sys.path.insert(0,'/home/claude/ptb/Passer_Travellers_Build/tools')
from render import render
from PIL import Image
d=sys.argv[1]; out=sys.argv[2]; yaws=[float(y) for y in sys.argv[3].split(',')]; W,H=int(sys.argv[4]),int(sys.argv[5])
crop=None
if len(sys.argv)>6: crop=[float(x) for x in sys.argv[6].split(',')]
body=pickle.load(open(f'{d}/beedle_tent_tris.pkl','rb')); glow=pickle.load(open(f'{d}/beedle_tent_glow_tris.pkl','rb'))
tb=np.array(Image.open(f'{d}/beedle_tent_HD_2048.png').convert('RGBA')); tg=np.array(Image.open(f'{d}/beedle_tent_glow_512.png').convert('RGBA'))
night=tb.copy().astype(float); night[...,:3]*=np.array([0.30,0.33,0.45]); night=night.astype(np.uint8)
# the game's own props on the stage behind the curtain and the entry lantern
from rarc import rarc
from bmd import BMD
OBJ='/mnt/user-data/uploads/New Modding/Rom Dump/files/res/Object'
STAGE=pickle.load(open(f'{d}/beedle_stage_props.pkl','rb'))
PROPS=[]   # (texture arrays, tris) per prop, in tent space
for arcname, model, pos, yaw, sc in STAGE:
    r=rarc(open(f'{OBJ}/{arcname}.arc','rb').read()); key=[k for k in r if k.endswith('/'+model)][0]
    b=BMD(r[key]); c_,s_=math.cos(math.radians(yaw)),math.sin(math.radians(yaw)); Rm=np.array([[c_,0,s_],[0,1,0],[-s_,0,c_]])
    texs=[np.asarray(t,np.uint8) for t in b.tex]; tris=[]
    for mi,p,uv in b.tris:
        ti,col=b.mats[mi] if mi<len(b.mats) else (None,np.ones(4))
        if ti is None or ti>=len(texs):
            texs.append((np.ones((2,2,4))*np.r_[np.asarray(col)[:3],1]*255).astype(np.uint8)); ti=len(texs)-1
        tris.append((ti,(np.asarray(p,float)*sc)@Rm.T+np.asarray(pos),np.asarray(uv,float)))
    PROPS.append((arcname,pos,texs,tris))
def grade(t, night_mode, behind, lamp):
    t=t.astype(float)
    if lamp and night_mode: t[...,:3]=np.clip(t[...,:3]*np.array([1.9,1.6,1.1])+40,0,255)
    elif night_mode: t[...,:3]*= (np.array([1.1,0.9,0.66]) if behind else np.array([0.30,0.33,0.45]))
    elif behind: t[...,:3]*=np.array([0.62,0.55,0.46])
    return t.astype(np.uint8)
class B: pass
SS=3
def views(night_mode):
    b=B(); b.tex=[night if night_mode else tb, tg]
    b.tris=[(0,t[0],t[1]) for t in body]+([(1,t[0],t[1]) for t in glow] if night_mode else [])
    for arcname,pos,texs,tris in PROPS:
        base=len(b.tex); behind=pos[2]<210; lamp=arcname=='Obj_kntr'
        b.tex+= [grade(t,night_mode,behind,lamp) for t in texs]
        b.tris+=[(base+ti,p,uv) for ti,p,uv in tris]
    b.mats=[(i,np.ones(4)) for i in range(len(b.tex))]
    P=np.array([t[0] for t in body]).reshape(-1,3)
    ims=[]
    for y in yaws:
        if crop: bounds=(np.array([crop[0],crop[1],-1e4]),np.array([crop[2],crop[3],1e4]))
        else:
            a=math.radians(y); c,s=math.cos(a),math.sin(a); Rm=np.array([[c,0,s],[0,1,0],[-s,0,c]]); Q=P@Rm.T
            bounds=(Q.min(0),Q.max(0))
        bg=(26,30,48) if night_mode else (120,128,120)
        ims.append(render(b,y,W*SS,H*SS,pad=12*SS,bounds=bounds,bg=bg).resize((W,H),Image.LANCZOS))
    return ims
day=views(False); nite=views(True)
o=Image.new('RGB',(W*len(yaws),H*2))
for i,(a,b_) in enumerate(zip(day,nite)): o.paste(a,(i*W,0)); o.paste(b_,(i*W,H))
o.save(out)
