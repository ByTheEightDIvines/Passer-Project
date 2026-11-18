"""The royal archer's bow, redone: the bowman's bow file (the Bulblin's joints and skinning kept, so the game
code poses it the same), its arms slimmed, and a new 32x32 texture in the royal colours: plum lacquer arms
with a gold inlay line down each face, silver and gold bands at the grip, a royal red leather grip laced in
gold, a linen string.
Texture rows (v from the top): 0-15 every arm section (the root-to-first-ring section's outer half too),
16-28 the grip end of that section, 29-31 the string. Each arm face is half the width (u 0-0.5, 0.5-1).
Usage: python3 -I royal_bow.py psr_bow.bmd OUT.bmd [OUT_texture.png]"""
import sys, struct
sys.path[:0] = ['/home/claude/w/anim']
import numpy as np
from bmdwrite import encode_cmpr

src, out = sys.argv[1:3]
d = bytearray(open(src, 'rb').read())
secs = {}
o = 32
while o < len(d):
    tag = bytes(d[o:o + 4]); size = struct.unpack_from('>I', d, o + 4)[0]; secs[tag] = o; o += size

LAC, LAC_HI, LAC_LO = (78, 28, 40), (112, 44, 58), (46, 16, 24)
GOLD, GOLD_LO = (226, 180, 64), (150, 104, 30)
SILV, SILV_LO = (216, 210, 182), (146, 140, 112)
RED, RED_LO = (156, 30, 32), (98, 18, 22)
LINEN = (228, 222, 202)

T = np.zeros((32, 32, 3), np.uint8)
for y in range(32):
    for x in range(32):
        fx = (x % 16) / 15.0                      # across one face, 0 and 1 are its edges
        if y <= 14:                               # the arms
            shade = 1.0 - 0.35 * abs(fx - 0.5) * 2  # darker toward the edges, lacquer sheen in the middle
            c = np.array(LAC) * shade + np.array(LAC_HI) * (1 - shade) * 0.0
            if 0.3 < fx < 0.55: c = np.array(LAC_HI)
            if x % 16 in (0, 15): c = np.array(LAC_LO)
            if x % 16 == 7: c = np.array(GOLD)    # the inlay line
        elif y == 15: c = np.array(SILV_LO)
        elif y == 16: c = np.array(SILV)
        elif y == 17: c = np.array(GOLD)
        elif y <= 26:                             # the grip: red leather, gold lacing crossing
            k = (x + (y - 18)) % 8; j = (x - (y - 18)) % 8
            c = np.array(GOLD if k == 0 or j == 0 else (RED if (x + y) % 3 else RED_LO))
            if (k == 0 or j == 0) and (x + y) % 4 == 0: c = np.array(GOLD_LO)
        elif y == 27: c = np.array(GOLD)
        elif y == 28: c = np.array(SILV)
        else: c = np.array(LINEN) - (0 if x % 3 else 18)
        T[y, x] = np.clip(c, 0, 255)

t = secs[b'TEX1']; hdr = t + struct.unpack_from('>I', d, t + 0xC)[0]
assert d[hdr] == 14 and struct.unpack_from('>HH', d, hdr + 2) == (32, 32)
data = hdr + struct.unpack_from('>I', d, hdr + 0x1C)[0]
d[data:data + 512] = encode_cmpr(T)

# slimmer arms: every ring pulled in toward its own middle (the grip ring a little, the arm rings more);
# the tips, the string ends and the string's middle stay where they are
v = secs[b'VTX1']; fo = v + struct.unpack_from('>I', d, v + 8)[0]
a, ct, dt, fr = struct.unpack_from('>IIIB', d, fo)
assert (a, ct, dt) == (9, 1, 4)
ps = v + struct.unpack_from('>I', d, v + 0xC)[0]
n = 32
P = np.array(struct.unpack_from('>%df' % (3 * n), d, ps)).reshape(n, 3)
rings = {}
for i, p in enumerate(P):
    key = round(p[0])
    if key in (0, 25, 51) and not (key == 0 and i in (4, 5)):
        rings.setdefault(key, []).append(i)
for key, idx in rings.items():
    c = P[idx].mean(0); k = 0.85 if key == 0 else 0.72
    for i in idx: P[i, 1:] = c[1:] + (P[i, 1:] - c[1:]) * k
struct.pack_into('>%df' % (3 * n), d, ps, *P.ravel())
open(out, 'wb').write(d)
if len(sys.argv) > 3:
    from PIL import Image
    Image.fromarray(T).resize((256, 256), 0).save(sys.argv[3])
print('rings', {k: len(x) for k, x in rings.items()})
