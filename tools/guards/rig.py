import sys,zipfile,numpy as np
sys.path[:0]=['/home/claude/w/anim','/home/claude/pp/tools/looks']
from rarc import rarc; from bmd import BMD, joint_names; from bck import BCK, srt_matrix
from render import render
Z=zipfile.ZipFile('/home/claude/pk/DT_0fa1a2c.dusk'); PACK=rarc(Z.read('overlay/res/Object/PsrPack.arc'))
OBJ=rarc(open('/mnt/user-data/uploads/Object/object.arc','rb').read())
MG=rarc(open('/mnt/user-data/uploads/Object/Mgeneral.arc','rb').read())
def body(t):
    nm='mad_a' if t==1 else 'man_a'
    return BMD(rarc(open(f'/mnt/user-data/uploads/Object/{nm[:4].upper()+nm[4:]}.arc','rb').read())[f'bmdr/{nm}.bmd'])
def pose(b,bck=None,frame=0,arms=None):
    """bck: bytes or None (bind). arms: optional bck bytes whose arm joints override."""
    names=joint_names(b); rest=[x.copy() for x in b.jloc]
    if bck is not None:
        k=BCK(bck); L=[]
        for j in range(k.nj):
            s,r,t=k.sample(j,frame); L.append(srt_matrix(s,r,rest[j][:3,3] if j else t))
        b.jloc=L
    if arms is not None:
        k=BCK(arms)
        for j,n in enumerate(names):
            if n in arms_set: 
                s,r,t=k.sample(j,0); b.jloc[j]=srt_matrix(s,r,rest[j][:3,3])
    b.jworld=[None]*len(b.jloc)
    for i in range(len(b.jloc)): b.jworld[i]=b.world(i)
    b.read_shp(); return names
arms_set={'shoulderL','armL1','armL2','handL','shoulderR','armR1','armR2','handR'}
def load_item(data):
    m=BMD(data); m.jworld=[m.world(i) for i in range(len(m.jloc))]; m.read_shp(); return m
def attach(sc,item,M):
    """append item's tris (model space) transformed by 4x4 M into scene sc"""
    off=len(sc.tex)
    sc.tex+=list(item.tex)
    for (mi,P,uv) in item.tris:
        P=np.asarray(P,float); Q=(M[:3,:3]@P.T).T+M[:3,3]
        tm,col=item.mats[mi]
        sc.mats.append((None if tm is None else tm+off,col)); sc.tris.append((len(sc.mats)-1,Q,np.asarray(uv)))
class S: pass
def scene(b):
    sc=S(); sc.tris=list(b.tris); sc.mats=list(b.mats); sc.tex=list(b.tex); sc.tcol=getattr(b,'tcol',None)
    return sc
def M3(m):  # game Mtx (3x4 rows) -> 4x4
    a=np.eye(4); a[:3,:]=np.asarray(m,float); return a
