"""The royal archer's quiver, low poly (74 triangles; the old Bulblin one was 77), dressed in the guards' own
scabbard textures (psr_g_scabbard.bmd): its plum leather for the case with a gold Triforce on each broad face,
its banded metal fitting for the mouth rim and the foot. Five arrows as crossed cards (cut-out).
Same place and size as the bowman's quiver (axis and middle measured from psr_quiver.bmd, in the man's
backbone frame, joint 'quiver'), mouth and arrows at the same end.
Usage: python3 -I royal_quiver.py psr_quiver.bmd psr_g_scabbard.bmd OUT.bmd [OUT_atlas.png]"""
import sys
sys.path[:0] = ['/home/claude/w/anim', '/home/claude/anim']
import numpy as np
from bmd import BMD
from multibmd import write_multi
from PIL import Image, ImageDraw

old, scab, out = sys.argv[1:4]
q = BMD(open(old, 'rb').read())
P = np.array([t[1] for t in q.tris]).reshape(-1, 3)
C = P.mean(0); _, _, V = np.linalg.svd(P - C)
E0, E1, E2 = V[0], V[1], V[2]           # along it (mouth at -), across (wide), through (flat)
if np.dot(np.cross(E0, E1), E2) < 0: E2 = -E2
bag = np.array([t[1] for t in q.tris if t[0] == 0]).reshape(-1, 3) - C
OA = ((bag @ E1).min() + (bag @ E1).max()) / 2; OB = ((bag @ E2).min() + (bag @ E2).max()) / 2
def W(s, a, b): return C + s * E0 + (a + OA) * E1 + (b + OB) * E2

# ---- the atlas: leather (left half) and metal (right half), 64 x 128 ----
sb = BMD(open(scab, 'rb').read())
t0 = np.asarray(sb.tex[0])[..., :3].astype(np.uint8)       # 64 x 64: the leather strip, cols 36-44, rows 22-63
t1 = np.asarray(sb.tex[1])[..., :3].astype(np.uint8)       # 128 x 32: the banded fitting at the bottom
strip = t0[25:62, 36:45]                   # the dark ends left off (they tile as seams)
leather = np.tile(strip, (4, 4, 1))[:128, :32]
metal = np.asarray(Image.fromarray(t1[108:128, :]).resize((32, 64), Image.BILINEAR))
atlas = np.zeros((128, 64, 3), np.uint8)
atlas[:, :32] = leather
atlas[:64, 32:] = metal; atlas[64:, 32:] = metal
img = Image.fromarray(atlas); dr = ImageDraw.Draw(img)
GOLD, GOLD_LO = (226, 180, 64), (140, 96, 28)
# the Triforce, centred on the face (u 0.25 of the atlas = the middle of each broad face), drawn taller in
# texels than wide because the leather is stretched along the case (about 2.1 texels/unit down, 1.2 across)
cx, cy, w, h = 16, 46, 6, 14            # one small triangle: half width, height (texels)
for ox, oy in ((0, -h), (-w, 0), (w, 0)):
    x, y = cx + ox, cy + oy
    dr.polygon([(x, y - h / 2), (x - w, y + h / 2), (x + w, y + h / 2)], fill=GOLD, outline=GOLD_LO)
atlas = np.asarray(img)

# ---- the case: 8 sides; rim (metal), body (leather), foot (metal) ----
S0, S1 = -10.0, 60.0
RA, RB, N = 9.5, 4.5, 8
def ring(s, flare=1.0):
    k = (1.0 - 0.14 * (s - S0) / (S1 - S0)) * flare
    return [(RA * k * np.cos(2 * np.pi * i / N), RB * k * np.sin(2 * np.pi * i / N)) for i in range(N + 1)]
def uface(i):                           # each half of the case shows one full copy (u 0..1 across the half)
    return (i % (N // 2)) / (N // 2), ((i % (N // 2)) + 1) / (N // 2)
tris = []
def band(s0, s1, f0, u0, v0, v1):
    A, B = ring(s0, f0), ring(s1)
    for i in range(N):
        a, b = uface(i)
        ua, ub = u0 + a * 0.5, u0 + b * 0.5
        p = [W(s0, *A[i]), W(s0, *A[i + 1]), W(s1, *B[i + 1]), W(s1, *B[i])]
        uv = [(ua, v0), (ub, v0), (ub, v1), (ua, v1)]
        tris.append((np.array([p[0], p[1], p[2]]), np.array([uv[0], uv[1], uv[2]])))
        tris.append((np.array([p[0], p[2], p[3]]), np.array([uv[0], uv[2], uv[3]])))
band(S0 - 0.8, S0 + 5, 1.1, 0.5, 0.0, 0.48)      # the mouth rim
band(S0 + 5, S1 - 5, 1.0, 0.0, 0.0, 1.0)          # the leather
band(S1 - 5, S1, 1.0, 0.5, 0.52, 1.0)             # the foot
F = ring(S1)
for i in range(1, N - 1):                         # the foot's bottom
    tris.append((np.array([W(S1, *F[0]), W(S1, *F[i + 1]), W(S1, *F[i])]), np.array([(0.75, 0.9)] * 3)))

# ---- the arrows: crossed cards, cut out ----
AW, AH = 16, 64
ci = Image.new('RGBA', (AW, AH), (0, 0, 0, 0)); cd = ImageDraw.Draw(ci)
cd.rectangle([7, 0, 8, AH], fill=(104, 74, 44, 255))                        # the shaft
cd.polygon([(8, 4), (1, 9), (1, 22), (8, 20)], fill=(236, 232, 220, 255))  # the vanes
cd.polygon([(8, 4), (15, 9), (15, 22), (8, 20)], fill=(236, 232, 220, 255))
cd.rectangle([1, 13, 15, 15], fill=(156, 30, 32, 255))                      # a red stripe
cd.rectangle([6, 0, 9, 2], fill=GOLD + (255,))                              # the nock
card = np.asarray(ci)
arrows = []
for k, (a, b) in enumerate([(-4, -1.5), (0, 1.5), (4, -1.5), (-2, 2.5), (2.5, 0)]):
    top = S0 - 20 - (k % 2) * 3; bot = S0 + 12
    for th in (0.3 + k, 0.3 + k + np.pi / 2):
        dx, dy = 2.6 * np.cos(th), 2.6 * np.sin(th)
        p = [W(top, a - dx, b - dy), W(top, a + dx, b + dy), W(bot, a + dx, b + dy), W(bot, a - dx, b - dy)]
        uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
        arrows.append((np.array([p[0], p[1], p[2]]), np.array([uv[0], uv[1], uv[2]])))
        arrows.append((np.array([p[0], p[2], p[3]]), np.array([uv[0], uv[2], uv[3]])))

def rgba(t): t = np.asarray(t); return np.concatenate([t, np.full(t.shape[:2] + (1,), 255, np.uint8)], -1)
write_multi(out, [(rgba(atlas), False, tris), (card, True, arrows)], jointname='quiver', cull=0)
if len(sys.argv) > 4: Image.fromarray(atlas).resize((256, 512), 0).save(sys.argv[4])
print('case', len(tris), 'arrows', len(arrows), 'total', len(tris) + len(arrows))
