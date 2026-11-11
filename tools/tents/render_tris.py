import sys, pickle, numpy as np, math
sys.path.insert(0,'/home/claude/ptb/Passer_Travellers_Build/tools')
from render import render
from PIL import Image
tris=pickle.load(open(sys.argv[1],'rb')); tex=np.array(Image.open(sys.argv[2]).convert('RGBA'))
class B: pass
b=B(); b.tris=[(0,t[0],t[1]) for t in tris]; b.mats=[(0,np.ones(4))]; b.tex=[tex]
P=np.array([t[0] for t in tris]).reshape(-1,3)
yaws=[float(a) for a in sys.argv[4].split(',')]
W,H=int(sys.argv[5]),int(sys.argv[6])
# common bounds over all yaws so scale matches
mn=np.full(3,1e9); mx=np.full(3,-1e9)
for y in yaws:
    a=math.radians(y); c,s=math.cos(a),math.sin(a); R=np.array([[c,0,s],[0,1,0],[-s,0,c]]); Q=P@R.T
    mn=np.minimum(mn,Q.min(0)); mx=np.maximum(mx,Q.max(0))
SS=3
imgs=[render(b,y,W*SS,H*SS,pad=12*SS,bounds=(mn,mx),bg=(120,128,120)).resize((W,H),Image.LANCZOS) for y in yaws]
out=Image.new('RGB',(W*len(imgs),H))
for i,im in enumerate(imgs): out.paste(im,(i*W,0))
out.save(sys.argv[3])
