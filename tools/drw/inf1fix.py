"""INF1 hierarchy: the game (J3DJointTree::makeHierarchy) hangs a material on the joint whose child list
it is in, but opens the material's own list under the LAST JOINT met at that level; a shape there goes on
that joint's newest material. If a material comes after a sibling joint, its shape lands on a joint with no
material: null pointer, crash on load. check() walks it exactly as the game does; fix() moves materials
(with their child lists) to the front of each level, as the game's own models have them."""
import struct, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from drwscan import secs

def nodes(d):
    S = secs(d); off, I = S[b'INF1']; ho = struct.unpack('>I', I[0x14:0x18])[0]; o = ho; out = []
    while True:
        t, i = struct.unpack('>HH', I[o:o + 4]); out.append((t, i)); o += 4
        if t == 0: break
    return S, off, ho, out

def check(d):
    _, _, _, N = nodes(d); pos = [0]; bad = []
    mesh = {}   # joint -> has a material
    def walk(pj):
        cur = pj
        while True:
            t, i = N[pos[0]]
            if t == 1: pos[0] += 1; walk(cur); continue
            if t == 2: pos[0] += 1; return
            if t == 0: return
            pos[0] += 1
            if t == 0x10: cur = i
            elif t == 0x11:
                if pj is None: bad.append(('material with no joint', i))
                else: mesh[pj] = True
            elif t == 0x12:
                if pj is None or not mesh.get(pj): bad.append(('shape on a joint with no material', i, pj))
    walk(None)
    return bad

def fix(d):
    S, off, ho, N = nodes(d)
    def parse(k):   # -> list of (node, children or None), next index
        items = []
        while True:
            t, i = N[k]
            if t in (0, 2): return items, k
            if t == 1:
                kids, k = parse(k + 1); assert N[k][0] == 2; k += 1
                items[-1] = (items[-1][0], kids); continue
            items.append(((t, i), None)); k += 1
    def order(items):
        mats = [x for x in items if x[0][0] == 0x11]; rest = [x for x in items if x[0][0] != 0x11]
        return [(n, order(c) if c else c) for n, c in mats + rest]
    def emit(items):
        out = []
        for n, c in items:
            out.append(n)
            if c: out += [(1, 0)] + emit(c) + [(2, 0)]
        return out
    top, k = parse(0); assert N[k][0] == 0
    new = emit(order(top)) + [(0, 0)]
    assert len(new) == len(N) and sorted(new) == sorted(N), 'node set changed'
    d = bytearray(d)
    for j, (t, i) in enumerate(new): struct.pack_into('>HH', d, off + ho + 4 * j, t, i)
    return bytes(d)

if __name__ == '__main__':
    for f in sys.argv[1:]: print(f, check(open(f, 'rb').read()))
