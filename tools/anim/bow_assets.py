"""The bowman's (passer 8's) gear for PsrPack, from the Bulblin archer's (E_rd.arc) and the fitting worked
out in the renders:
  psr_bow.bmd     the Bulblin's bow (its joints: bow_loc, center = the grip, bow_1 / bow_2 = the limbs,
                  line = the string's middle), walnut reskin (its own CMPR texture swapped), no back-face culling
  psr_quiver.bmd  the Bulblin's quiver and its arrows, cut from its body (materials 1 and 3 above the waist),
                  tan reskin, in the man's backbone joint frame (drawn on that joint), no culling
  psr_m_bow_draw / _aim / _shoot.bck   rd_retarget.py's refit (copied in from its output folder)
  bow_fit.inc     the matrices the game code poses the bow with (row-major 3x4)
Usage: python3 -I bow_assets.py E_rd.arc P_MAN_a2.arc RESKIN.pkl ANIM_DIR OUT_DIR"""
import sys, os, struct, pickle, shutil, numpy as np
sys.path[:0] = ['/home/claude/work', '/home/claude/anim']
from rarc import rarc
from bmd import BMD, joint_names
from bck import BCK, srt_matrix
from bmdwrite import sections, encode_cmpr
from multibmd import write_multi

rd_arc, man_arc, reskin, anim_dir, out = sys.argv[1:6]
os.makedirs(out, exist_ok=True)
rd = rarc(open(rd_arc, 'rb').read())
get = lambda a, e: [v for k, v in a.items() if k.endswith(e)][0]
man = get(rarc(open(man_arc, 'rb').read()), '/man_a2.bmd')
RS = pickle.load(open(reskin, 'rb'))

# ---- the bow: the Bulblin's file, walnut texture, culling off ----
bowfile = bytearray(get(rd, '/rd_bow.bmd'))
o = 32; secs = {}
while o < len(bowfile):
    tag = bytes(bowfile[o:o + 4]); size = struct.unpack_from('>I', bowfile, o + 4)[0]; secs[tag] = o; o += size
t = secs[b'TEX1']; hdr = t + struct.unpack_from('>I', bowfile, t + 0xC)[0]
assert bowfile[hdr] == 14 and struct.unpack_from('>HH', bowfile, hdr + 2) == (32, 32)
data = hdr + struct.unpack_from('>I', bowfile, hdr + 0x1C)[0]
cmpr = encode_cmpr(np.asarray(RS['bow_tex'][0])[..., :3])
assert len(cmpr) == 512
bowfile[data:data + 512] = cmpr
m = secs[b'MAT3']; cull = m + struct.unpack_from('>I', bowfile, m + 0xC + 4 * 4)[0]
nxt = min(x for x in (m + struct.unpack_from('>I', bowfile, m + 0xC + 4 * i)[0] for i in range(30)
                      if struct.unpack_from('>I', bowfile, m + 0xC + 4 * i)[0]) if x > cull)
for p in range(cull, nxt, 4): struct.pack_into('>I', bowfile, p, 0)
open(f'{out}/psr_bow.bmd', 'wb').write(bowfile)

# ---- the quiver: cut from the body, into the man's backbone frame ----
def worlds(b):
    W = [None] * len(b.jloc)
    for i in range(len(W)): W[i] = b.jloc[i] if b.parent[i] < 0 else W[b.parent[i]] @ b.jloc[i]
    return W
src = BMD(get(rd, '/rd.bmd')); tgt = BMD(man); Ws = worlds(src); Wt = worlds(tgt)
ns, nt = joint_names(src), joint_names(tgt)
Wm = Ws[ns.index('mune2')]; Wb = Wt[nt.index('backbone')]
Rc = np.linalg.inv(Wb[:3, :3]) @ Wm[:3, :3]
SC = np.linalg.norm(Wt[nt.index('armL1')][:3, 3] - Wt[nt.index('armR1')][:3, 3]) / \
     np.linalg.norm(Ws[ns.index('armL1')][:3, 3] - Ws[ns.index('armR1')][:3, 3])
