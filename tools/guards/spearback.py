import sys,re; sys.path.insert(0,'.')
from rig import *
src=open('/home/claude/pp/src/guard_gear.inc').read()
def mtx(name):
    m=re.search(name+r' = \{(.*?)\};',src).group(1); v=[float(x) for x in re.findall(r'-?[\d.]+',m)]; return M3(np.array(v).reshape(3,4))
SP=OBJ['bmdr/b_spear.bmd']
def spear_back(t,ang,cy=118.0):
    """offset in backbone space: spear diagonal behind the shield, head up over the right shoulder"""
    b=body(t); n=pose(b)
    m='mad_a' if t==1 else 'man_a'
    sh=load_item(PACK['bmdr/psr_g_shield_t.bmd']); W=b.jworld[n.index('backbone')]@mtx('kHold_back_tall_'+m)
    Q=np.array([tt[1] for tt in sh.tris]).reshape(-1,3); Q=(W[:3,:3]@Q.T).T+W[:3,3]
    zb=Q[:,2].min()-5.0
    a=np.radians(ang)
    tip=np.array([-np.sin(a),np.cos(a),0.0])      # world direction of the spearhead
    Y=-tip; Zl=np.array([np.cos(a),np.sin(a),0.0]); X=np.cross(Y,Zl)
    R=np.column_stack([X,Y,Zl])
    centre=np.array([0.0,cy,zb])
    T=centre-R@np.array([10.0,-36.0,4.0])
    Wd=np.eye(4); Wd[:3,:3]=R; Wd[:3,3]=T
    return np.linalg.inv(b.jworld[n.index('backbone')])@Wd
def show(t,off,bck=None,frame=0,shield='t'):
    b=body(t); n=pose(b,bck,frame); sc=scene(b); m='mad_a' if t==1 else 'man_a'
    shm='tall' if shield=='t' else 'small'
    attach(sc,load_item(PACK['bmdr/psr_g_shield_%s.bmd'%shield]),b.jworld[n.index('backbone')]@mtx('kHold_back_%s_%s'%(shm,m)))
    if off is not None: attach(sc,load_item(SP),b.jworld[n.index('backbone')]@off)
    attach(sc,load_item(PACK['bmdr/psr_g_scabbard.bmd']),b.jworld[n.index('waist')]@mtx('kHold_scab'))
    return sc
