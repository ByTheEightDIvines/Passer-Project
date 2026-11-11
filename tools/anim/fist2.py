"""Fit the grip line through each passer's cupped hand.
Project the hand along a candidate axis, take the cavity = convex hull minus silhouette, and
score by the largest circle that fits in the cavity. Search axis tilts; the best axis is the one
the curl wraps around."""
import sys; sys.argv = sys.argv[:1]; sys.path.insert(0, '/home/claude/anim')
import cv2
from hipdraw import *

PX = 12.0  # pixels per unit


def hand_tris_local(b, rig):
    j = rig.idx['handR']; inv = np.linalg.inv(rig.rest_world[j])
    ok = {j, rig.idx.get('fingerR', -9)}
    return [(P @ inv[:3, :3].T) + inv[:3, 3] for (mi, P, uv), js in zip(b.tris, b.trij) if all(x in ok for x in js)]


def cavity(tris, axis, u, v):
    Q = [np.stack([T @ u, T @ v], 1) for T in tris]
    allq = np.concatenate(Q); lo = allq.min(0) - 6; hi = allq.max(0) + 6
    W, H = (np.ceil((hi - lo) * PX)).astype(int)
    mask = np.zeros((H, W), np.uint8)
    for q in Q:
        p = ((q - lo) * PX).astype(np.int32)
        cv2.fillPoly(mask, [p], 255)
    pts = cv2.findNonZero(mask)
    hull = cv2.convexHull(pts)
    hm = np.zeros_like(mask); cv2.fillPoly(hm, [hull], 255)
    cav = ((hm > 0) & (mask == 0)).astype(np.uint8)
    # distance from cavity pixels to the hand (inscribed radius)
    dist = cv2.distanceTransform((mask == 0).astype(np.uint8), cv2.DIST_L2, 5)
    dist[cav == 0] = 0
    hy, hx = np.nonzero(mask)
    best = (0.0, None)
    cand = np.argwhere(dist > 1.5 * PX)
    if len(cand) > 400:
        cand = cand[np.random.default_rng(0).choice(len(cand), 400, replace=False)]
    for y, x in cand:
        r = dist[y, x]
        dd = np.hypot(hx - x, hy - y); near = dd < r + 4 * PX
        if near.sum() < 5: continue
        ang = np.arctan2(hy[near] - y, hx[near] - x)
        cov = len(np.unique(((ang + np.pi) / (2 * np.pi) * 36).astype(int) % 36)) / 36
        if cov < 0.6: continue
        sc = r / PX * min(cov, 0.85)
        if sc > best[0]: best = (sc, (x, y, r / PX, cov))
    if best[1] is None:
        return 0.0, np.zeros(2), mask, cav
    x, y, r, cov = best[1]
    return best[0], np.array([x, y]) / PX + lo, mask, cav


def fit(tris, axis0):
    axis0 = axis0 / np.linalg.norm(axis0)
    best = None
    for ax in np.radians(np.arange(-28, 29, 4)):
        for ay in np.radians(np.arange(-28, 29, 4)):
            R = Rot.from_euler('xy', [ax, ay])
            a = R.apply(axis0)
            u = np.cross(a, [0, 0, 1.0]) if abs(a[2]) < .9 else np.cross(a, [1.0, 0, 0])
            u /= np.linalg.norm(u); v = np.cross(a, u)
            r, c2, _, _ = cavity(tris, a, u, v)
            r -= 0.02 * math.degrees(math.hypot(ax, ay))
            if best is None or r > best[0]:
                best = (r, a, u, v, c2)
    r, a, u, v, c2 = best
    # position along the axis: middle of the hand's extent there
    P = np.concatenate(tris)
    t = P @ a
    c = c2[0] * u + c2[1] * v
    near = np.linalg.norm((P - np.outer(t, a)) - c, axis=1) < r + 6
    tm = (t[near].min() + t[near].max()) / 2 if near.any() else t.mean()
    return c + a * tm, a, r


def new_grip(m_grip, centre, axis):
    """keep the weapon's edge orientation as close to the old grip as possible"""
    if axis @ m_grip[:3, 2] < 0: axis = -axis
    x = m_grip[:3, 0] - axis * (m_grip[:3, 0] @ axis); x /= np.linalg.norm(x)
    G = np.eye(4); G[:3, 0], G[:3, 1], G[:3, 2] = x, np.cross(axis, x), axis
    G[:3, 3] = centre
    return G


if __name__ == '__main__':
    tr = hand_tris_local(BMD(arcf(BASE + 'Moi.arc', 'moi.bmd')), MOI)
    c, a, r = fit(tr, G_RUSL[:3, 2])
    print('Rusl    centre', c.round(1), 'axis', a.round(2), 'r', round(r, 1), ' (his real grip', G_RUSL[:3, 3].round(1), G_RUSL[:3, 2].round(2), ')')
    for n in ['MON_a', 'MAS_a', 'MAN_a2', 'MAD_a2', 'MAN_c2']:
        m = Model(n); tr = hand_tris_local(m.b, m.rig)
        c, a, r = fit(tr, m.grip[:3, 2])
        print(f'{n:7s} centre {c.round(1)} axis {a.round(2)} r {r:.1f}   (old {m.grip[:3,3].round(1)} {m.grip[:3,2].round(2)})')
