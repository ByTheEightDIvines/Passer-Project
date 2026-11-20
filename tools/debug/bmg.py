import struct
def load(p):
    d=open(p,'rb').read(); secs={}; o=0x20
    n=struct.unpack('>I',d[12:16])[0]
    for _ in range(n):
        tag=d[o:o+4]; sz=struct.unpack('>I',d[o+4:o+8])[0]; secs[tag]=d[o:o+sz]; o+=sz
    return secs
def entries(secs):
    inf=secs[b'INF1']; cnt,esz=struct.unpack('>HH',inf[8:12]); out=[]
    for i in range(cnt):
        e=inf[16+i*esz:16+(i+1)*esz]; out.append(e)
    return out,esz
def text(secs,off):
    dat=secs[b'DAT1'][8:]; o=off; s=''
    while True:
        c=dat[o]
        if c==0: break
        if c==0x1A:
            ln=dat[o+1]; s+='{'+dat[o+2:o+ln].hex()+'}'; o+=ln; continue
        s+=chr(c); o+=1
    return s
