"""Structural checks on a J3D .bmd: the INF1 hierarchy (balanced, every joint/material/shape index in range,
each joint once), EVP1/DRW1 joint indices in range, section sizes/alignment, header counts."""
import struct, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from drwscan import secs
def check(d):
    p = []; S = secs(d)
    tags = [t for t in S]; p.append(('sections', [t.decode() for t in tags]))
    if struct.unpack('>I', d[8:12])[0] != len(d): p.append(('ERR file size field', struct.unpack('>I', d[8:12])[0], len(d)))
    nsec = struct.unpack('>I', d[12:16])[0]
    if nsec != len(S): p.append(('ERR section count', nsec, len(S)))
    for t, (o, s) in S.items():
        if o % 32: p.append(('WARN section not 32-aligned', t, o))
    J = S[b'JNT1'][1]; nj = struct.unpack('>H', J[8:10])[0]
    M = S[b'MAT3'][1]; nm = struct.unpack('>H', M[8:10])[0]
    H = S[b'SHP1'][1]; nsh = struct.unpack('>H', H[8:10])[0]
    I = S[b'INF1'][1]; ho = struct.unpack('>I', I[0x14:0x18])[0]
    p.append(('counts joints/mats/shapes', nj, nm, nsh))
    # joint remap/name tables
    jt = struct.unpack('>III', J[12:24]); 
    seenj = []; depth = 0; o = ho; maxd = 0; nodes = 0
    while True:
        typ, idx = struct.unpack('>HH', I[o:o + 4]); o += 4; nodes += 1
        if typ == 0: break
        if typ == 1: depth += 1; maxd = max(maxd, depth)
        elif typ == 2: depth -= 1
        elif typ == 0x10:
            if idx >= nj: p.append(('ERR hierarchy joint out of range', idx, nj))
            seenj.append(idx)
        elif typ == 0x11:
            if idx >= nm: p.append(('ERR hierarchy material out of range', idx, nm))
        elif typ == 0x12:
            if idx >= nsh: p.append(('ERR hierarchy shape out of range', idx, nsh))
        else: p.append(('ERR unknown node type', hex(typ)))
        if depth < 0: p.append(('ERR hierarchy closes too far',)); break
        if o >= len(I): p.append(('ERR hierarchy runs off INF1',)); break
    if depth != 0: p.append(('ERR hierarchy unbalanced', depth))
    missing = sorted(set(range(nj)) - set(seenj)); dup = sorted({j for j in seenj if seenj.count(j) > 1})
    if missing: p.append(('ERR joints never in hierarchy', missing))
    if dup: p.append(('ERR joints twice in hierarchy', dup))
    p.append(('hierarchy nodes/max depth', nodes, maxd))
    # first hierarchy joint should be joint 0 (root)
    # EVP1
    E = S[b'EVP1'][1]; ne = struct.unpack('>H', E[8:10])[0]
    co, io_, wo, mo = struct.unpack('>IIII', E[12:28])
    tot = sum(E[co:co + ne])
    ji = struct.unpack(f'>{tot}H', E[io_:io_ + 2 * tot]) if ne else ()
    bad = [j for j in ji if j >= nj]
    if bad: p.append(('ERR EVP1 joint out of range', sorted(set(bad))))
    nmtx = (max(ji) + 1) if ji else 0
    if ne and mo:
        avail = (len(E) - mo) // 48
        if avail < nmtx: p.append(('ERR EVP1 inverse bind matrices short', avail, nmtx))
    D = S[b'DRW1'][1]; n = struct.unpack('>H', D[8:10])[0]; fo, ido = struct.unpack('>II', D[12:20])
    fl = D[fo:fo + n]; ix = struct.unpack(f'>{n}H', D[ido:ido + 2 * n])
    for k in range(n):
        if fl[k] == 0 and ix[k] >= nj: p.append(('ERR DRW1 rigid joint out of range', k, ix[k]))
        if fl[k] == 1 and ix[k] >= ne: p.append(('ERR DRW1 envelope out of range', k, ix[k]))
    # INF1 header: matrix group count / vertex count / loader flags
    flags, _, mpk, nv = struct.unpack('>HHII', I[8:20])
    p.append(('INF1 flags/packets/vertices', hex(flags), mpk, nv))
    return p
if __name__ == '__main__':
    for f in sys.argv[1:]:
        print('==', f)
        for x in check(open(f, 'rb').read()): print('  ', *x)
