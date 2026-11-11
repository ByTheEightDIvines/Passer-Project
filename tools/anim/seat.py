"""Fit grips: curl axis (fist2.fit) then seat the handle deep in the curl, allowing the
fingers to wrap it slightly (clearance >= CLEAR_R)."""
import sys, json; sys.argv = sys.argv[:1]; sys.path.insert(0, '/home/claude/anim')
import cv2
from fist2 import *
PXu, CLEAR_R = 12.0, 1.1


def seat(m, G, tr):
    a = G[:3, 2]; u = G[:3, 0]; v = G[:3, 1]; c = G[:3, 3]
    Q = [np.stack([(T - c) @ u, (T - c) @ v], 1) for T in tr]
    lo = np.array([-25., -25.]); Wd = int(50 * PXu)
    mask = np.zeros((Wd, Wd), np.uint8)
    for q in Q: cv2.fillPoly(mask, [((q - lo) * PXu).astype(np.int32)], 255)
    hull = cv2.convexHull(cv2.findNonZero(mask)); hm = np.zeros_like(mask); cv2.fillPoly(hm, [hull], 255)
    clear = cv2.distanceTransform((mask == 0).astype(np.uint8), cv2.DIST_L2, 5) / PXu
    depth = cv2.distanceTransform((hm > 0).astype(np.uint8), cv2.DIST_L2, 5) / PXu
    ok = (hm > 0) & (mask == 0) & (clear >= CLEAR_R)
    n_, lab = cv2.connectedComponents(ok.astype(np.uint8))
    cur = ((np.zeros(2) - lo) * PXu).astype(int)
    L = lab[cur[1], cur[0]]
    if L == 0:
        ys, xs = np.nonzero(ok); k = np.argmin((xs - cur[0]) ** 2 + (ys - cur[1]) ** 2); L = lab[ys[k], xs[k]]
    score = np.where(lab == L, depth, -1); y, x = np.unravel_index(np.argmax(score), score.shape)
    p2 = np.array([x, y]) / PXu + lo
    G = G.copy(); G[:3, 3] = c + u * p2[0] + v * p2[1]
    return G, clear[y, x], depth[y, x]


if __name__ == '__main__':
    import os
    os.environ['RUSL_GRIP'] = '1'
    out = {}
    for n in ['MON_a', 'MAS_a', 'MAN_a2', 'MAD_a2', 'MON_a2', 'MAN_c2', 'MAS_a2']:
        m = Model(n); tr = hand_tris_local(m.b, m.rig)
        c, a, r = fit(tr, m.grip[:3, 2])
        G = new_grip(m.grip, c, a)
        G, cl, dp = seat(m, G, tr)
        G[:3, 3] += G[:3, 2] * 2.0      # hand centre sits at weapon z = -2
        out[n] = G.tolist()
        print(f'{n:7s} clearance {cl:.2f} depth {dp:.2f}')
    json.dump(out, open('/home/claude/anim/out/hip/grips_fit.json', 'w'), indent=1)
