"""Beedle's shop stage, R_PSR00, on the base that loads: the game's Kakariko interior settings (R_SP109),
cut to one room exactly as build_shop_stage.py did (stage settings, room table, layer list, room list,
demo banks: untouched). Added on top, each through the game's own mechanism (Dusklight d_stage.cpp,
d_kankyo.cpp, d_a_bg.cpp):
  - Hyrule Field's sky: its vrbox models, animation and textures in the stage files (dStage_Create makes
    the sky when vrbox_sora.bmd is there)
  - Hyrule Field's time-of-day lighting: F_SP122's Env/Col/PAL/VRB tables appended, every layer's table
    header pointed at them (lighting picks its entry by room number: entry 0 is F_SP122 room 8's)
  - time passing in our room (room table byte 2 & 3, the field's value 9)
  - room 0: F_SP122 room 8's terrain with the part under the tent cut away (model.bmd, cut_terrain.py)
    and the shop where the tent stands (model5.bmd: the room loader lights each model file by its slot,
    l_tevStrType {BG0, BG1, BG2, BG3, BG3, BG0}; model5 takes BG0 like the field's own ground: the sun by
    day, the moon and nearby lamp lights at night), its windows cut open, the shop's collision there,
    the spawn inside the curtain and the three lantern flames
Usage: python3 -I build_shop_kak.py ROM_STAGE_DIR SHOP_DIR OUT_DIR"""
import sys, os, struct, math, subprocess, hashlib
import numpy as np
sys.path[:0] = ['/home/claude/anim', '/home/claude/room']
from rarc import rarc
from rarcwrite import write_rarc
STAGE, SHOP, OUT = sys.argv[1:4]
NAME = 'R_PSR00'
TENT = (-72921.4, -1551.7, 5115.4)     # Beedle's tent (travel network node 16, F_SP122 room 8)
YAW = 9935
a_ = YAW / 65536 * 2 * math.pi; c_, s_ = math.cos(a_), math.sin(a_)
def place(x, y, z): return (TENT[0] + x * c_ + z * s_, TENT[1] + y, TENT[2] - x * s_ + z * c_)

def chunks(v):
    n = struct.unpack('>I', v[:4])[0]
    return {v[4 + 12 * i:8 + 12 * i].decode('latin1'): (8 + 12 * i, *struct.unpack('>II', v[8 + 12 * i:16 + 12 * i])) for i in range(n)}
def set_count(v, tag, n):
    C = chunks(v)
    if tag in C: struct.pack_into('>I', v, C[tag][0], n)

# ---- the field's lighting tables (F_SP122), byte for byte ----
fstg = rarc(open(f'{STAGE}/F_SP122/STG_00.arc', 'rb').read())
fdzs = [v for k, v in fstg.items() if k.endswith('stage.dzs')][0]
FC = chunks(fdzs)
_offs = sorted(o for _, _, o in FC.values()) + [len(fdzs)]
def field_block(tag):
    _, cnt, off = FC[tag]; nxt = min(x for x in _offs if x > off)
    return cnt, bytearray(fdzs[off:nxt])
ENV_SZ = 0x41
env_n, env = field_block('Env0')
env[0:ENV_SZ] = env[8 * ENV_SZ:9 * ENV_SZ]          # our room 0 lights as the field's room 8 (the tent's)
TABLES = {'Env': (env_n, env), 'Col': field_block('Col0'), 'PAL': field_block('PAL0'), 'VRB': field_block('VRB0')}
_, _, frt = FC['RTBL']
f_r8 = struct.unpack_from('>I', fdzs, frt + 4 * 8)[0]
TIME_PASS = fdzs[f_r8 + 2]                           # the field's room 8 value (9: time runs)
assert TIME_PASS & 3, TIME_PASS

# ---- stage settings: the Kakariko base as it loaded ----
stg = rarc(open(f'{STAGE}/R_SP109/STG_00.arc', 'rb').read())
files = {}
for k, data in stg.items():
    rel = k.split('/', 1)[1] if k.count('/') > 1 else k
    name = rel.split('/')[-1]
    if name.startswith('room') and name.endswith('.dzs') and name != 'room0.dzs': continue   # one room
    if name.startswith('door-knob'): continue
    files[rel] = bytearray(data)
