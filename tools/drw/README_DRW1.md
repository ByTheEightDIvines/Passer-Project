# Skinning Draw-Matrix (DRW1) Issue

Notes for the Hero of Legend mods (Dusklight / Twilight Princess J3D models). Found while building the Pants versions of the Hero's Clothes, Zora Armor and Magic Armor.

---

## Summary

Exported Link models can have their **draw-matrix list (the DRW1 section of the .bmd)** in an order the game doesn't expect. The model still loads, but the game's joint setup writes past the end of an internal table and corrupts the joint pointers. The result is broken skinning in-game: pieces stretch, fold or follow the wrong bone, and it can look different between loads.

**Fix:** rewrite DRW1 into the layout the game assumes, then remap every mesh packet's matrix indices so each one still points at the same joint or weight blend. Nothing else in the model changes: geometry, UVs, weights, materials and textures stay as they were.

**Every skinned model exported for a mod needs this check before it ships.**

---

## Background: what DRW1 is

A J3D model (.bmd/.bdl) skins vertices through a list of **draw matrices** (DRW1). Each entry is one of:

- **Rigid:** the vertex follows a single joint 100%. The entry stores a joint index.
- **Envelope (weighted):** the vertex is blended between several joints. The entry stores an index into the EVP1 envelope list.

Every mesh packet (in SHP1) has a small table of DRW1 indices it uses, and each vertex picks one of those.

## The layout the game assumes

The game's model code (`J3DJointTree` / `J3DModelData`) doesn't read the DRW1 entries one by one in a general way. It assumes this fixed layout:

```
[ rigid entries ............ ][ envelope 0 .. N-1 ][ envelope 0 .. N-1 ]
  one per fully-weighted joint  in order, once        the same list again
```

- `mEntryNum` (the number of real matrices) = **DRW1 count − EVP1 count**.
- The rigid entries come **first**.
- Draw-matrix slot **(number of rigid entries + k)** is treated as **envelope k**. Envelopes must appear in order 0, 1, 2, … with no gaps.
- The envelope list is repeated a second time at the end. That tail is what makes the count math above work.

## What goes wrong

Exporters (Blender plugins, converters, AI-generated pipelines) usually write DRW1 in **whatever order they first meet each joint or blend**. Rigid and envelope entries end up mixed together, envelopes out of order, and the count doesn't follow the rule above.

At load, `J3DJointTree::findImportantMtxIndex()` fills a table (`mWEvlpImportantMtxIdx`) sized from `mEntryNum`, assuming the layout above. With a non-canonical list:

1. The computed number of rigid entries + envelopes is **larger than the table**, so the writes **overflow** past its end.
2. The overflow lands on the **joint pointer table** next to it in memory and corrupts it.
3. Separately, packets pointing at a slot the game reads as "envelope k" get a different envelope (or a rigid joint) than the exporter intended.

## Symptoms

- Armor pieces, the tunic or the pants stretched, folded, or snapping to the wrong bone.
- Parts that look fine in Blender but wrong in-game.
- Behaviour that changes between loads or areas (memory corruption, not a stable mapping).
- A model that was fine until new pieces or new weights were added (more envelopes, so the overflow starts).

## How to check a model

`drwscan.py` opens every `.bmd`/`.bdl` inside each `.dusk` and reports, per skinned model:

- `drw` / `evp`: DRW1 and EVP1 counts.
- `overflow`: how many entries the joint setup would write past its table (must be **0**).
- `wrong_slots`: packet references that the game would resolve to a different joint or envelope than intended (must be **0**).
- `canonical`: whether DRW1 already follows the layout above (should be **True**).

`drwuse.py <model.bmd>` prints the same numbers for a single loose model, plus the run layout (rigid/envelope blocks) and which slots the meshes actually use.

## How to fix a model

```
python drwfix.py broken.bmd fixed.bmd
python drwverify.py broken.bmd fixed.bmd
```

`drwfix.py`:

1. Reads DRW1 and EVP1.
2. Collects the unique rigid joints (in first-seen order) and builds the new list: `[rigid joints][envelopes 0..N-1][envelopes 0..N-1]`.
3. Builds a lookup from every old slot to its new slot (rigid joint → its rigid position; envelope k → rigid count + k).
4. Rewrites every SHP1 packet's matrix table and its default matrix through that lookup. Shared tables are only rewritten once.
5. Rebuilds the DRW1 section, aligned to 32 bytes, and updates the section and file sizes.

`drwverify.py` then checks that **every packet still points at the same joint or envelope as before**, that the game's table **fits** (no overflow), and that no packet uses a slot at or above `mEntryNum`.

