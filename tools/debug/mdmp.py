import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'MDMP'
n, diroff = struct.unpack('<II', d[8:16])
streams = {}
for i in range(n):
    t, sz, rva = struct.unpack('<III', d[diroff+i*12:diroff+i*12+12]); streams[t] = (sz, rva)
def ustr(rva):
    l = struct.unpack('<I', d[rva:rva+4])[0]; return d[rva+4:rva+4+l].decode('utf-16le')
mods = []
sz, rva = streams[4]
cnt = struct.unpack('<I', d[rva:rva+4])[0]
for i in range(cnt):
    o = rva + 4 + i*108
    base, size = struct.unpack('<QI', d[o:o+12]); name = ustr(struct.unpack('<I', d[o+20:o+24])[0])
    mods.append((base, size, name))
def where(a):
    for b, s, nme in mods:
        if b <= a < b+s: return '%s+0x%x' % ('/'.join(nme.split(chr(92))[-3:]), a-b)
    return '?'
# exception stream (6)
sz, rva = streams[6]
tid = struct.unpack('<I', d[rva:rva+4])[0]
code, flags, rec, addr, np_ = struct.unpack('<IIQQI', d[rva+8:rva+36])
params = struct.unpack('<15Q', d[rva+40:rva+40+120])
print('thread', tid, 'code %08x' % code, 'at', hex(addr), where(addr), 'params', [hex(p) for p in params[:np_]])
ctxsz, ctxrva = struct.unpack("<II", d[rva+160:rva+168])
ctx = d[ctxrva:ctxrva+ctxsz]
regs = struct.unpack('<16Q', ctx[0x78:0x78+128])
names = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15']
print({k: hex(v) for k, v in zip(names, regs)})
rip = struct.unpack('<Q', ctx[0xF8:0x100])[0]; print('rip', hex(rip), where(rip))
rsp = regs[4]
# memory list (5) and memory64 (9)
mem = []
if 5 in streams:
    sz, rva = streams[5]; c = struct.unpack('<I', d[rva:rva+4])[0]
    for i in range(c):
        st, dsz, drva = struct.unpack('<QII', d[rva+4+i*16:rva+20+i*16]); mem.append((st, dsz, drva))
def read(a, n):
    for st, sz_, r in mem:
        if st <= a and a+n <= st+sz_: return d[r+(a-st):r+(a-st)+n]
    return None
stk = read(rsp, 0x1000) or b''
print('stack bytes', len(stk))
for i in range(0, len(stk), 8):
    v = struct.unpack('<Q', stk[i:i+8])[0]
    w = where(v)
    if w != '?' and ('mod' in w.lower() or 'dusklight' in w.lower()): print(' rsp+0x%x' % i, w)
print([ (hex(b), nme.split('\\')[-1]) for b,s,nme in mods if 'mod' in nme.lower() or 'dusk' in nme.lower()][:20])
# whole stack region
for st, sz_, r in mem:
    if st <= rsp < st+sz_:
        full = d[r+(rsp-st):r+sz_]
        print('stack region to', hex(st+sz_), 'bytes', len(full))
        for i in range(0, len(full)-7, 8):
            v = struct.unpack('<Q', full[i:i+8])[0]; w = where(v)
            if w != '?' and 'mod.dll' in w: print(' MOD rsp+0x%x' % i, w)
        break
for b,s,nme in mods:
    if 'mod.dll' in nme: print(hex(b), hex(s), nme)
