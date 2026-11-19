"""Builds the recoloured traveller bodies (recolor.py's approved variants) as archives for the overlay.

For each body type and variant: a copy of the body archive (P_<model>.arc) named PV<tt><v>.arc
(tt: the passer type, v: 1.. the variant) whose model has the recoloured body texture. Only the
4x4 texture blocks whose colours changed are re-encoded; every other block keeps the original's
exact bytes, so skin, faces and anything left alone are bit-for-bit the original. Both mip levels
are updated (the smaller one gets the same change, scaled down).

usage: pack_variants.py <base .dusk or folder with overlay/res/Object> <out folder> [types...]
"""
import sys, os, io, zipfile, struct, numpy as np
sys.path[:0] = ['/home/claude/w/anim', os.path.dirname(os.path.abspath(__file__))]
from rarc import rarc
from rarcwrite import write_rarc
from bmd import decode_tex
from bmdwrite import cmpr_block_alpha
import regions as R, recolor as C


def variant_arc(t, v):
    return f'PV{t:02d}{v}'


def read_base(src, arc):
    p = f'overlay/res/Object/{arc}.arc'
    if src.endswith('.dusk'):
        return zipfile.ZipFile(src).read(p)
    return open(os.path.join(src, p), 'rb').read()


def blocks(img):
    """CMPR order: 8x8 tiles of four 4x4 blocks; yields (block index, y, x)"""
    h, w = img.shape[:2]; i = 0
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for sb in range(4):
                yield i, by + (sb // 2) * 4, bx + (sb % 2) * 4
                i += 1


def reencode(data, orig, new):
    """data: the level's original CMPR bytes; replace only blocks whose colours differ"""
    out = bytearray(data); n = 0
    for i, y, x in blocks(orig):
        a, b = orig[y:y + 4, x:x + 4], new[y:y + 4, x:x + 4]
        if np.array_equal(a[..., :3], b[..., :3]): continue
        out[i * 8:(i + 1) * 8] = cmpr_block_alpha(b[..., :3], a[..., 3])
        n += 1
    return bytes(out), n


def down(img):
    f = img[..., :3].astype(float)
    return (f[0::2, 0::2] + f[1::2, 0::2] + f[0::2, 1::2] + f[1::2, 1::2]) / 4


def build(src, out_dir, types):
    os.makedirs(out_dir, exist_ok=True)
    made = []
    for t in types:
        arc = C.BODY[t][0]
        files = rarc(read_base(src, arc))
        bmd_key = [k for k in files if k.endswith('.bmd')][0]
        bmd_bytes = files[bmd_key]
        m, names = R.load(bmd_bytes)
        ti, img = R.body_texture(m)
        ids, _ = R.islands(m, names, ti, img.shape)
        hm, sm = C.body_masks(t, m, ti, img, ids)
        keep = C.body_keep(t, m, ti, ids)
        # the body texture's header(s): every TEX1 entry showing this image (they share one data block)
        d = m.d; o = m.sec['TEX1']
        n = struct.unpack('>H', d[o + 8:o + 10])[0]; ho = o + struct.unpack('>I', d[o + 0xC:o + 0x10])[0]
        starts = set()
        for i in R.same_textures(m, ti):
            h = ho + i * 0x20
            assert d[h] == 14 and d[h + 0x18] == 2, (arc, i, d[h], d[h + 0x18])
            starts.add(h + struct.unpack('>I', d[h + 0x1C:h + 0x20])[0])
        assert len(starts) == 1, (arc, starts)
        start = starts.pop()
        H, W = img.shape[:2]
        size0, size1 = W * H // 2, (W // 2) * (H // 2) // 2
        lvl0 = bytes(d[start:start + size0]); lvl1 = bytes(d[start + size0:start + size0 + size1])
        mip1 = decode_tex(lvl1, 14, W // 2, H // 2)
        for v, spec in enumerate(C.VARIANTS[t], 1):
            new0 = C.apply(img, hm, sm, *spec, keep=keep)
            n0data, c0 = reencode(lvl0, img, new0)
            new1 = mip1.copy()
            new1[..., :3] = np.clip(mip1[..., :3].astype(float) + down(new0) - down(img), 0, 255).round().astype(np.uint8)
            n1data, c1 = reencode(lvl1, mip1, new1)
            nb = bytearray(d)
            nb[start:start + size0] = n0data
            nb[start + size0:start + size0 + size1] = n1data
            name = variant_arc(t, v)
            out = dict(files); out[bmd_key] = bytes(nb)
            path = os.path.join(out_dir, name + '.arc')
            write_rarc(path, name.lower(), out)
            made.append((name, arc, spec, c0, c1))
            print(f'{name}.arc  from {arc}  {spec[0]} hair, {spec[1]}  blocks re-encoded: {c0} + {c1} (mip)')
    return made


if __name__ == '__main__':
    src, out_dir = sys.argv[1], sys.argv[2]
    types = [int(a) for a in sys.argv[3:]] or list(C.BODY)
    build(src, out_dir, types)
