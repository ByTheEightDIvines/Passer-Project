"""Beedle's shop stage, R_PSR00, built from our own tables (Dusklight's stage loader, d_stage.cpp, says
what each one is for): only what the shop needs. Room 8 of it holds the field's room 8 model, the shop
placed where Beedle's tent stands, the shop's collision, the spawn and the three lantern flames; the
stage has the field's sky and time-of-day tables so the view out of the door and windows is the field's.
Usage: python3 -I build_shop_own.py ROM_STAGE_DIR SHOP_DIR OUT_DIR"""
import sys, os, struct, math, subprocess, hashlib
import numpy as np
sys.path[:0] = ['/home/claude/anim', '/home/claude/room']
from rarc import rarc
from rarcwrite import write_rarc
STAGE, SHOP, OUT = sys.argv[1:4]
NAME, ROOM = 'R_PSR00', 8
TENT = (-72921.4, -1551.7, 5115.4)     # Beedle's tent spot (travel network node 16, F_SP122 room 8)
YAW = 9935
a_ = YAW / 65536 * 2 * math.pi; c_, s_ = math.cos(a_), math.sin(a_)
def place(x, y, z): return (TENT[0] + x * c_ + z * s_, TENT[1] + y, TENT[2] - x * s_ + z * c_)

def read_chunks(d):
    n = struct.unpack_from('>I', d, 0)[0]
    return {d[4 + 12 * i:8 + 12 * i].decode('latin1'): struct.unpack_from('>II', d, 8 + 12 * i) for i in range(n)}
def write_dzx(chunks):
    """chunks: list of (tag, count, data-builder(base offset) -> bytes). Offsets in the data are from the
    file's start, as the game's loader expects (dStage_dt_c_offsetToPtr)."""
    head = 4 + 12 * len(chunks)
    out = bytearray(head); struct.pack_into('>I', out, 0, len(chunks))
    for i, (tag, count, build) in enumerate(chunks):
        while len(out) % 4: out.append(0)
        off = len(out)
        out += build(off)
        struct.pack_into('>4sII', out, 4 + 12 * i, tag.encode('latin1'), count, off)
    while len(out) % 32: out.append(0)
    return bytes(out)
raw = lambda b: (lambda off: bytes(b))

# ---- the field's tables we keep (F_SP122), straight from its stage file ----
fstg = rarc(open(f'{STAGE}/F_SP122/STG_00.arc', 'rb').read())
fdzs = [v for k, v in fstg.items() if k.endswith('stage.dzs')][0]
FC = read_chunks(fdzs)
_offs = sorted(o for _, o in FC.values()) + [len(fdzs)]
def field_block(tag, size=None):
    # the table exactly as it sits in the field's file: from its start to where the next table begins
    cnt, off = FC[tag]; nxt = min(x for x in _offs if x > off)
    return cnt, fdzs[off:nxt]
STAG_F = bytearray(fdzs[FC['STAG'][1]:FC['STAG'][1] + 0x3C])
# STAG: the field's own, except: room type (no rain or weather indoors), interior map setting 3 / minimap 3
# / particle set 12 (field_0x0a as the interiors have it: a field-map stage looks its rooms up in the shared
# field-map archive), no stage title
v = struct.unpack_from('>I', STAG_F, 0xC)[0]
struct.pack_into('>I', STAG_F, 0xC, (v & ~(7 << 16)) | (2 << 16))
struct.pack_into('>H', STAG_F, 0xA, 0x6063)
struct.pack_into('>H', STAG_F, 0x2A, 0)
env_n, env = field_block('Env0', 0x41)
col_n, col = field_block('Col0', 0xC)
pal_n, pal = field_block('PAL0', 0x34)
vrb_n, vrb = field_block('VRB0', 0x18)
rcam_n, rcam = field_block('RCAM'); raro_n, raro = field_block('RARO')
# RCAM / RARO: the stage's base camera entry (both the field and the interiors have exactly one)

def rtbl(off):
    # entries 0..8: offsets to roomRead_data (num, flags, pad, offset to room list); only room 8 lists a room
    n = ROOM + 1
    tbl = off; data = off + 4 * n
    b = bytearray(4 * n)
    for i in range(n):
        struct.pack_into('>I', b, 4 * i, data + 8 * i)
    for i in range(n):
        b += struct.pack('>BBBxI', 1 if i == ROOM else 0, 0, 0, data + 8 * n)
    b += bytes([0xC0 | ROOM, 0, 0, 0])            # room 8 itself (0xC0: the flags the field gives it)
    return bytes(b)
