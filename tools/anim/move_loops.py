"""Move the mace/axe loops from the left hip to the front of the right hip (MAS_a, MAS_a2, MAD_a2).

Mirror across the body's centre line, then turn forward along the belt to the target angle,
snapping the hang point onto the belt ring there. Belt models are patched in place (only the
hanger-strap vertex positions and bounds change; texture untouched). Writes:
  out/gear/PsrGear.arc, out/gear/gear_table.json
"""
import sys, json, struct, math, copy
import numpy as np
sys.path.insert(0, '/home/claude/ptb/Passer_Travellers_Build/tools')
from rarc import rarc
from rarcwrite import write_rarc
from bmd import BMD, joint_names

PP = '/mnt/user-data/outputs/pp/obj/'
GT = json.load(open('/home/claude/ptb/Passer_Travellers_Build/build/gear/gear_table.json'))
OUT = '/home/claude/anim/out/gear/'
TARGET = {'MAS_a': -50.0, 'MAS_a2': -50.0, 'MAD_a2': -50.0}   # degrees, atan2(x, z); right side is negative x
STRAP = {'MAS_a': [34, 35], 'MAS_a2': [34, 35]}               # hanger strap triangles (MAD's shaft goes through the belt)


def ang(p): return math.degrees(math.atan2(p[0], p[2]))


def Ry(deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    M = np.eye(4); M[0, 0], M[0, 2], M[2, 0], M[2, 2] = c, s, -s, c
    return M


def ring_at(ring_pts, theta):
    """outer radius and centre height of the belt ring at angle theta (deg)"""
    A = np.array([ang(p) for p in ring_pts])
    d = np.abs((A - theta + 180) % 360 - 180)
    sel = d < 14
    r = np.max([math.hypot(p[0], p[2]) for p in ring_pts[sel]])
    y = np.mean(ring_pts[sel][:, 1])
    return r, y


def body_pts(name):
    r = rarc(open(PP + 'P_%s.arc' % name, 'rb').read())
    b = BMD([v for k, v in r.items() if k.endswith('.bmd')][0])
    return b, np.array([t[1] for t in b.tris])


def ray_extent(T, origin, dirv):
    e1 = T[:, 1] - T[:, 0]; e2 = T[:, 2] - T[:, 0]; p = np.cross(dirv, e2); det = (e1 * p).sum(1)
    ok = np.abs(det) > 1e-9; inv = np.zeros_like(det); inv[ok] = 1 / det[ok]
    s = origin - T[:, 0]; u = (s * p).sum(1) * inv; q = np.cross(s, e1); v = (q @ dirv) * inv; t = (e2 * q).sum(1) * inv
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0)
    return t[hit].max() if hit.any() else 0.0


def radial_margin(TT, pts):
    worst = 1e9
    for p in pts:
        h = np.array([p[0], 0, p[2]]); r = np.linalg.norm(h)
        worst = min(worst, r - ray_extent(TT, np.array([0, p[1], 0.]), h / r))
    return worst


def patch_positions(data, old_new):
    """replace exact float triples in VTX1's position array; returns new bytes"""
    d = bytearray(data)
    o = 0x20
    sec = {}
    for i in range(struct.unpack('>I', d[0xC:0x10])[0]):
        tag = bytes(d[o:o + 4]); sz = struct.unpack('>I', d[o + 4:o + 8])[0]; sec[tag] = o; o += sz
    v = sec[b'VTX1']; offs = struct.unpack('>13I', d[v + 0xC:v + 0xC + 52])
    p0 = v + offs[0]; p1 = v + min(x for x in offs if x > offs[0])
    n = (p1 - p0) // 12
    P = np.array(struct.unpack('>%df' % (n * 3), d[p0:p0 + n * 12])).reshape(n, 3)
    hit = 0
    for old, new in old_new:
        k = np.where(np.abs(P - old).max(1) < 1e-3)[0]
        for i in k:
            P[i] = new; hit += 1
    d[p0:p0 + n * 12] = struct.pack('>%df' % (n * 3), *P.ravel())
    # bounds in SHP1 shape entry and JNT1 entry
    used = P
    mn, mx = used.min(0), used.max(0); rad = float(np.linalg.norm(used, axis=1).max())
    s = sec[b'SHP1']; so = struct.unpack('>I', d[s + 0xC:s + 0x10])[0]
    struct.pack_into('>7f', d, s + so + 0xC, rad, *mn, *mx)
    j = sec[b'JNT1']; eo = struct.unpack('>I', d[j + 0xC:j + 0x10])[0]
    struct.pack_into('>7f', d, j + eo + 0x24, rad, *mn, *mx)
    return bytes(d), hit


