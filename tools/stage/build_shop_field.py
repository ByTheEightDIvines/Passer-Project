"""Beedle's shop as a copy of the real field round his tent: stage R_PSR00 is F_SP122's own stage settings
(sky, time of day, lighting), and its only room is room 8 (the room his tent stands in): the field's
own room 8 model, the shop placed where the tent stands, collision for the shop alone (Link stays
inside), no field actors, the three lantern flames, one spawn inside the curtain and the way out.
Usage: python3 -I build_shop_field.py ROM_STAGE_DIR SHOP_DIR OUT_DIR"""
import sys, os, struct, math, subprocess
import numpy as np
sys.path[:0] = ['/home/claude/anim']
from rarc import rarc
from rarcwrite import write_rarc
from bmd import BMD
STAGE, SHOP, OUT = sys.argv[1:4]
NAME, ROOM = 'R_PSR00', 8
TENT = (-72921.4, -1551.7, 5115.4)     # Beedle's tent spot (travel network node 16, F_SP122 room 8)
YAW = 9935
FIELD = b'F_SP122'
def _house(arc_name, tag, size):
    a = rarc(open(f'{STAGE}/R_SP109/{arc_name}', 'rb').read())
    d = [v for k, v in a.items() if k.endswith('.dzs') and 'stage' in k or k.endswith('room.dzr')][0]
    n = struct.unpack_from('>I', d, 0)[0]
    for i in range(n):
        t, cnt, off = struct.unpack_from('>4sII', d, 4 + 12 * i)
        if t == tag: return d[off:off + size]
HOUSE_MAP = struct.unpack('>H', _house('STG_00.arc', b'STAG', 0x34)[0xA:0xC])[0]   # 0x6063
HOUSE_FILI = _house('R00_00.arc', b'FILI', 0x20)                                      # minimap 3

def chunks(v):
    n = struct.unpack('>I', v[:4])[0]
    return {v[4 + 12 * i:8 + 12 * i].decode('latin1'): (8 + 12 * i, *struct.unpack('>II', v[8 + 12 * i:16 + 12 * i])) for i in range(n)}
def set_count(v, tag, n):
    C = chunks(v)
    if tag in C: struct.pack_into('>I', v, C[tag][0], n)
a = YAW / 65536 * 2 * math.pi; c, sn = math.cos(a), math.sin(a)
def place(x, y, z): return (TENT[0] + x * c + z * sn, TENT[1] + y, TENT[2] - x * sn + z * c)

# ---- stage: F_SP122's own settings, its stage actors removed, the exit back to the field ----
stg = rarc(open(f'{STAGE}/F_SP122/STG_00.arc', 'rb').read())
files = {(k.split('/', 1)[1] if k.count('/') > 1 else k): bytearray(v) for k, v in stg.items()}
dzs = files['dzs/stage.dzs']
for t in list(chunks(dzs)):
    if t[:3] in ('ACT', 'SCO', 'SON') or t == 'SOND': set_count(dzs, t, 0)
# not a field-map stage: a field stage (map setting 0) loads its rooms' extra data from the game's shared
# field-map archive by stage name, which has nothing for R_PSR00 (the crash going in). 3 as an interior.
_, _, o = chunks(dzs)['STAG']
struct.pack_into('>H', dzs, o + 0xA, HOUSE_MAP)
set_count(dzs, 'MULT', 0)
os.makedirs(f'{OUT}/{NAME}', exist_ok=True)
write_rarc(f'{OUT}/{NAME}/STG_00.arc', 'STG_00', {k: bytes(v) for k, v in files.items()})

# ---- room 8: the field's model and room data, the shop added ----
r8 = rarc(open(f'{STAGE}/F_SP122/R08_00.arc', 'rb').read())
get = lambda end: [v for k, v in r8.items() if k.endswith(end)][0]
dzr = bytearray(get('room.dzr'))
for t in list(chunks(dzr)):
    if t[:3] in ('ACT', 'SCO', 'SON', 'LGT', 'TRE') or t in ('REVT',): set_count(dzr, t, 0)
C = chunks(dzr)
# room flags as the Kakariko house's (its minimap type: the field's expects the field map)
dzr[C['FILI'][2]:C['FILI'][2] + 0x20] = HOUSE_FILI
# spawn 0: inside the curtain (250 in from the doorway), facing into the shop
_, n, o = C['PLYR']
x, y, z = place(0.0, 0.0, 250.0)
face = (YAW + 0x8000) & 0xFFFF
face = face - 0x10000 if face > 0x7FFF else face
struct.pack_into('>8sI3f3hH', dzr, o, b'Link', 0xFF00003F, x, y, z, 0, face, 0, 0xFFFF)
set_count(dzr, 'PLYR', 1)
# the way out (the mod sends Link to the field itself; this only names it)
_, n, o = C['SCLS']
struct.pack_into('>8sBbBBB', dzr, o, FIELD, 0, ROOM, 0xF0, 0xFF, 0)
set_count(dzr, 'SCLS', 1)
# lantern flames: the game's own (fireWd2, with their point lights) in the three hanging lanterns;
# switch 0x20 puts them out (the mod sets it by day, clears it at night)
acts = b''.join(struct.pack('>8sI3f3hH', b'fireWd2', 0xFF010120, *place(0.0, 340.0, lz), 0, 0, 0, 0xFFFF)
                for lz in (-330.0, 0.0, 330.0))
while len(dzr) % 4: dzr.append(0)
struct.pack_into('>II', dzr, chunks(dzr)['ACTR'][0], 3, len(dzr))
dzr += acts
# the shop model, turned and moved to where the tent stands (a second model of the room)
sh = BMD(open(f'{SHOP}/shop_room.bmd', 'rb').read())
from multibmd import write_multi
import hashlib, pickle
sys.path[:0] = ['/home/claude/room']
import scene
S, TX = scene.build(False, clear_floor=False)
groups = {}; order = []
for ti, P, UV in S:
    P = np.asarray(P, float); Q = np.array([place(*p) for p in P])
    tex = (np.clip(TX[ti], 0, 1) * 255).astype(np.uint8)
    if tex.shape[2] == 3: tex = np.dstack([tex, np.full(tex.shape[:2], 255, np.uint8)])
    k = hashlib.md5(tex.tobytes()).hexdigest()
    if k not in groups: groups[k] = (tex, bool(tex[..., 3].min() < 128), []); order.append(k)
    groups[k][2].append((Q, np.asarray(UV, float)))
write_multi(f'{SHOP}/shop_world.bmd', [groups[k] for k in order], jointname='shop', cull=0)
# collision: the shop alone, in the world
subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shop_collision.py'),
                SHOP, str(TENT[0]), str(TENT[1]), str(TENT[2]), str(YAW)], check=True)
rfiles = {
    'bmdr/model.bmd': get('model.bmd'),
    'bmdr/model1.bmd': open(f'{SHOP}/shop_world.bmd', 'rb').read(),
    'dzr/room.dzr': bytes(dzr),
    'kcl/room.kcl': open(f'{SHOP}/room.kcl', 'rb').read(),
    'plc/room.plc': get('room.plc'),
}
write_rarc(f'{OUT}/{NAME}/R{ROOM:02d}_00.arc', f'R{ROOM:02d}_00', rfiles)
for f in ('STG_00', f'R{ROOM:02d}_00'):
    back = rarc(open(f'{OUT}/{NAME}/{f}.arc', 'rb').read())
    print(f, os.path.getsize(f'{OUT}/{NAME}/{f}.arc') // 1024, 'KB', sorted(k.split('/', 1)[1] for k in back))
print('spawn at', place(0.0, 0.0, 250.0))
