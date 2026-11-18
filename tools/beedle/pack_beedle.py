"""Beedle's field archive (P_Beedl.arc): his model and his Wind Waker talking (upper body, standing legs).
Usage: python3 -I pack_beedle.py beedle.bmd ANIMS_DIR OUT.arc"""
import sys, os, glob
sys.path[:0] = ['/home/claude/w/anim', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'drw')]
from rarcwrite import write_rarc
import drwfix, drwverify, inf1fix   # every skinned model needs the canonical DRW1 layout, or the game corrupts its joints
bmd, anims, out = sys.argv[1:4]
files = {'bmdr/beedle.bmd': open(bmd, 'rb').read()}
for f in sorted(glob.glob(os.path.join(anims, 'psr_bs_*_up.bck'))):
    files['bck/' + os.path.basename(f)] = open(f, 'rb').read()
# Beedle's models (field and tent): DRW1 into the layout J3D assumes, checked (see tools/drw/README_DRW1.md)
for k in [k for k in files if k.endswith('.bmd')]:
    fixed, info = drwfix.fix(inf1fix.fix(files[k]))   # hierarchy order first (materials before child joints)
    chk = drwverify.check(files[k], fixed)
    assert chk['same_targets'] and chk['overflow'] == 0 and chk['wrong_slots'] == 0 and chk['canonical'], (k, chk)
    assert not inf1fix.check(fixed), (k, inf1fix.check(fixed))
    files[k] = fixed
    print(k, 'DRW1', info)
write_rarc(out, 'P_Beedl', files)
print(out, os.path.getsize(out), sorted(files))
