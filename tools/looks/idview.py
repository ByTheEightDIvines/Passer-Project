import sys, zipfile, numpy as np
sys.path[:0]=['/home/claude/w/anim','/home/claude/pp/tools/looks']
from rarc import rarc; from render import render
import regions as R, recolor as C
from PIL import Image, ImageDraw
z=zipfile.ZipFile('/home/claude/DT_2ca2cf0.dusk')
for t in [int(a) for a in sys.argv[1:]]:
    arc,face,hair,skin=C.BODY[t]
    r=rarc(z.read(f'overlay/res/Object/{arc}.arc')); d=[v for k,v in r.items() if k.endswith('.bmd')][0]
    m,names=R.load(d); ti,img=R.body_texture(m); ids,_=R.islands(m,names,ti,img.shape)
    orig=list(m.tex); tc=getattr(m,'tcol',None)
    idt=[]
    for k,T in enumerate(orig):
        a=np.zeros(T.shape,np.uint8); a[...,3]=T[...,3] if T.shape[-1]==4 else 255
        a[...,0]=k*12+6
        if k in R.same_textures(m,ti): a[...,0]=ti*12+6; a[...,1]=ids
        idt.append(a)
    Hh,Ww=ids.shape
    FP=np.array([q[1] for q in m.tris if m.mats[q[0]][0] in R.same_textures(m,ti) and R.tri_island(m,ids,q) in face]).reshape(-1,3)
    cy=(FP[:,1].min()+FP[:,1].max())/2; hb=(np.array([-34,cy-36,-80]),np.array([34,cy+30,80]))
    tiles=[]
    for yaw,bb,W,H in [(-20,hb,420,420),(160,hb,420,420),(-25,None,300,460),(155,None,300,460)]:
        m.tex=orig; m.tcol=tc; im=render(m,yaw_deg=yaw,W=W,H=H,bounds=bb)
        m.tex=idt; m.tcol=None; idim=np.asarray(render(m,yaw_deg=yaw,W=W,H=H,bounds=bb))
        dr=ImageDraw.Draw(im)
        for k in range(len(orig)):
            sel=(np.abs(idim[...,0].astype(int)-(k*12+6))<2)
            if k!=ti:
                if sel.sum()>30: ys,xs=np.nonzero(sel); dr.text((int(np.median(xs)),int(np.median(ys))),f'T{k}',fill=(255,255,0),stroke_width=2,stroke_fill=(0,0,0))
                continue
            for i in np.unique(idim[...,1][sel]):
                s2=sel&(idim[...,1]==i)
                if s2.sum()<25: continue
                ys,xs=np.nonzero(s2); lab=str(i)+('F' if i in face else 'H' if i in hair else 'S' if i in skin else '')
                dr.text((int(np.median(xs))-6,int(np.median(ys))-6),lab,fill=(255,255,255),stroke_width=2,stroke_fill=(0,0,0))
        tiles.append(im)
    out=Image.new('RGB',(1440,460),(30,30,30)); x=0
    for im in tiles: out.paste(im,(x,0)); x+=im.width
    ImageDraw.Draw(out).text((4,440),f'#{t} {arc} textures {[T.shape[:2] for T in orig]} body tex {ti}',fill=(255,200,120))
    out.save(f'/home/claude/bodies/var/id_{t}.png'); print(t)