After fixing: repack the model into the `.dusk`, keep backface culling off on every material, and test in-game.

## Checklist for every new or re-exported model

- [ ] Export the model.
- [ ] Run `drwscan.py` (or `drwuse.py` on the loose .bmd). `overflow` = 0, `wrong_slots` = 0, `canonical` = True?
- [ ] If not, run `drwfix.py`, then `drwverify.py` (all checks pass).
- [ ] Backface culling off on every material.
- [ ] Repack into the `.dusk` and test in-game. Move, swing and roll, and check shoulders, hips and knees.

This applies to anything with weighted vertices: Link's body and armor models, heads and hair, the Pants versions, and any future outfit or transplant. It will also apply to the retextured Magic Armor once it's exported from Blender again.

---

## Appendix: drwfix.py

```python
"""Rewrite a BMD's DRW1 into the canonical J3D layout [rigid][env 0..N-1][env 0..N-1] and remap
every shape packet's matrix-index table so each packet keeps referencing the same joint/envelope.

J3D assumes: mEntryNum = DRW count - EVP count, rigid entries come first, and draw-matrix slot
(fullWgt + k) is envelope k. Non-canonical exports overflow mWEvlpImportantMtxIdx in
J3DJointTree::findImportantMtxIndex() and corrupt the joint pointer table."""
import struct, sys


def secs(d):
    o = 32; r = []
    while o < len(d):
        sz = struct.unpack('>I', d[o + 4:o + 8])[0]; r.append([d[o:o + 4], bytearray(d[o:o + sz])]); o += sz
    return r


def fix(d):
    S = secs(d); T = {t: s for t, s in S}
    w = T[b'DRW1']; n = struct.unpack('>H', w[8:10])[0]; fo, io = struct.unpack('>II', w[12:20])
    flags = list(w[fo:fo + n]); idx = list(struct.unpack(f'>{n}H', w[io:io + 2 * n]))
    nevp = struct.unpack('>H', T[b'EVP1'][8:10])[0]
    rigid_joints = []
    for k in range(n):
        if flags[k] == 0 and idx[k] not in rigid_joints:
            rigid_joints.append(idx[k])
    R = len(rigid_joints)
    newflags = [0] * R + [1] * nevp + [1] * nevp
    newidx = rigid_joints + list(range(nevp)) + list(range(nevp))
    assert len(newidx) <= 0xFFFF, 'too many draw matrices for a u16 count'

    def remap(old):
        if flags[old] == 0:
            return rigid_joints.index(idx[old])
        return R + idx[old]

    lut = [remap(k) for k in range(n)]
    # SHP1 packet matrix tables
    s = T[b'SHP1']; ns = struct.unpack('>H', s[8:10])[0]; so = struct.unpack('>8I', s[12:44])
    shp, _, _, _, mtxidx, _, mtxdata, _ = so
    seen = set(); seen_u = set(); changed = 0
    for i in range(ns):
        en = shp + i * 0x28; npk, ao, fmi = struct.unpack('>HHH', s[en + 2:en + 8])
        for k in range(npk):
            m = mtxdata + (fmi + k) * 8; u, mc, ms = struct.unpack('>HHI', s[m:m + 8])
            if u != 0xFFFF and u < n and m not in seen_u:
                seen_u.add(m); struct.pack_into('>H', s, m, lut[u])   # packet's default matrix (DRW index)
            if (ms, mc) in seen: continue          # tables can be shared between packets
            seen.add((ms, mc))
            for j in range(mc):
                p = mtxidx + (ms + j) * 2
                x = struct.unpack('>H', s[p:p + 2])[0]
                if x != 0xFFFF:
                    struct.pack_into('>H', s, p, lut[x]); changed += 1
    # rebuild DRW1
    N = len(newidx)
    body = bytearray(b'DRW1' + b'\0' * 4 + struct.pack('>H', N) + b'\xff\xff' + b'\0' * 8)
    f_off = len(body); body += bytes(newflags); body += b'\0' * ((-len(body)) % 2)
    i_off = len(body); body += struct.pack(f'>{N}H', *newidx)
    body += b'\0' * ((-len(body)) % 32)
    struct.pack_into('>I', body, 4, len(body)); struct.pack_into('>II', body, 12, f_off, i_off)
    for e in S:
        if e[0] == b'DRW1': e[1] = body
    out = bytearray(d[:32]) + b''.join(x for _, x in S)
    struct.pack_into('>I', out, 8, len(out))
    return bytes(out), dict(old=n, new=N, rigid=R, evp=nevp, remapped=changed)


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    out, info = fix(d)
    open(sys.argv[2], 'wb').write(out)
    print(info)
```
