import numpy as np
def fixed_normals(pos, nrm, tris, maxdev=30.0, minalt=3.0):
    """Per triangle: its three normals. Flat canvas whose normals stray > maxdev from its face gets the
    face normal, on the side the old normals were on. Thin pieces (ropes, poles: altitude < minalt) keep theirs."""
    out = []; changed = []
    for i, t in enumerate(tris):
        P = np.array([pos[v[9]] for v in t], float)
        old = np.array([nrm[v[10]] for v in t], float)
        fn = np.cross(P[1] - P[0], P[2] - P[0]); L = np.linalg.norm(fn)
        if L < 1e-6: out.append(old); continue
        fn /= L
        edges = [np.linalg.norm(P[(k+1) % 3] - P[k]) for k in range(3)]
        alt = L / max(edges)
        on = old / np.maximum(np.linalg.norm(old, axis=1, keepdims=True), 1e-9)
        dev = np.degrees(np.arccos(np.clip(np.abs(on @ fn), 0, 1))).max()
        if dev > maxdev and alt >= minalt:
            side = 1.0 if on.sum(0) @ fn >= 0 else -1.0
            out.append(np.array([fn * side] * 3)); changed.append(i)
        else:
            out.append(old)
    return out, changed

def fixed_normals_vote(pos, nrm, tris, maxdev=30.0, minalt=3.0, radius=160.0):
    """As fixed_normals, but each changed triangle takes the side its flat neighbours (same plane, close
    by, normals already true) face; the old smoothed normals are unreliable near ridges and corners."""
    out, changed = fixed_normals(pos, nrm, tris, maxdev, minalt)
    ch = set(changed)
    C = np.array([np.mean([pos[v[9]] for v in t], 0) for t in tris])
    F = []
    for t in tris:
        P = np.array([pos[v[9]] for v in t], float); f = np.cross(P[1]-P[0], P[2]-P[0]); F.append(f / max(np.linalg.norm(f), 1e-9))
    F = np.array(F)
    good = np.array([i not in ch for i in range(len(tris))])
    ON = np.array([o.mean(0) for o in out])
    for i in changed:
        d = np.linalg.norm(C - C[i], axis=1)
        nb = np.where(good & (d < radius) & (np.abs(F @ F[i]) > 0.985))[0]
        if len(nb) == 0: continue
        votes = np.sign(ON[nb] @ F[i]); w = 1.0 / (d[nb] + 1.0)
        side = 1.0 if (votes * w).sum() >= 0 else -1.0
        out[i] = np.array([F[i] * side] * 3)
    return out, changed
