"""Small perspective rasterizer for unlit interior renders.
scene: list of (tex_index, pos(3,3), uv(3,2)); textures: list of HxWx4 float arrays (0..1).
Perspective-correct UVs, near-plane clipping, alpha-test at 0.5, supersampled."""
import numpy as np, math
from PIL import Image

def look_at(eye, target, up=(0, 1, 0)):
    eye, target, up = map(lambda a: np.asarray(a, float), (eye, target, up))
    f = target - eye; f /= np.linalg.norm(f)
    r = np.cross(f, up); r /= np.linalg.norm(r)
    u = np.cross(r, f)
    M = np.eye(4); M[0, :3], M[1, :3], M[2, :3] = r, u, -f
    M[:3, 3] = -M[:3, :3] @ eye
    return M

def clip_near(P, UV, near):
    """P: 3x3 camera-space (looking down -z). Returns list of triangles in front of near plane."""
    inside = [(-p[2]) >= near for p in P]
    if all(inside): return [(P, UV)]
    if not any(inside): return []
    poly = []
    n = 3
    for i in range(n):
        a, b = P[i], P[(i + 1) % n]; ua, ub = UV[i], UV[(i + 1) % n]
        ia, ib = inside[i], inside[(i + 1) % n]
        if ia: poly.append((a, ua))
        if ia != ib:
            t = (near - (-a[2])) / ((-b[2]) - (-a[2]))
            poly.append((a + (b - a) * t, ua + (ub - ua) * t))
    out = []
    for i in range(1, len(poly) - 1):
        out.append((np.array([poly[0][0], poly[i][0], poly[i + 1][0]]), np.array([poly[0][1], poly[i][1], poly[i + 1][1]])))
    return out

def render(scene, textures, eye, target, fov=60.0, W=960, H=600, ss=2, bg=(30, 30, 36), near=2.0):
    Wp, Hp = W * ss, H * ss
    V = look_at(eye, target)
    f = 1 / math.tan(math.radians(fov) / 2)
    img = np.zeros((Hp, Wp, 3)); img[:] = np.array(bg) / 255
    zb = np.full((Hp, Wp), np.inf)
    for ti, P, UV in scene:
        Pc = (np.c_[P, np.ones(3)] @ V.T)[:, :3]
        if (-Pc[:, 2]).max() < near: continue
        for Q, U in clip_near(Pc, np.asarray(UV, float), near):
            zc = -Q[:, 2]
            x = (Q[:, 0] / zc * f) * (Hp / 2) + Wp / 2
            y = -(Q[:, 1] / zc * f) * (Hp / 2) + Hp / 2
            x0, x1 = int(max(0, math.floor(x.min()))), int(min(Wp - 1, math.ceil(x.max())))
            y0, y1 = int(max(0, math.floor(y.min()))), int(min(Hp - 1, math.ceil(y.max())))
            if x0 > x1 or y0 > y1: continue
            den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
            if abs(den) < 1e-9: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
            w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den
            w2 = 1 - w0 - w1
            m = (w0 >= -1e-5) & (w1 >= -1e-5) & (w2 >= -1e-5)
            if not m.any(): continue
            iz = w0 / zc[0] + w1 / zc[1] + w2 / zc[2]
            depth = 1 / iz
            T = textures[ti]; th, tw = T.shape[:2]
            u = (w0 * U[0, 0] / zc[0] + w1 * U[1, 0] / zc[1] + w2 * U[2, 0] / zc[2]) * depth
            v = (w0 * U[0, 1] / zc[0] + w1 * U[1, 1] / zc[1] + w2 * U[2, 1] / zc[2]) * depth
            px = T[(np.floor(v * th).astype(int)) % th, (np.floor(u * tw).astype(int)) % tw]
            m &= px[..., 3] > 0.5
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            m &= depth < sub
            sub[m] = depth[m]
            img[y0:y1 + 1, x0:x1 + 1][m] = px[..., :3][m]
    im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    return im.resize((W, H), Image.LANCZOS) if ss > 1 else im

def render_window(scene, textures, eye, rect, W=512, H=512, ss=2, bg=(30, 30, 36)):
    """Render what a viewer at `eye` sees through an axis-aligned window in a plane of constant z,
    rect = (x0, x1, y0, y1, zw), viewer on the +z side looking toward -z. The image IS the window:
    an off-axis projection onto that rectangle, so the picture is correct when shown on a card
    placed exactly there. Anything between the viewer and the window is clipped away."""
    x0, x1, y0, y1, zw = rect
    ex, ey, ez = eye
    dc = ez - zw
    Wp, Hp = W * ss, H * ss
    img = np.zeros((Hp, Wp, 3)); img[:] = np.array(bg) / 255
    zb = np.full((Hp, Wp), np.inf)
    for ti, P, UV in scene:
        P = np.asarray(P, float)
        Pc = P - np.array([ex, ey, ez])                    # camera looks down -z, no rotation
        if (-Pc[:, 2]).max() < dc: continue
        for Q, U in clip_near(Pc, np.asarray(UV, float), dc):
            zc = -Q[:, 2]
            wx = ex + Q[:, 0] * dc / zc; wy = ey + Q[:, 1] * dc / zc
            x = (wx - x0) / (x1 - x0) * Wp; y = (y1 - wy) / (y1 - y0) * Hp
            bx0, bx1 = int(max(0, math.floor(x.min()))), int(min(Wp - 1, math.ceil(x.max())))
            by0, by1 = int(max(0, math.floor(y.min()))), int(min(Hp - 1, math.ceil(y.max())))
            if bx0 > bx1 or by0 > by1: continue
            den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
            if abs(den) < 1e-9: continue
            gx, gy = np.meshgrid(np.arange(bx0, bx1 + 1) + 0.5, np.arange(by0, by1 + 1) + 0.5)
            w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
            w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den
            w2 = 1 - w0 - w1
            m = (w0 >= -1e-5) & (w1 >= -1e-5) & (w2 >= -1e-5)
            if not m.any(): continue
            iz = w0 / zc[0] + w1 / zc[1] + w2 / zc[2]; depth = 1 / iz
            T = textures[ti]; th, tw = T.shape[:2]
            u = (w0 * U[0, 0] / zc[0] + w1 * U[1, 0] / zc[1] + w2 * U[2, 0] / zc[2]) * depth
            v = (w0 * U[0, 1] / zc[0] + w1 * U[1, 1] / zc[1] + w2 * U[2, 1] / zc[2]) * depth
            px = T[(np.floor(v * th).astype(int)) % th, (np.floor(u * tw).astype(int)) % tw]
            m &= px[..., 3] > 0.5
            sub = zb[by0:by1 + 1, bx0:bx1 + 1]; m &= depth < sub; sub[m] = depth[m]
            img[by0:by1 + 1, bx0:bx1 + 1][m] = px[..., :3][m]
    im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    return im.resize((W, H), Image.LANCZOS) if ss > 1 else im
