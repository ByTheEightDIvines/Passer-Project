"""Beedle's blink: two eye textures (half-closed, shut) appended to his models' TEX1, and a btp for the
eye material m0 on each: open, half, shut, half, open (5 frames, as Wind Waker's maba.btp)."""
import sys,struct; sys.path[:0]=['/home/claude/w/anim']
import numpy as np; from PIL import Image
from bmdwrite import encode_cmpr_rgba
def h16(s):
    h=0
    for c in s.encode(): h=(h*3+c)&0xFFFF
    return h
assert h16('SC_eye')==0x735e
def pad(b,a=32,fill=b'\0'): return b+fill*((-len(b))%a)
def add_textures(bmd, imgs, names, like=0, alpha=True):
    d=bytearray(bmd); o=d.find(b'TEX1'); assert o>0
    size=struct.unpack_from('>I',d,o+4)[0]; assert o+size==len(d) or o+size==len(d)-(len(d)-o-size)
    n=struct.unpack_from('>H',d,o+8)[0]; ho=o+struct.unpack_from('>I',d,o+0xC)[0]; so=o+struct.unpack_from('>I',d,o+0x10)[0]
    hdr=[bytearray(d[ho+32*i:ho+32*i+32]) for i in range(n)]
    data=[]
    for i in range(n):
        h=hdr[i]; fmt,w,hh=h[0],struct.unpack_from('>H',h,2)[0],struct.unpack_from('>H',h,4)[0]
        assert fmt==14 and h[0x18]==1, (fmt,h[0x18])
        st=ho+32*i+struct.unpack_from('>I',h,0x1C)[0]; data.append(bytes(d[st:st+w*hh//2]))
    old=[]; cnt=struct.unpack_from('>H',d,so)[0]
    for i in range(cnt):
        off=struct.unpack_from('>H',d,so+4+4*i+2)[0]; old.append(d[so+off:d.index(b'\0',so+off)].decode())
    eye=hdr[like]
    for im,nm in zip(imgs,names):
        a=np.asarray(im.convert('RGBA'),np.uint8)
        h=bytearray(eye); struct.pack_into('>HH',h,2,a.shape[1],a.shape[0])
        hdr.append(h); data.append(encode_cmpr_rgba(a) if alpha else __import__('bmdwrite').encode_cmpr(a[...,:3])); old.append(nm)
    N=len(hdr)
    # string table
    st=struct.pack('>HH',N,0xFFFF); off=4+4*N; body=b''
    for nm in old:
        st+=struct.pack('>HH',h16(nm),off+len(body)); body+=nm.encode()+b'\0'
    strtab=pad(st+body,4)
    hdr_off=0x20; str_off=hdr_off+32*N; data_off=(str_off+len(strtab)+31)//32*32
    blob=b''; offs=[]
    for i,dd in enumerate(data):
        offs.append(data_off+len(blob)); blob+=pad(dd,32)
    out=bytearray(b'TEX1'+b'\0'*4+struct.pack('>HH',N,0xFFFF)+struct.pack('>II',hdr_off,str_off)+b'\0'*0x0C)
    for i,h in enumerate(hdr):
        struct.pack_into('>I',h,0x1C,offs[i]-(hdr_off+32*i)); out+=h
    out+=strtab; out=pad(bytes(out),32); assert len(out)==data_off; out=bytearray(out+blob)
    struct.pack_into('>I',out,4,len(out))
    res=bytearray(d[:o])+out; struct.pack_into('>I',res,8,len(res))
    return bytes(res), list(range(n,N))
def btp(mat, seq):
    T=bytearray(b'TPT1'+b'\0'*4)
    T+=struct.pack('>BBHHH',2,0xFF,len(seq),1,len(seq))
    T+=struct.pack('>IIII',0x20,0x28,0x28+((2*len(seq)+3)//4*4),0)   # anim, indices, remap, names (set below)
    T+=struct.pack('>HHBBH',len(seq),0,0,0xFF,0xFFFF)                 # 0x20: the one material's table
    T+=b''.join(struct.pack('>H',s) for s in seq); T=bytearray(pad(bytes(T),4,b'\0'))
    rem=len(T); T+=struct.pack('>H',0); T=bytearray(pad(bytes(T),4,b'\0'))
    nm=len(T); T+=struct.pack('>HHHH',1,0xFFFF,h16(mat),8)+mat.encode()+b'\0'
    T=bytearray(pad(bytes(T),32,b'\0'))
    struct.pack_into('>II',T,0x18,rem,nm); struct.pack_into('>I',T,4,len(T))
    head=bytearray(b'J3D1btp1'+b'\0'*4+struct.pack('>I',1)+b'\xff'*16)
    out=head+T; struct.pack_into('>I',out,8,len(out)); return bytes(out)
if __name__=='__main__':
    imgs=[Image.open('/home/claude/beedle/eye_half.png'),Image.open('/home/claude/beedle/eye_shut.png')]
    for n in ['beedle','beedle_tent']:
        b=open('/home/claude/beedle/%s.bmd'%n,'rb').read()
        nb,idx=add_textures(b,imgs,['eye_half','eye_shut'])
        open('/home/claude/beedle/new_%s.bmd'%n,'wb').write(nb)
        open('/home/claude/beedle/new_%s.btp'%n,'wb').write(btp('m0',[0,idx[0],idx[1],idx[0],0]))
        print(n,'textures',idx,'bmd',len(b),'->',len(nb))
    # check our btp writer against Wind Waker's own file
    ww=open('/home/claude/beedle/maba.btp','rb').read(); mine=btp('SC_eye',[1,2,3,2,1])
    print('maba match except padding:', ww[:0x60]==mine[:0x60], ww[0x60:0x67]==mine[0x60:0x67])
