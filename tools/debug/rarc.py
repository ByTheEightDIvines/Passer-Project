import struct
def yaz0(d):
    if d[:4]!=b'Yaz0': return d
    size=struct.unpack('>I',d[4:8])[0]; out=bytearray(); i=16
    while len(out)<size:
        code=d[i]; i+=1
        for b in range(8):
            if len(out)>=size: break
            if code&(0x80>>b): out.append(d[i]); i+=1
            else:
                b1,b2=d[i],d[i+1]; i+=2
                dist=((b1&0xF)<<8|b2)+1; n=b1>>4
                if n==0: n=d[i]+0x12; i+=1
                else: n+=2
                for _ in range(n): out.append(out[-dist])
    return bytes(out)
def rarc(d):
    d=yaz0(d)
    assert d[:4]==b'RARC'
    dataoff=struct.unpack('>I',d[12:16])[0]+0x20
    info=0x20
    nnodes,nodeoff,nents,entoff,strsz,stroff=struct.unpack('>IIIIII',d[info:info+24])
    nodeoff+=0x20; entoff+=0x20; stroff+=0x20
    def s(o): return d[stroff+o:d.index(b'\0',stroff+o)].decode('latin1')
    files={}
    for e in range(nents):
        idx,h,typ,no,off,sz=struct.unpack('>HHHHII',d[entoff+e*20:entoff+e*20+16])
        if typ>>8 & 2: continue
        files[s(typ&0xFFFF if False else no)]=d[dataoff+off:dataoff+off+sz]
    return files
