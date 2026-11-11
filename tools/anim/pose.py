"""Pose skinned BMDs with animation locals and render unlit contact sheets."""
import sys, math
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, '/home/claude/anim')
sys.path.insert(0, '/home/claude/ptb/Passer_Travellers_Build/tools')
from bmd import BMD
from render import render
from retarget import world_mats


def posed_tris(b, locals_):
    """re-skin b with the given joint locals; returns list of (mat, P, uv) + joint world mats"""
    b.jloc = locals_
    b.jworld = world_mats(b.parent, locals_)
    b.read_shp()
    return list(b.tris), b.jworld


class Scene:
    def __init__(self):
        self.tris, self.mats, self.tex = [], [], []

    def add(self, b, tris, M=None):
        mo, to = len(self.mats), len(self.tex)
        self.tex += list(b.tex)
        self.mats += [((t + to) if t is not None else None, c) for t, c in b.mats]
        for mi, P, uv in tris:
            if M is not None:
                P = P @ M[:3, :3].T + M[:3, 3]
            self.tris.append((mi + mo, P, uv))


def rigid_tris(b):
    return list(b.tris)


def sheet(images, labels, cols, path, bg=(46, 46, 52)):
    w, h = images[0].size
    rows = (len(images) + cols - 1) // cols
    out = Image.new('RGB', (w * cols, (h + 22) * rows), bg)
    d = ImageDraw.Draw(out)
    for i, (im, lab) in enumerate(zip(images, labels)):
        x, y = (i % cols) * w, (i // cols) * (h + 22)
        out.paste(im, (x, y + 22))
        d.text((x + 8, y + 5), lab, fill=(220, 220, 220))
    out.save(path)
    return out
