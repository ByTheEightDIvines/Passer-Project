import sys, json, re
sys.path[:0]=['/home/claude/w/anim','/home/claude/passer-project/tools/convoy/royal_archer']
from common import *
import rarc
from bck import BCK, srt_matrix
U='/mnt/user-data/uploads/Desktop/New Modding/Rom Dump/files/res/Object/'
def arc(p):
    d=open(p,'rb').read(); return rarc.rarc(rarc.yaz0(d) if d[:4]==b'Yaz0' else d)
MG=arc(U+'Mgeneral.arc'); PK=arc('/home/claude/out/PsrPack.arc')
gg=open('/home/claude/passer-project/src/guard_gear.inc').read()
def MI(n,src=gg):
    v=[float(x) for x in re.findall(r'-?\d+\.\d+',re.search(n+r' = \{(.*?)\};',src,re.S).group(1))]; m=np.eye(4); m[:3,:]=np.array(v).reshape(3,4); return m
TW=json.load(open('/home/claude/handover/x/repo_snapshot/tools/anim/cd2_joint_tweaks.json'))
def tweak(name):
    out={}
    for jn,v in TW.get(name,{}).items():
        rx,ry_,rz_=[np.radians(a) for a in v['r_deg']]
        X=np.eye(3); X[1:,1:]=[[np.cos(rx),-np.sin(rx)],[np.sin(rx),np.cos(rx)]]
        Y=np.eye(3); Y[0,0]=Y[2,2]=np.cos(ry_); Y[0,2]=np.sin(ry_); Y[2,0]=-np.sin(ry_)
        Z=np.eye(3); Z[:2,:2]=[[np.cos(rz_),-np.sin(rz_)],[np.sin(rz_),np.cos(rz_)]]
        m=np.eye(4); m[:3,:3]=Y@X@Z; m[:3,3]=np.array(v['t'])*10.0; out[jn]=m
    return out
class Scene:
    def __init__(s): s.tris=[]; s.mats=[]; s.tex=[]
    def add(s,m):
        n0=len(s.tex); s.tex+=list(m.tex)
        for mi,pp,uv in m.tris:
            ti=m.mats[mi][0] if mi<len(m.mats) else 0
            s.mats.append((n0+(ti or 0),np.ones(4))); s.tris.append((len(s.mats)-1,np.asarray(pp,float),uv))
def guard(name, shield_back):
    sc=Scene(); A=arc(U+name+'.arc')
    b=BMD([v for k,v in A.items() if k.endswith('/%s.bmd'%name.lower())][0])
    t=BCK([v for k,v in MG.items() if k.endswith('m_wait_a.bck')][0]); bind=[m.copy() for m in b.jloc]; L=[]
    for j in range(len(b.jloc)):
        if j>=t.nj: L.append(b.jloc[j]); continue
        m=srt_matrix(*t.sample(j,0));
        if j>0: m[:3,3]=bind[j][:3,3]
        L.append(m)
    nt=joint_names(b); tw=tweak(name); b.jloc=[L[i]@tw.get(nt[i],np.eye(4)) for i in range(len(L))]
    b.jworld=[None]*len(b.jloc)
    for i in range(len(b.jloc)): b.jworld[i]=b.world(i)
    b.read_shp(); sc.add(b)
    BB=b.jworld[nt.index('backbone')]; HL=b.jworld[7]
    UP=np.eye(4); UP[1,3]=-8
    q=BMD(PK['bmdr/psr_quiver_royal.bmd']); q.jworld=[BB@UP@q.world(0)]; q.read_shp(); sc.add(q)
    bw=BMD(PK['bmdr/psr_bow_royal.bmd'])
    bw.jloc=[np.eye(4),M('kBowCenterBack'),Tr(-5,0,0)@rz(160)@RX,Tr(5,0,0)@rz(20),Tr(0,30,0)]
    base=BB@UP@M('kBowOnBack'); bw.jworld=[base@bw.world(i) for i in range(5)]; bw.read_shp(); sc.add(bw)
    sh=BMD(PK['bmdr/psr_g_shield_s.bmd'])
    big='mad' in name.lower()
    hold=MI('kHold_back_small_%s'%('mad_a' if big else 'man_a')) if shield_back else MI('kHold_small')
    J=BB if shield_back else HL
    sh.jworld=[J@hold@sh.world(i) for i in range(len(sh.jloc))]; sh.read_shp(); sc.add(sh)
    return sc
if __name__=='__main__':
    ims=[]
    for name in ('MAN_a','MAD_a'):
        for back in (False,True):
            sc=guard(name,back)
            for y in (180,135):
                ims.append(img(sc,y,260,460))
    strip(ims,'onguard.png')