dzs = files['dzs/stage.dzs']
set_count(dzs, 'RTBL', 1); set_count(dzs, 'MULT', 1)
for t in ('ACTR', 'ACTe', 'SCOe', 'SCO2', 'SCO3'): set_count(dzs, t, 0)
C = chunks(dzs)
_, n, o = C['SCLS']
struct.pack_into('>8sBbBBB', dzs, o, b'F_SP122', 0, 8, 0xF0, 0xFF, 0)
set_count(dzs, 'SCLS', 1)
_, _, rt = C['RTBL']
r0 = struct.unpack_from('>I', dzs, rt)[0]
dzs[r0 + 2] = TIME_PASS
# append the field tables (nothing else moves, so no offset inside the file changes) and point every
# layer's Env/Col/PAL/VRB header at them
placed = {}
for kind, (cnt, blk) in TABLES.items():
    while len(dzs) % 4: dzs.append(0)
    placed[kind] = (cnt, len(dzs)); dzs += blk
while len(dzs) % 32: dzs.append(0)
C = chunks(dzs)
for tag, (hdr, _, _) in C.items():
    if tag[:3] in placed and len(tag) == 4:
        struct.pack_into('>II', dzs, hdr, *placed[tag[:3]])   # hdr: the count field, then the offset
# the field's sky models, animation and textures (and the shared textures its terrain model takes)
for k, val in fstg.items():
    rel = k.split('/', 1)[1] if k.count('/') > 1 else k
    if rel.split('/')[0] in ('bmdp', 'btk', 'tex', 'texc'):
        assert rel not in files, rel
        files[rel] = bytearray(val)
os.makedirs(f'{OUT}/{NAME}', exist_ok=True)
write_rarc(f'{OUT}/{NAME}/STG_00.arc', 'STG_00', {k: bytes(v) for k, v in files.items()})

# ---- room 0 ----
room = rarc(open(f'{STAGE}/R_SP109/R00_00.arc', 'rb').read())
dzr = bytearray(room[[k for k in room if k.endswith('room.dzr')][0]])
C = chunks(dzr)
for t in list(C):
    if t[:3] in ('ACT', 'SCO', 'LGT', 'TRE') or t in ('ACTR', 'SCOB', 'Door', 'REVT', 'RCAM', 'RARO'): set_count(dzr, t, 0)
face = (YAW + 0x8000) & 0xFFFF; face = face - 0x10000 if face > 0x7FFF else face
_, n, o = C['PLYR']
struct.pack_into('>8sI3f3hH', dzr, o, b'Link', 0xFF00003F, *place(0.0, 0.0, 250.0), 0, face, 0, 0xFFFF)
set_count(dzr, 'PLYR', 1)
_, n, o = C['SCLS']
struct.pack_into('>8sBbBBB', dzr, o, b'F_SP122', 0, 8, 0xF0, 0xFF, 0)
set_count(dzr, 'SCLS', 1)
# the three hanging lanterns: the game's own lamp flame (fireWd2) with its point light; switch 0x20 puts
# them out (the mod sets it by day, clears it at night)
acts = b''.join(struct.pack('>8sI3f3hH', b'fireWd2', 0xFF010120, *place(0.0, 340.0, lz), 0, 0, 0, 0xFFFF)
                for lz in (-330.0, 0.0, 330.0))
while len(dzr) % 4: dzr.append(0)
C = chunks(dzr)
struct.pack_into('>II', dzr, C['ACTR'][0], 3, len(dzr))
dzr += acts
# ---- the shop model, in the world where the tent stands, its windows open ----
# the windows painted on the canvas (measured off the wall texture), cut through: (wall, u range, y range)
# back wall z = -500 (u = x); side walls x = +-400 (u = z)
WINDOWS = [('back', (-60.0, 60.0), (255.0, 345.0)),
           ('side', (-262.0, -134.0), (84.0, 176.0)), ('side', (134.0, 262.0), (84.0, 176.0))]
def wall_uv(kind, P):
    if kind == 'back':
        return (P[:, 0], P[:, 1]) if np.all(np.abs(P[:, 2] + 500.0) < 3.0) else None
    for sx in (400.0, -400.0):
        if np.all(np.abs(P[:, 0] - sx) < 3.0): return (P[:, 2], P[:, 1])
    return None
