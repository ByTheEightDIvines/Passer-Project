"""The travellers' wagon, burnt out (approved look, same treatment as the royal one): rigid bakes of the
game's coach (Coach.arc coach.bmd, its wood texture charred) and harness, plus the cover rebuilt by
burnt_cover.build_from (canvas ~19% left, the hoops bare). Writes trav_coach_burnt / trav_cover_burnt /
trav_harness_burnt .bmd to OUT (default /tmp/burn). The wagon draws them at its coach and harness frames."""
import sys, numpy as np
sys.path[:0] = ['/home/claude/w/anim', '/home/claude/passer-project/tools/convoy']
from bmd import BMD
from bmdwrite import write_rigid_bmd
from PIL import Image
import make_burnt as mb, burn, burnt_cover

OUT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/burn'
A = mb.arc('/mnt/user-data/uploads/Object/Coach.arc')
m = mb.posed(BMD(A['bmdr/coach.bmd'])); m.read_shp()
t0 = [(np.asarray(p, float), np.asarray(u, float)) for mi, p, u in m.tris if m.mats[mi][0] == 0]
body = [(np.asarray(p, float), np.asarray(u, float)) for mi, p, u in m.tris if m.mats[mi][0] == 2]
isc = lambda u: u[:, 0].max() < 0.56 and u[:, 1].max() < 0.52            # the canvas: top-left of texture 0
canvas = [(p, u) for p, u in t0 if isc(u)]; trim = [(p, u) for p, u in t0 if not isc(u)]
C = np.concatenate([u for p, u in canvas]); tex0 = np.asarray(m.tex[0], np.uint8); H, W = tex0.shape[:2]
crop = tex0[:int(np.ceil(C[:, 1].max() * H)), :int(np.ceil(C[:, 0].max() * W))]
cw, ch = crop.shape[1] / W, crop.shape[0] / H
canvas_n = [(p, u / [cw, ch]) for p, u in canvas]
crop = np.asarray(Image.fromarray(crop).resize((256, 512)))
harn = mb.posed(BMD(A['bmdr/harness.bmd'])); harn.read_shp(); wood = np.asarray(harn.tex[0], np.uint8)
burnt_cover.build_from(canvas_n, trim, crop, tex0, wood, OUT + '/trav_cover_burnt.bmd', seed=31, burn_more=0.08)
def dark(t, seed):   # this wagon's wood starts dark: charred harder so it reads burnt, not grey
    c = burn.char(t, seed, 1.3).astype(float); c[..., :3] *= 0.75; return np.clip(c, 0, 255).astype(np.uint8)
write_rigid_bmd(OUT + '/trav_coach_burnt.bmd', body, dark(np.asarray(m.tex[2], np.uint8), 41)[..., :3],
                texname='psr_cbrn', matname='psr_cbrn')
write_rigid_bmd(OUT + '/trav_harness_burnt.bmd', [(np.asarray(p, float), np.asarray(u, float)) for mi, p, u in harn.tris],
                dark(wood, 51)[..., :3], texname='psr_hbrn', matname='psr_hbrn')
