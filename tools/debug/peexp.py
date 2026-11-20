import struct, sys, bisect
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack('<I', d[0x3c:0x40])[0]
nsec = struct.unpack('<H', d[pe+6:pe+8])[0]; optsz = struct.unpack('<H', d[pe+20:pe+22])[0]
opt = pe + 24
exp_rva, exp_sz = struct.unpack('<II', d[opt+112:opt+120])
secs = []
so = opt + optsz
for i in range(nsec):
    nm, vs, va, rs, ra = struct.unpack('<8sIIII', d[so+i*40:so+i*40+24]); secs.append((va, vs, ra))
def off(rva):
    for va, vs, ra in secs:
        if va <= rva < va + max(vs, 1): return rva - va + ra
nfuncs, nnames, af, an, ao = struct.unpack('<IIIII', d[off(exp_rva)+20:off(exp_rva)+40])
funcs = struct.unpack('<%dI' % nfuncs, d[off(af):off(af)+4*nfuncs])
names = struct.unpack('<%dI' % nnames, d[off(an):off(an)+4*nnames])
ords = struct.unpack('<%dH' % nnames, d[off(ao):off(ao)+2*nnames])
syms = sorted((funcs[o], d[off(n):d.index(b'\0', off(n))].decode()) for n, o in zip(names, ords))
addrs = [s[0] for s in syms]
print('exports', len(syms))
for a in [int(x, 16) for x in sys.argv[2:]]:
    i = bisect.bisect_right(addrs, a) - 1
    print(hex(a), '+0x%x' % (a - syms[i][0]), syms[i][1][:160], ' | next', syms[i+1][1][:60] if i+1 < len(syms) else '')
