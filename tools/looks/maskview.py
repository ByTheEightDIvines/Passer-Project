import sys, zipfile, numpy as np
sys.path[:0]=['/home/claude/w/anim','/home/claude/pp/tools/looks']
from rarc import rarc; from render import render
import regions as R, recolor as C
from PIL import Image
from bmd import WrapTex
z=zipfile.ZipFile('/home/claude/DT_2ca2cf0.dusk')
for t in [int(a) for a in sys.argv[1:]]:
    arc,face,hair,skin=C.BODY[t]
    r=rarc(z.read(f'overlay/res/Object/{arc}.arc')); d=[v for k,v in r.items() if k.endswith('.bmd')][0]
    m,names=R.load(d); ti,img=R.body_texture(m); ids,_=R.islands(m,names,ti,img.shape)
    hm,sm=C.body_masks(t,m,ti,img,ids)
    T=img.copy(); lum=img[...,:3].mean(-1,keepdims=True)/255
    col=np.stack([hm,sm,np.clip(1-hm-sm,0,1)*0.6],-1)*255*(0.4+lum)
    T[...,:3]=np.clip(col,0,255)
    for k in R.same_textures(m,ti): m.tex[k]=T.view(WrapTex)
    a=render(m,yaw_deg=-20,W=420,H=600); b=render(m,yaw_deg=160,W=420,H=600)
    o=Image.new('RGB',(840,600)); o.paste(a,(0,0)); o.paste(b,(420,0)); o.save(f'/home/claude/bodies/var/mv_{t}.png')
