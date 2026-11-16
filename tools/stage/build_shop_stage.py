"""Beedle's shop as a stage of its own, R_PSR00: STG_00.arc (stage settings) and R00_00.arc (the room).
Starts from the game's own Kakariko interior settings (R_SP109) for their layout and indoor lighting,
cut down to one room; the room is ours (model, collision, one spawn point inside the curtain).
Usage: python3 -I build_shop_stage.py ROM_STAGE_DIR SHOP_DIR OUT_DIR"""
import sys, os, struct
sys.path[:0] = ['/home/claude/anim']
from rarc import rarc
from rarcwrite import write_rarc
STAGE, SHOP, OUT = sys.argv[1:4]
NAME = 'R_PSR00'
FIELD = b'F_SP121'
ZF = 500.0

def chunks(v):
    n = struct.unpack('>I', v[:4])[0]
    return {v[4 + 12 * i:8 + 12 * i].decode('latin1'): (8 + 12 * i, *struct.unpack('>II', v[8 + 12 * i:16 + 12 * i])) for i in range(n)}
def set_count(v, tag, n):
    C = chunks(v)
    if tag in C: struct.pack_into('>I', v, C[tag][0], n)

# ---- stage settings ----
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
# its own light: neutral and bright, no fog (the house's palettes are lamp-orange and hazy)
for tag, (_, n, o) in chunks(dzs).items():
    if not tag.startswith('PAL'): continue
    for j in range(n):
        e = o + 0x34 * j
        dzs[e:e + 3] = bytes((150, 146, 140))                        # actors
        for k in range(4): dzs[e + 3 + 3 * k:e + 6 + 3 * k] = bytes((235, 230, 222))   # the room
        for k in range(6): dzs[e + 0xF + 3 * k:e + 0x12 + 3 * k] = bytes((255, 236, 205))  # lamp light
        dzs[e + 0x21:e + 0x24] = bytes((0, 0, 0))
        struct.pack_into('>ff', dzs, e + 0x24, 50000.0, 100000.0)   # fog far beyond the walls
C = chunks(dzs)
_, n, o = C['SCLS']
struct.pack_into('>8sBbBBB', dzs, o, FIELD, 0, 0, 0xF0, 0xFF, 0)     # the way out: the field (the mod puts Link at the tent)
set_count(dzs, 'SCLS', 1)
os.makedirs(f'{OUT}/{NAME}', exist_ok=True)
write_rarc(f'{OUT}/{NAME}/STG_00.arc', 'STG_00', {k: bytes(v) for k, v in files.items()})

# ---- the room ----
room = rarc(open(f'{STAGE}/R_SP109/R00_00.arc', 'rb').read())
dzr = bytearray(room[[k for k in room if k.endswith('room.dzr')][0]])
C = chunks(dzr)
for t in list(C):
    if t[:3] in ('ACT', 'SCO', 'LGT', 'TRE') or t in ('ACTR', 'SCOB', 'Door', 'REVT', 'RCAM', 'RARO'): set_count(dzr, t, 0)
_, n, o = C['PLYR']
# spawn 0: just inside the curtain, facing into the shop (plain standing spawn, like the game's own)
struct.pack_into('>8sI3f3hH', dzr, o, b'Link', 0xFF00003F, 0.0, 0.0, ZF - 90.0, 0, -0x8000, 0, 0xFFFF)
set_count(dzr, 'PLYR', 1)
_, n, o = C['SCLS']
struct.pack_into('>8sBbBBB', dzr, o, FIELD, 0, 0, 0xF0, 0xFF, 0)
set_count(dzr, 'SCLS', 1)
# lamp flames: the game's own (fireWd2, as in the Kakariko houses), each with its point light. Switch 0x20
# puts them out: the mod sets it by day and clears it at night (switch type 1: lit while it's off).
FLAMES = [(0.0, 340.0, -330.0), (0.0, 340.0, 0.0), (0.0, 340.0, 330.0),   # the three hanging lanterns
          (150.0, 150.0, -341.0),                                       # the candlestick on the counter
          (-40.0, 70.0, -339.0)]                                        # the oil lamp on the counter
acts = b''.join(struct.pack('>8sI3f3hH', b'fireWd2', 0xFF010120, x, y, z, 0, 0, 0, 0xFFFF) for x, y, z in FLAMES)
while len(dzr) % 4: dzr.append(0)
C = chunks(dzr)
struct.pack_into('>II', dzr, C['ACTR'][0], len(FLAMES), len(dzr))
dzr += acts
r160 = rarc(open(f'{STAGE}/R_SP160/R00_00.arc', 'rb').read())
rfiles = {
    'bmdr/model.bmd': open(f'{SHOP}/shop_room.bmd', 'rb').read(),
    'dzr/room.dzr': bytes(dzr),
    'kcl/room.kcl': open(f'{SHOP}/room.kcl', 'rb').read(),
    'plc/room.plc': r160[[k for k in r160 if k.endswith('room.plc')][0]],
}
write_rarc(f'{OUT}/{NAME}/R00_00.arc', 'R00_00', rfiles)
for f in ('STG_00', 'R00_00'):
    back = rarc(open(f'{OUT}/{NAME}/{f}.arc', 'rb').read())
    print(f, os.path.getsize(f'{OUT}/{NAME}/{f}.arc') // 1024, 'KB', sorted(k.split('/', 1)[1] for k in back))
