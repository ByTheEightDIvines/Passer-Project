import sys, zipfile, numpy as np
sys.path[:0]=['/home/claude/w/anim','/home/claude/pp/tools/looks']
from rarc import rarc; from render import render
import regions as R, recolor as C
from PIL import Image, ImageDraw
z=zipfile.ZipFile('/home/claude/DT_2ca2cf0.dusk')
types=[int(a) for a in sys.argv[1:]] or list(C.BODY)
for t in types:
    arc,face,hair,skin=C.BODY[t]
    r=rarc(z.read(f'overlay/res/Object/{arc}.arc')); d=[v for k,v in r.items() if k.endswith('.bmd')][0]
    m,names=R.load(d); ti,img=R.body_texture(m); ids,_=R.islands(m,names,ti,img.shape)
    hm,sm=C.body_masks(t,m,ti,img,ids)
    same=R.same_textures(m,ti); orig=m.tex[ti]; cols=[('original',orig)]
    for v in C.VARIANTS[t]:
        nt=C.apply(img,hm,sm,*v,keep=C.body_keep(t,m,ti,ids)).astype(np.asarray(orig).dtype); cols.append((f'{v[0]} hair',nt))
    tiles=[]
    for lab,tex in cols:
        for k in same: m.tex[k]=tex
        Hh,Ww=ids.shape
        FP=[]
        for q in m.tris:
            if m.mats[q[0]][0] not in same: continue
            if R.tri_island(m,ids,q) in face: FP.append(np.array(q[1]))
        FP=np.array(FP).reshape(-1,3); cy=(FP[:,1].min()+FP[:,1].max())/2; hb=(np.array([-30,cy-30,-80]),np.array([30,cy+30,80]))
        a=render(m,yaw_deg=-25,W=260,H=400); b=render(m,yaw_deg=155,W=260,H=400)
        hf=render(m,yaw_deg=-20,W=200,H=200,bounds=hb); hk=render(m,yaw_deg=160,W=200,H=200,bounds=hb)
        tile=Image.new('RGB',(520,624),(46,46,52)); tile.paste(a,(0,24)); tile.paste(b,(260,24)); tile.paste(hf,(60,424)); tile.paste(hk,(260,424))
        ImageDraw.Draw(tile).text((8,6),lab,fill=(235,235,235)); tiles.append(tile)
    for k in same: m.tex[k]=orig
    W=520*min(len(tiles),3); rows=(len(tiles)+2)//3
    out=Image.new('RGB',(W,624*rows+26),(36,36,40)); ImageDraw.Draw(out).text((8,6),f'Body #{t} ({arc})',fill=(255,220,150))
    for i,tl in enumerate(tiles): out.paste(tl,((i%3)*520,26+(i//3)*624))
    out.save(f'/home/claude/bodies/var/body_{t}.png')
    # mask debug
    dbg=np.zeros(img.shape[:2]+(3,),np.uint8); dbg[...,0]=(hm*255); dbg[...,1]=(sm*255); dbg[...,2]=(ids>0)*60
    Image.fromarray(np.concatenate([img[...,:3],dbg],1)).resize((1024,1024),Image.NEAREST).save(f'/home/claude/bodies/var/mask_{t}.png')
    print(t,'ok')
