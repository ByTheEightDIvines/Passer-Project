import struct, sys
def align(n, a=0x20): return (n + a - 1) & ~(a - 1)
def replace(arc, name, newdata):
    assert arc[:4] == b'RARC'
    hdr = bytearray(arc[:0x40])
    dataoff = struct.unpack('>I', arc[12:16])[0] + 0x20
    nnodes, nodeoff, nents, entoff, strsz, stroff = struct.unpack('>IIIIII', arc[0x20:0x38])
    entoff += 0x20; stroff += 0x20
    def s(o): return arc[stroff+o:arc.index(b'\0', stroff+o)].decode('latin1')
    ents = []
    for e in range(nents):
        p = entoff + e*20
        idx, h, tn, no, off, sz = struct.unpack('>HHHHII', arc[p:p+16])
        typ = tn >> 8
        if typ & 2: continue           # directories
        ents.append((p, s(no), off, sz))
    # keep the file data order as it is
    ents.sort(key=lambda x: x[2])
    head = bytearray(arc[:dataoff])
    data = bytearray()
    found = False
    for p, n, off, sz in ents:
        blob = newdata if n == name else arc[dataoff+off:dataoff+off+sz]
        found |= n == name
        newoff = len(data)
        data += blob
        data += b'\0' * (align(len(data)) - len(data))
        struct.pack_into('>II', head, p+8, newoff, len(blob))
    assert found, name
    total = len(head) + len(data)
    struct.pack_into('>I', head, 4, total)
    struct.pack_into('>I', head, 0x10, len(data))      # data length
    struct.pack_into('>I', head, 0x14, len(data))      # MRAM size (everything loads to main RAM)
    return bytes(head + data)
if __name__ == '__main__':
    arc = open(sys.argv[1], 'rb').read()
    print('header', arc[:0x20].hex())
    out = replace(arc, sys.argv[2], open(sys.argv[3], 'rb').read())
    open(sys.argv[4], 'wb').write(out)
