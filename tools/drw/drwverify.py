"""Check a fixed BMD against the original: every packet reference resolves (the game's way, in the fixed
file) to the same joint/envelope it meant in the original; the table fits; no slot at or above mEntryNum."""
import sys, struct
import os, sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drwscan import secs, scan
def refs(d):
    S = secs(d); w = S[b'DRW1'][1]; n = struct.unpack('>H', w[8:10])[0]; fo, io_ = struct.unpack('>II', w[12:20])
    flags = list(w[fo:fo + n]); idx = list(struct.unpack(f'>{n}H', w[io_:io_ + 2 * n]))
    s = S[b'SHP1'][1]; ns = struct.unpack('>H', s[8:10])[0]; so = struct.unpack('>8I', s[12:44])
    shp, _, _, _, mtxidx, _, mtxdata, _ = so; out = []
    for i in range(ns):
        en = shp + i * 0x28; npk, ao, fmi = struct.unpack('>HHH', s[en + 2:en + 8])
        for k in range(npk):
            m = mtxdata + (fmi + k) * 8; u, mc, ms = struct.unpack('>HHI', s[m:m + 8])
            out.append(('def', i, k, None if u == 0xFFFF else (flags[u], idx[u])))
            for j in range(mc):
                x = struct.unpack('>H', s[mtxidx + (ms + j) * 2: mtxidx + (ms + j) * 2 + 2])[0]
                out.append(('mtx', i, k, j, None if x == 0xFFFF else (flags[x], idx[x])))
    return out
def check(a, b):
    ra, rb = refs(a), refs(b); same = ra == rb
    r = scan(b)
    return dict(same_targets=same, refs=len(ra), **r)
if __name__ == '__main__':
    print(check(open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()))
