import sys,os; sys.path.insert(0,'/home/claude/gw'); os.chdir('/home/claude/gw')
from spearback import *
import spearback; import re
src=open('/home/claude/pp/src/guard_gear.inc').read()
def mtx(name):
    m=re.search(name+r' = \{(.*?)\};',src).group(1); v=[float(x) for x in re.findall(r'-?[\d.]+',m)]; return M3(np.array(v).reshape(3,4))
from PIL import Image, ImageDraw
walk=MG['bck/m_walk_a.bck']; run=MG['bck/m_run_a.bck']
def carry(t,style,bck,fr):
    b=body(t); n=pose(b,bck,fr); sc=scene(b); m='mad_a' if t==1 else 'man_a'
    shm='tall' if style=='spear' else 'small'
    attach(sc,load_item(PACK['bmdr/psr_g_shield_%s.bmd'%('t' if style=='spear' else 's')]),b.jworld[n.index('backbone')]@mtx('kHold_back_%s_%s'%(shm,m)))
    if style=='spear': attach(sc,load_item(SP),b.jworld[n.index('backbone')]@mtx('kHold_back_spear_'+m))
    attach(sc,load_item(PACK['bmdr/psr_g_scabbard.bmd']),b.jworld[n.index('waist')]@mtx('kHold_scab'))
    return sc