body = BMD(get(rd, '/rd.bmd')); body.jworld = [body.world(i) for i in range(len(body.jloc))]; body.read_shp()
groups = {3: [], 1: []}
for mi, p, uv in body.tris:
    if mi == 1 or (mi == 3 and np.asarray(p)[:, 1].mean() > 60):
        P = np.asarray(p, float)
        loc = (np.c_[P, np.ones(3)] @ np.linalg.inv(Wm).T)[:, :3]
        groups[mi].append(((loc @ Rc.T) * SC, np.asarray(uv, float)))
tex = lambda a: (np.clip(np.asarray(a, float), 0, 255)).astype(np.uint8)
write_multi(f'{out}/psr_quiver.bmd', [(tex(RS['quiver_tex'][0]), False, groups[3]), (tex(RS['quiver_tex'][1]), False, groups[1])],
            jointname='quiver', cull=0, maxtex=256)

# ---- the animations ----
for n in ('bow_draw', 'bow_aim', 'bow_shoot'):
    shutil.copy(f'{anim_dir}/psr_m_{n}.bck', f'{out}/psr_m_{n}.bck')

# ---- the fit, for the game code ----
ready = BCK(get(rd, '/rd_bow_ready.bck'))
GRIP = 14.9 * 15.1 / 31.5            # the Bulblin's grip offset along its hand, scaled to the man's hand
s, r, tr = ready.sample(1, 0)
C_hand = srt_matrix(s, r, np.array(tr) * GRIP / 14.9)    # grip joint: in his left hand
C_back = srt_matrix(s, r, np.array(tr))                  # grip joint: on his back (the fit below used it)
GL = np.eye(4); GL[:3, :3] = np.linalg.inv(Wt[nt.index('handL')][:3, :3]) @ Ws[ns.index('handL')][:3, :3]
# on his back: the bow's flat side against it, long axis on the diagonal (35 degrees off his spine toward
# his left shoulder), grip 22 up the spine and 24 behind it
bw = BMD(get(rd, '/rd_bow.bmd'))
bw.jloc = [np.eye(4), C_back] + [srt_matrix(*ready.sample(j, 0)) for j in (2, 3, 4)]
bw.jworld = [bw.world(i) for i in range(5)]; bw.read_shp()
PBW = np.array([np.asarray(t_[1]) for t_ in bw.tris]).reshape(-1, 3)
c0 = PBW.mean(0); _, _, V = np.linalg.svd(PBW - c0)
Rb = Wb[:3, :3]; FWD = Rb.T @ [0, 0, 1.0]; UP = Rb.T @ [0, 1.0, 0]; RIGHT = Rb.T @ [-1.0, 0, 0]
a = np.radians(-35.0); long_t = UP * np.cos(a) + RIGHT * np.sin(a); wide_t = np.cross(FWD, long_t)
R = np.c_[long_t, wide_t, FWD] @ np.c_[V[0], V[1], V[2]].T
M_back = np.eye(4); M_back[:3, :3] = R; M_back[:3, 3] = UP * 22 + FWD * -24 - R @ c0
def cmat(name, M, note):
    rows = ', '.join('{%s}' % ', '.join('%.6ff' % v for v in M[i]) for i in range(3))
    return f'// {note}\nconst Mtx {name} = {{{rows}}};\n'
inc = ('// Generated by tools/anim/bow_assets.py: the bowman\'s bow, fitted (see its renders).\n' +
       cmat('kBowGripL', GL, 'his left hand joint -> the bow\'s base (the Bulblin\'s grip angle, carried over)') +
       cmat('kBowCenterHand', C_hand, 'the grip joint in his hand (the Bulblin animation\'s, offset scaled to his hand)') +
       cmat('kBowCenterBack', C_back, 'the grip joint on his back') +
       cmat('kBowOnBack', M_back, 'his backbone joint -> the bow\'s base, on his back') +
       '// the limbs: bow_1 = (-5,0,0) Rz(a) Rx(180), bow_2 = (5,0,0) Rz(b); at rest a = 160, b = 20 degrees, fully\n'
       '// drawn 135 / 45 (the Bulblin\'s); the string\'s middle (line) at (0, 30, 0) in the grip frame at rest,\n'
       '// fully drawn 80 out\n')
open(f'{out}/bow_fit.inc', 'w').write(inc)
for f in sorted(os.listdir(out)): print(f, os.path.getsize(f'{out}/{f}'))
