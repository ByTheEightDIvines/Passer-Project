import os, sys, struct, zipfile
sys.path[:0] = ['/home/claude/w/anim', '/home/claude/drw']
from rarc import rarc, yaz0
from drwscan import secs
def matnames(d):
    S = secs(d)
    if b'MAT3' not in S: return []
    m = S[b'MAT3'][1]; n = struct.unpack('>H', m[8:10])[0]
    no = struct.unpack('>I', m[0x14:0x18])[0]
    cnt = struct.unpack('>H', m[no:no + 2])[0]; out = []
    for i in range(cnt):
        h, so = struct.unpack('>HH', m[no + 4 + i * 4: no + 8 + i * 4])
        e = m.index(b'\0', no + so); out.append(m[no + so:e].decode('latin1'))
    return out
def walk(name, data, res):
    if data[:4] == b'Yaz0': data = yaz0(data)
    if data[:4] == b'RARC':
        for k, v in rarc(data).items(): walk(name + ':' + k, v, res)
    elif data[:8] in (b'J3D2bmd3', b'J3D2bdl4'): res.append((name, matnames(data)))
if __name__ == '__main__':
    pre = [l.strip() for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'live_reflections_prefixes.txt')) if l.strip()]
    z = zipfile.ZipFile(sys.argv[1]); res = []
    for n in z.namelist():
        if n.lower().endswith(('.arc', '.bmd', '.bdl')): walk(n, z.read(n), res)
    for n, ms in res:
        hit = [m for m in ms if any(m.startswith(p) for p in pre)]
        if hit: print(n, hit)
    print('prefixes', pre)
