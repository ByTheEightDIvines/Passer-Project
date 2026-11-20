import sys
sys.argv = ['x', sys.argv[1], 'none']
src = open('/home/claude/scripts/symb.py').read()
exec(src)
sz, rva = streams[3]; cnt = struct.unpack('<I', d[rva:rva+4])[0]
for i in range(cnt):
    o = rva + 4 + i*48
    tid, susp, pcls, prio, teb, stst, stsz, strva, ctsz, ctrva = struct.unpack('<IIIIQQIIII', d[o:o+48])
    if tid != 22648: continue
    stk = d[strva:strva+stsz]
    words = [struct.unpack('<Q', stk[k:k+8])[0] for k in range(0, len(stk)-7, 8)]
    idx = [k for k, v in enumerate(words) if 'push_verts+' in sym(v) or 'draw_prim+' in sym(v) or 'gfx::push+' in sym(v)]
    for k in idx:
        print('---', sym(words[k]))
        for j in range(max(0, k-10), min(len(words), k+14)):
            v = words[j]; s = sym(v)
            print('  [%+d] 0x%016x %s' % (j-k, v, s if s != '?' else ''))
