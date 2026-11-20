import struct
def parse(d):
    assert d[:8]==b'JPAC2-10', d[:8]
    resNum,texNum,texOff=struct.unpack('>HHI',d[8:16])
    o=0x10; res=[]
    for _ in range(resNum):
        rid,blk,fld,key,tdb,_p=struct.unpack('>HHBBBB',d[o:o+8])
        start=o; o+=8; blocks=[]
        for b in range(blk):
            mg,sz=struct.unpack('>4sI',d[o:o+8]); blocks.append((mg.decode(),o,sz)); o+=sz
        res.append(dict(id=rid,start=start,end=o,blocks=blocks))
    # textures
    tex=[]; t=texOff
    for _ in range(texNum):
        mg,sz=struct.unpack('>4sI',d[t:t+8]); name=d[t+0xC:t+0x20].split(b'\0')[0].decode('latin1'); tex.append((name,t,sz)); t+=sz
    return res,tex,texOff
