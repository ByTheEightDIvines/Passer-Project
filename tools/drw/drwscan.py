"""Scan every .bmd/.bdl inside a .dusk (and the .arc files in it) for the DRW1 layout J3D assumes:
[rigid][env 0..N-1][env 0..N-1]. Reports drw/evp counts, overflow, wrong_slots, canonical."""
import sys, struct, zipfile, io
sys.path[:0] = ['/home/claude/w/anim']
from rarc import rarc, yaz0

def secs(d):
    o = 32; r = {}
    while o + 8 <= len(d):
        tag = d[o:o + 4]; sz = struct.unpack('>I', d[o + 4:o + 8])[0]
        if sz == 0: break
        r[tag] = (o, d[o:o + sz]); o += sz
    return r

def scan(d):
    S = secs(d)
    if b'DRW1' not in S or b'EVP1' not in S or b'SHP1' not in S: return None
    w = S[b'DRW1'][1]; n = struct.unpack('>H', w[8:10])[0]; fo, io_ = struct.unpack('>II', w[12:20])
    flags = list(w[fo:fo + n]); idx = list(struct.unpack(f'>{n}H', w[io_:io_ + 2 * n]))
    E = struct.unpack('>H', S[b'EVP1'][1][8:10])[0]
    if E == 0 and all(f == 0 for f in flags):
        return dict(drw=n, evp=0, overflow=0, wrong_slots=0, canonical=True, skinned=False)
    R = sum(1 for f in flags if f == 0)                       # the game's full-weight count
    entry = n - E                                             # mEntryNum
    overflow = max(0, R + E - entry)
    s = S[b'SHP1'][1]; ns = struct.unpack('>H', s[8:10])[0]; so = struct.unpack('>8I', s[12:44])
    shp, _, _, _, mtxidx, _, mtxdata, _ = so
    used = set()
    for i in range(ns):
        en = shp + i * 0x28; npk, ao, fmi = struct.unpack('>HHH', s[en + 2:en + 8])
        for k in range(npk):
            m = mtxdata + (fmi + k) * 8; u, mc, ms = struct.unpack('>HHI', s[m:m + 8])
            for j in range(mc):
                x = struct.unpack('>H', s[mtxidx + (ms + j) * 2: mtxidx + (ms + j) * 2 + 2])[0]
                if x != 0xFFFF: used.add(x)
    wrong = 0; above = 0
    for x in used:
        if x >= n: wrong += 1; continue
        want = (flags[x], idx[x])
        got = (0, idx[x]) if x < R else (1, x - R)
        if got != want: wrong += 1
        if x >= entry: above += 1
    rigid = []
    for k in range(n):
        if flags[k] == 0 and idx[k] not in rigid: rigid.append(idx[k])
    canon = flags == [0] * len(rigid) + [1] * E + [1] * E and idx == rigid + list(range(E)) + list(range(E))
    return dict(drw=n, evp=E, overflow=overflow, wrong_slots=wrong, above_entry=above, canonical=canon, skinned=True)

def walk(name, data, out):
    if data[:4] == b'Yaz0': data = yaz0(data)
    if data[:4] == b'RARC':
        for k, v in rarc(data).items(): walk(name + ':' + k, v, out)
    elif data[:8] in (b'J3D2bmd3', b'J3D2bdl4'):
        r = scan(data)
        if r: out.append((name, r))

if __name__ == '__main__':
    for p in sys.argv[1:]:
        z = zipfile.ZipFile(p); out = []
        for n in z.namelist():
            if n.lower().endswith(('.arc', '.bmd', '.bdl', '.szs')): walk(n, z.read(n), out)
        bad = [(n, r) for n, r in out if r['skinned'] and (r['overflow'] or r['wrong_slots'] or not r['canonical'])]
        print(f'{p}: {len(out)} models, {sum(r["skinned"] for _, r in out)} skinned, {len(bad)} bad')
        for n, r in bad: print('  BAD ', n, r)
