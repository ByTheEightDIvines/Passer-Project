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
class B: pass
SS=3
def views(night_mode):
    b=B(); b.mats=[(0,np.ones(4)),(1,np.ones(4))]
    b.tex=[night if night_mode else tb, tg]
    b.tris=[(0,t[0],t[1]) for t in body]+([(1,t[0],t[1]) for t in glow] if night_mode else [])
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