if __name__ == '__main__':
    import os
    os.makedirs(OUT, exist_ok=True)
    gear = rarc(open(PP + 'PsrGear.arc', 'rb').read())
    gt = copy.deepcopy(GT)
    F = np.diag([-1.0, 1, 1, 1])
    for name, theta_first in TARGET.items():
      for theta_t in [theta_first - 8 * k for k in range(10)]:
          body, TT = body_pts(name)
          jn = joint_names(body); Ww = body.jworld[jn.index('waist')]
          belt_file = 'bmdr/' + GT[name]['belt']['file']
          bb = BMD(gear[belt_file])
          tris_w = [(P @ Ww[:3, :3].T) + Ww[:3, 3] for _, P, _ in bb.tris]
          strap = STRAP.get(name, [])
          ring_pts = np.concatenate([T for i, T in enumerate(tris_w) if i not in strap and i < 32])
          S = np.array(GT[name]['sheath_mtx_in_waist'])
          Sw = Ww @ S                                       # weapon frame in bind world
          if strap:
              sp = np.concatenate([tris_w[i] for i in strap])
              hp_old = sp[np.argmax(sp[:, 1])]              # top of the strap, on the belt
          else:                                             # where the shaft crosses the belt
              z = Sw[:3, 2]; o = Sw[:3, 3]
              ry = np.mean(ring_pts[:, 1]); t = (ry - o[1]) / z[1]
              hp_old = o + z * t
          th0 = ang(hp_old)
          r0, y0 = ring_at(ring_pts, th0)
          out0 = math.hypot(hp_old[0], hp_old[2]) - r0
          # mirror, turn forward along the belt, snap onto the ring at the target angle
          mir = F @ np.r_[hp_old, 1]
          rot = theta_t - ang(mir[:3])
          r1, y1 = ring_at(ring_pts, theta_t)
          R = Ry(rot)
          hp_rot = (R @ mir)[:3]
          rad = np.array([math.sin(math.radians(theta_t)), 0, math.cos(math.radians(theta_t))])
          hp_new = rad * (r1 + out0) + np.array([0, hp_old[1] - y0 + y1, 0])
          M = np.eye(4); M[:3, 3] = hp_new - hp_rot
          M = M @ R @ F
          fixflip = np.diag([1.0, -1, 1, 1])                # keep handedness: flip the weapon's thin axis
          Sw_new = M @ Sw @ fixflip
          # weapon clearance: push straight out from the body until it clears like the original fit
          wpn = BMD(gear['bmdr/wpn_%s.bmd' % GT[name]['weapon']])
          Pw = np.concatenate([t[1] for t in wpn.tris])[::3]
          def margin(Sm):
              Q = Pw @ Sm[:3, :3].T + Sm[:3, 3]
              Q = Q[np.linalg.norm(Q - hp_new, axis=1) > 3.0]
              return radial_margin(TT, Q)
          m0 = margin(Sw @ np.eye(4))
          push = 0.0
          while margin(Sw_new) < min(0.6, m0) and push < 4:
              Sw_new[:3, 3] += rad * 0.5; push += 0.5
          gt[name]['sheath_mtx_in_waist'] = (np.linalg.inv(Ww) @ Sw_new).round(4).tolist()
          gt[name]['loop_side'] = 'right_front'
          if margin(Sw_new) < 0.5 and theta_t > theta_first - 72:
              continue
          print(f'{name}: hang point {th0:.0f} deg -> {theta_t:.0f} deg; clearance before {m0:.2f}, after {margin(Sw_new):.2f} (pushed out {push:.1f})')
          if strap:
              Wi = np.linalg.inv(Ww)
              pairs = []
              for i in strap:
                  for p in tris_w[i]:
                      q = (M @ np.r_[p, 1])[:3]
                      q = q + rad * push * (1.0 if p[1] < hp_old[1] - 1 else 0.0)   # bottom of the strap follows the weapon out
                      pairs.append(((Wi @ np.r_[p, 1])[:3], (Wi @ np.r_[q, 1])[:3]))
              new_bytes, hit = patch_positions(gear[belt_file], pairs)
              gear[belt_file] = new_bytes
              print(f'   belt {belt_file}: patched {hit} vertex positions')
          break
    files = {k.split('/', 1)[1] if '/' in k else k: v for k, v in gear.items()}
    write_rarc(OUT + 'PsrGear.arc', 'PsrGear', gear)
    json.dump(gt, open(OUT + 'gear_table.json', 'w'), indent=1)
    print('wrote', OUT)
