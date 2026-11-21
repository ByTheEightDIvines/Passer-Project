import sys; sys.path[:0]=['/home/claude/gu/tools']
import numpy as np
from rarc import rarc, yaz0
from bmd import BMD, joint_names
from bck import BCK, srt_matrix
def arc(p):
    d=open(p,'rb').read(); return rarc(yaz0(d) if d[:4]==b'Yaz0' else d)
BE=arc('in/P_Beedl.arc'); PK=arc('in/PsrPack.arc')
def get(A,end): return [v for k,v in A.items() if k.endswith(end)][0]
from render import render
from PIL import Image
B0=get(BE,'beedle.bmd')
bind=[m.copy() for m in BMD(B0).jloc]
def pose_locals(anim, t):
    L=[]
    for j in range(len(bind)):
        m=srt_matrix(*anim.sample(j,t))
        if j>0: m[:3,3]=bind[j][:3,3]
        L.append(m)
    return L
def model_at(L):
    b=BMD(B0); b.jloc=[x.copy() for x in L]; b.jworld=[None]*len(L)
    for i in range(len(L)): b.jworld[i]=b.world(i)
    b.read_shp(); return b
def img(b,yaw,W=300,H=300,bounds=None):
    im=np.asarray(render(b,yaw,W=W,H=H,bounds=bounds))
    if im.max()<=1.01: im=im*255
    return Image.fromarray(np.clip(im,0,255).astype('uint8'))
def strip(ims,path,cols=None):
    cols=cols or len(ims); w,h=ims[0].size; rows=(len(ims)+cols-1)//cols
    c=Image.new('RGB',(w*cols,h*rows),(46,46,52))
    for i,im in enumerate(ims): c.paste(im,((i%cols)*w,(i//cols)*h))
    c.save(path)