def cut_holes(tris):
    out = []
    for ti, P, UV in tris:
        P = np.asarray(P, float); UV = np.asarray(UV, float)
        pieces = [(P, UV)]
        for kind, (u0, u1), (y0, y1) in WINDOWS:
            nxt = []
            for Pp, Uq in pieces:
                w = wall_uv(kind, Pp)
                if w is None or w[0].max() <= u0 or w[0].min() >= u1 or w[1].max() <= y0 or w[1].min() >= y1:
                    nxt.append((Pp, Uq)); continue
                axis_u = 0 if kind == 'back' else 2
                f_u = lambda V: V[0][axis_u]; f_y = lambda V: V[0][1]
                regions = [[lambda V: u0 - f_u(V)], [lambda V: f_u(V) - u1],
                           [lambda V: f_u(V) - u0, lambda V: u1 - f_u(V), lambda V: y0 - f_y(V)],
                           [lambda V: f_u(V) - u0, lambda V: u1 - f_u(V), lambda V: f_y(V) - y1]]
                for fs in regions:
                    poly = [(Pp[k], Uq[k]) for k in range(3)]
                    for f in fs:
                        new = []
                        for k in range(len(poly)):
                            A, B = poly[k], poly[(k + 1) % len(poly)]
                            fa, fb = f(A), f(B)
                            if fa >= 0: new.append(A)
                            if (fa >= 0) != (fb >= 0):
                                t = fa / (fa - fb); new.append((A[0] + (B[0] - A[0]) * t, A[1] + (B[1] - A[1]) * t))
                        poly = new
                        if len(poly) < 3: break
                    for k in range(1, len(poly) - 1):
                        nxt.append((np.array([poly[0][0], poly[k][0], poly[k + 1][0]]), np.array([poly[0][1], poly[k][1], poly[k + 1][1]])))
            pieces = nxt
        out += [(ti, Pp, Uq) for Pp, Uq in pieces
                if np.linalg.norm(np.cross(Pp[1] - Pp[0], Pp[2] - Pp[0])) > 1e-3]   # no zero-area slivers along the cuts
    return out
import scene
from multibmd import write_multi
S, TX = scene.build(False, clear_floor=False)
S = cut_holes(S)
groups = {}; order = []
for ti, P, UV in S:
    Q = np.array([place(*p) for p in np.asarray(P, float)])
    tex = (np.clip(TX[ti], 0, 1) * 255).astype(np.uint8)
    if tex.shape[2] == 3: tex = np.dstack([tex, np.full(tex.shape[:2], 255, np.uint8)])
    k = hashlib.md5(tex.tobytes()).hexdigest()
    if k not in groups: groups[k] = (tex, bool(tex[..., 3].min() < 128), []); order.append(k)
    groups[k][2].append((Q, np.asarray(UV, float)))
write_multi(f'{SHOP}/shop_world.bmd', [groups[k] for k in order], jointname='shop', cull=0)
# the shop's collision where the tent stands
subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shop_collision.py'),
                SHOP, str(TENT[0]), str(TENT[1]), str(TENT[2]), str(YAW)], check=True)
r8 = rarc(open(f'{STAGE}/F_SP122/R08_00.arc', 'rb').read())
open(f'{SHOP}/field_r8.bmd', 'wb').write([v for k, v in r8.items() if k.endswith('model.bmd')][0])
subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cut_terrain.py'),
                f'{SHOP}/field_r8.bmd', f'{SHOP}/field_r8_cut.bmd', str(TENT[0]), str(TENT[1]), str(TENT[2]), str(YAW),
                '405', '505'], check=True)
r160 = rarc(open(f'{STAGE}/R_SP160/R00_00.arc', 'rb').read())
write_rarc(f'{OUT}/{NAME}/R00_00.arc', 'R00_00', {
    'bmdr/model.bmd': open(f'{SHOP}/field_r8_cut.bmd', 'rb').read(),   # the field round the tent, none of it inside
    'bmdr/model5.bmd': open(f'{SHOP}/shop_world.bmd', 'rb').read(),     # the shop, in the world, lit as the ground is
    'dzr/room.dzr': bytes(dzr),
    'kcl/room.kcl': open(f'{SHOP}/room.kcl', 'rb').read(),
    'plc/room.plc': r160[[k for k in r160 if k.endswith('room.plc')][0]],
})
for f in ('STG_00', 'R00_00'):
    back = rarc(open(f'{OUT}/{NAME}/{f}.arc', 'rb').read())
    print(f, os.path.getsize(f'{OUT}/{NAME}/{f}.arc') // 1024, 'KB', sorted(k.split('/', 1)[1] for k in back))
