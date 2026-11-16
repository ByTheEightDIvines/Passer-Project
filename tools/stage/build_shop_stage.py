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
