"""Field around Beedle's tent -> per-material meshes (positions, uv0, vertex colours) + textures, for Blender."""
import sys, numpy as np, pickle, os
sys.path[:0]=['/home/claude/anim']
from rarc import rarc; from bmd import BMD,u16,u32
from PIL import Image
from bmd import decode_tex, _c565, _c5a3
STG=rarc(open('/mnt/user-data/uploads/Stage/F_SP122/STG_00.arc','rb').read())
SHARED={k.split('/')[-1][:-4]:v for k,v in STG.items() if k.endswith('.bti')}
def bti(d):
    fmt,w,hh=d[0],u16(d,2),u16(d,4); pfmt,pcnt,poff=d[9],u16(d,0xA),u32(d,0xC); doff=u32(d,0x1C)
    pal=None
    if fmt in (8,9):
        pal=[]
        for k in range(pcnt):
            v=u16(d,poff+2*k); pal.append(((v>>8,)*3+(v&255,)) if pfmt==0 else _c565(v) if pfmt==1 else _c5a3(v))
    return decode_tex(d[doff:],fmt,w,hh,pal)
C=np.array([-72921.4,-1551.7,5115.4]); R=30000
os.makedirs('tex',exist_ok=True)
out={}
def names_of(b,sec,off_idx=None):
    d=b.d
    if sec=='MAT3':
        o=b.sec['MAT3']; offs=[u32(d,o+0xC+4*i) for i in range(30)]; no=o+offs[2]
    else:
        t=b.sec['TEX1']; no=t+u32(d,t+0x10)
    cnt=u16(d,no); r=[]
    for i in range(cnt):
        so=u16(d,no+4+4*i+2); r.append(d[no+so:d.index(b'\0',no+so)].decode())
    return r
for f in ['R08_00','R16_00','R17_00']:
    a=rarc(open(f'/mnt/user-data/uploads/Stage/F_SP122/{f}.arc','rb').read())
    for k,v in a.items():
        if not k.endswith('.bmd'): continue
        b=BMD(v)
        try: b.jworld=[b.world(i) for i in range(len(b.jloc))]; b.read_shp()
        except Exception as e: print('skip',k,e); continue
        mn=names_of(b,'MAT3'); tn=names_of(b,'TEX1')
        t1=b.sec['TEX1']; ho=t1+u32(b.d,t1+0xC)
        for i,t in enumerate(b.tex):
            if u32(b.d,ho+0x20*i+0x1C)==0:          # the image lives in the stage's shared textures
                if tn[i].lower() not in SHARED: print('missing shared texture',tn[i]); continue
                t=bti(SHARED[tn[i].lower()])
            p=f'tex/{tn[i]}.png'
            Image.fromarray(np.asarray(t,np.uint8)).save(p)
        for ti,(mi,P,UV) in enumerate(b.tris):
            c=P.mean(0)
            if np.hypot(c[0]-C[0],c[2]-C[2])>R: continue
            tex,col=b.mats[mi]
            key=(mn[mi], tn[tex] if tex is not None and tex<len(tn) else None)
            e=out.setdefault(key,{'P':[],'UV':[],'C':[]})
            e['P'].append(P-C); e['UV'].append(UV); e['C'].append(np.asarray(b.tcol[ti],float)/255 if np.asarray(b.tcol[ti]).max()>1.5 else np.asarray(b.tcol[ti],float))
for k,e in out.items():
    for f in e: e[f]=np.array(e[f],np.float32)
pickle.dump(out,open('field_mats.pkl','wb'))
for k,e in sorted(out.items()): print(k,len(e['P']), 'col range %.2f-%.2f'%(e['C'][...,:3].min(),e['C'][...,:3].max()), 'alpha %.2f-%.2f'%(e['C'][...,3].min(),e['C'][...,3].max()))
