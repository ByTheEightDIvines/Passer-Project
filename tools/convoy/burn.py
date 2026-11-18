"""Burnt-out wagons: char the textures of a finished model in place (same CMPR size), and rebuild the
royal cover as burnt rags on bare hoops. Used by make_burnt.py."""
import sys, struct, numpy as np
sys.path[:0] = ['/home/claude/w/anim']
from bmd import BMD, decode_tex
from bmdwrite import encode_cmpr, encode_cmpr_rgba

def noise(h, w, seed, octaves=5, base=4):
    rng = np.random.default_rng(seed); out = np.zeros((h, w)); amp = 1.0; tot = 0
    for o in range(octaves):
        n = base * 2 ** o
        g = rng.random((n + 1, n + 1))
        ys = np.linspace(0, n, h, endpoint=False); xs = np.linspace(0, n, w, endpoint=False)
        y0 = ys.astype(int); x0 = xs.astype(int); fy = (ys - y0)[:, None]; fx = (xs - x0)[None]
        fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
        a = g[y0][:, x0]; b = g[y0][:, x0 + 1]; c = g[y0 + 1][:, x0]; d = g[y0 + 1][:, x0 + 1]
        out += amp * ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy); tot += amp; amp *= 0.5
    return out / tot

CHAR = np.array([20, 15, 12], float); ASH = np.array([120, 116, 110], float)
def cells(h, w, seed, n):
    """alligator cracking: 0 on the crack lines, rising to 1 in the middle of each charred block"""
    rng = np.random.default_rng(seed); pts = rng.random((n, 2)) * [h, w]
    yy, xx = np.mgrid[0:h, 0:w]; d = np.full((2, h, w), 1e9)
    for py, px in pts:
        for oy in (-h, 0, h):
            for ox in (-w, 0, w):
                dd = np.hypot(yy - py - oy, (xx - px - ox) * 0.55)     # cracks run with the grain: longer than wide
                d1 = np.minimum(d[0], dd); d[1] = np.minimum(d[1], np.maximum(d[0], dd)); d[0] = d1
    return np.clip((d[1] - d[0]) / 1.8, 0, 1) ** 0.6

def char(img, seed, strength=1.0):
    """img HxWx3/4 uint8 -> charred copy: scorched brown, charred black with grey alligator cracking, ash.
    The grain and carving stay readable; bright metal and gold are only sooted."""
    im = np.asarray(img, float).copy(); rgb = im[..., :3]; h, w = rgb.shape[:2]
    lum = rgb @ [0.3, 0.59, 0.11]; mx = rgb.max(-1); mn = rgb.min(-1)
    sat = (mx - mn) / np.maximum(mx, 1)
    metal = np.clip((lum - 110) / 80, 0, 1) * np.clip(1 - sat * 1.5, 0, 1)
    gold = np.clip((lum - 90) / 70, 0, 1) * (rgb[..., 0] > rgb[..., 2] * 1.4)
    hard = np.clip(metal + gold, 0, 1)[..., None]
    rel = (lum - lum.mean()) / max(lum.std(), 1)                               # the grain and carving, normalised
    n = noise(h, w, seed); n2 = noise(h, w, seed + 7, base=8)
    red = np.clip((rgb[..., 0] - rgb[..., 1] * 1.6 - 20) / 60, 0, 1)            # red cloth (curtains, banners) burns worst
    zone = np.clip((n - 0.35) * 4.0 + red * 0.8, 0, 1)[..., None] * strength   # 0 scorched .. 1 charred through
    scorched = rgb * 0.42 * np.array([1.0, 0.82, 0.66]) + CHAR * 0.25         # browned, darkened, still itself
    c = cells(h, w, seed + 3, max(16, h * w // 500))[..., None]
    charred = CHAR + (42 + 24 * rel[..., None]) * (0.25 + 0.75 * c) * (0.75 + 0.5 * n2[..., None])            # blocks with a grey sheen, black cracks
    out = scorched * (1 - zone) + charred * zone
    soot = rgb * (0.45 + 0.2 * n[..., None]) + CHAR * 0.25
    out = out * (1 - hard) + soot * hard
    ash = (np.clip((n2 - 0.7) * 5, 0, 1) * zone[..., 0] * (1 - hard[..., 0]))[..., None]
    out = out * (1 - ash * 0.7) + ASH * ash * 0.7
    im[..., :3] = np.clip(out, 0, 255)
    return im.astype(np.uint8)

def tex_headers(data):
    b = BMD(data); o = b.sec['TEX1']; n = struct.unpack_from('>H', data, o + 8)[0]
    ho = o + struct.unpack_from('>I', data, o + 0xC)[0]
    out = []
    for i in range(n):
        h = ho + i * 0x20
        fmt = data[h]; w, hh = struct.unpack_from('>HH', data, h + 2); mips = data[h + 0x18]
        doff = struct.unpack_from('>I', data, h + 0x1C)[0]
        out.append(dict(h=h, fmt=fmt, w=w, hh=hh, mips=mips, data=h + doff))
    return out

def patch(data, fn):
    """fn(i, rgba HxWx4 uint8) -> new rgba (or None to leave it). Rewrites CMPR texels in place, mips too."""
    data = bytearray(data); b = BMD(bytes(data))
    for i, t in enumerate(tex_headers(bytes(data))):
        if t['fmt'] != 14: continue
        new = fn(i, np.asarray(b.tex[i], np.uint8))
        if new is None: continue
        alpha = new.shape[-1] == 4 and (new[..., 3] < 128).any()
        off = t['data']; w, h = t['w'], t['hh']; img = new
        from PIL import Image
        for m in range(max(1, t['mips'])):
            enc = encode_cmpr_rgba(img) if alpha else encode_cmpr(img[..., :3])
            data[off:off + len(enc)] = enc; off += len(enc)
            w, h = max(8, w // 2), max(8, h // 2)
            img = np.asarray(Image.fromarray(img).resize((w, h), Image.LANCZOS))
    return bytes(data)
