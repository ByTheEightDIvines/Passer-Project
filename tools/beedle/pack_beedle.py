"""Beedle's field archive (P_Beedl.arc): his model and his Wind Waker talking (upper body, standing legs).
Usage: python3 -I pack_beedle.py beedle.bmd ANIMS_DIR OUT.arc"""
import sys, os, glob
sys.path[:0] = ['/home/claude/w/anim']
from rarcwrite import write_rarc
bmd, anims, out = sys.argv[1:4]
files = {'bmdr/beedle.bmd': open(bmd, 'rb').read()}
for f in sorted(glob.glob(os.path.join(anims, 'psr_bs_*_up.bck'))):
    files['bck/' + os.path.basename(f)] = open(f, 'rb').read()
write_rarc(out, 'P_Beedl', files)
print(out, os.path.getsize(out), sorted(files))