scls = struct.pack('>8sBbBBB', b'F_SP122', 0, ROOM, 0xF0, 0xFF, 0)
stage_dzs = write_dzx([
    ('STAG', 1, raw(STAG_F)),
    ('RTBL', ROOM + 1, rtbl),
    ('RCAM', rcam_n, raw(rcam)), ('RARO', raro_n, raw(raro)),
    ('SCLS', 1, raw(scls)),
    ('Env0', env_n, raw(env)), ('Col0', col_n, raw(col)), ('PAL0', pal_n, raw(pal)), ('VRB0', vrb_n, raw(vrb)),
])
# stage archive: our stage.dzs, the field's sky models and textures, the shared textures its room model
# uses, and the field's polygon tables (no bank.bin / name.bin: those make rooms load object archives)
files = {'dzs/stage.dzs': stage_dzs}
for k, val in fstg.items():
    rel = k.split('/', 1)[1] if k.count('/') > 1 else k
    base = rel.split('/')[-1]
    if base in ('stage.dzs', 'bank.bin', 'name.bin'): continue
    files[rel] = val
os.makedirs(f'{OUT}/{NAME}', exist_ok=True)
write_rarc(f'{OUT}/{NAME}/STG_00.arc', 'STG_00', files)

# ---- room 8 ----
hdzr = [v for k, v in rarc(open(f'{STAGE}/R_SP109/R00_00.arc', 'rb').read()).items() if k.endswith('room.dzr')][0]
HC = read_chunks(hdzr)
fili = hdzr[HC['FILI'][1]:HC['FILI'][1] + 0x20]      # room flags: interior minimap, no default camera
face = (YAW + 0x8000) & 0xFFFF; face = face - 0x10000 if face > 0x7FFF else face
plyr = struct.pack('>8sI3f3hH', b'Link', 0xFF00003F, *place(0.0, 0.0, 250.0), 0, face, 0, 0xFFFF)
acts = b''.join(struct.pack('>8sI3f3hH', b'fireWd2', 0xFF010120, *place(0.0, 340.0, lz), 0, 0, 0, 0xFFFF)
                for lz in (-330.0, 0.0, 330.0))
room_dzr = write_dzx([('FILI', 1, raw(fili)), ('PLYR', 1, raw(plyr)), ('ACTR', 3, raw(acts))])
# the shop model in the world (where the tent stands), and its collision there
import scene
from multibmd import write_multi
S, TX = scene.build(False, clear_floor=False)
groups = {}; order = []
for ti, P, UV in S:
    Q = np.array([place(*p) for p in np.asarray(P, float)])
    tex = (np.clip(TX[ti], 0, 1) * 255).astype(np.uint8)
    if tex.shape[2] == 3: tex = np.dstack([tex, np.full(tex.shape[:2], 255, np.uint8)])
    k = hashlib.md5(tex.tobytes()).hexdigest()
    if k not in groups: groups[k] = (tex, bool(tex[..., 3].min() < 128), []); order.append(k)
    groups[k][2].append((Q, np.asarray(UV, float)))
write_multi(f'{SHOP}/shop_world.bmd', [groups[k] for k in order], jointname='shop', cull=0)
subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shop_collision.py'),
                SHOP, str(TENT[0]), str(TENT[1]), str(TENT[2]), str(YAW)], check=True)
r8 = rarc(open(f'{STAGE}/F_SP122/R08_00.arc', 'rb').read())
get = lambda end: [v for k, v in r8.items() if k.endswith(end)][0]
write_rarc(f'{OUT}/{NAME}/R{ROOM:02d}_00.arc', f'R{ROOM:02d}_00', {
    'bmdr/model.bmd': get('model.bmd'),                 # the field round the tent
    'bmdr/model1.bmd': open(f'{SHOP}/shop_world.bmd', 'rb').read(),
    'dzr/room.dzr': room_dzr,
    'kcl/room.kcl': open(f'{SHOP}/room.kcl', 'rb').read(),
    'plc/room.plc': get('room.plc'),
})
print('built', sorted(os.listdir(f'{OUT}/{NAME}')))
