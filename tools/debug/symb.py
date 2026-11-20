import struct, sys, pickle, bisect
exec(open('/home/claude/scripts/mdmp.py').read().split('# exception stream')[0])
code = pickle.load(open('/home/claude/scripts/code.pkl','rb')); strs = open('/home/claude/scripts/strs.bin','rb').read()
rv = [c[0] for c in code]
exe = [b for b,s,n in mods if n.lower().endswith('dusklight.exe')][0]
def sym(a):
    w = where(a)
    if 'dusklight.exe' in w:
        r = a - exe; i = bisect.bisect_right(rv, r) - 1
        o = code[i][1]; return '%s+0x%x' % (strs[o:strs.index(b'\0', o)].decode('latin1')[-110:], r - code[i][0])
    return w
sz, rva = streams[6]; tidx = struct.unpack('<I', d[rva:rva+4])[0]
code_, = struct.unpack('<I', d[rva+8:rva+12]); addr = struct.unpack('<Q', d[rva+24:rva+32])[0]
print('crash thread', tidx, 'code %08x' % code_, sym(addr))
ctxsz, ctxrva = struct.unpack('<II', d[rva+160:rva+168])
sz, rva = streams[3]; cnt = struct.unpack('<I', d[rva:rva+4])[0]
want = sys.argv[2:] or [str(tidx)]
for i in range(cnt):
    o = rva + 4 + i*48
    tid, susp, pcls, prio, teb, stst, stsz, strva, ctsz, ctrva = struct.unpack('<IIIIQQIIII', d[o:o+48])
    if str(tid) not in want and 'all' not in want: continue
    ctx = d[ctrva:ctrva+ctsz]; rip = struct.unpack('<Q', ctx[0xF8:0x100])[0]
    stk = d[strva:strva+stsz]; hits = []
    for k in range(0, len(stk)-7, 8):
        v = struct.unpack('<Q', stk[k:k+8])[0]; w = where(v)
        if w != '?' and ('.exe' in w or 'mod.dll' in w): hits.append(sym(v))
    print('== thread', tid, 'rip', sym(rip), 'stack', hex(stsz))
    seen = []
    for h in hits:
        if h not in seen: seen.append(h)
    for h in seen[:45]: print('   ', h)
