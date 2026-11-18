import sys;sys.path.insert(0,'/home/claude/rb')
from fit import *
import pickle
F=pickle.load(open('/home/claude/rb/fits3.pkl','rb'))
def scene(name, shield_back):
    sc=Scene(); A=arc(U+name+'.arc')
    b=BMD([v for k,v in A.items() if k.endswith('/%s.bmd'%name.lower())][0])
    t=BCK([v for k,v in MG.items() if k.endswith('m_wait_a.bck')][0]); bind=[m.copy() for m in b.jloc]; L=[]
    for j in range(len(b.jloc)):
        if j>=t.nj: L.append(b.jloc[j]); continue
        m=srt_matrix(*t.sample(j,0))
        if j>0: m[:3,3]=bind[j][:3,3]
        L.append(m)
    nt=joint_names(b); tw=tweak(name); b.jloc=[L[i]@tw.get(nt[i],np.eye(4)) for i in range(len(L))]
    b.jworld=[None]*len(b.jloc)
    for i in range(len(b.jloc)): b.jworld[i]=b.world(i)
    b.read_shp(); sc.add(b)
    BB=b.jworld[nt.index('backbone')]; HL=b.jworld[7]; nm=name.lower()
    G,push,SB,BF=F[nm]
    q=BMD(PK['bmdr/psr_quiver_royal.bmd']); q.jworld=[BB@G@q.world(0)]; q.read_shp(); sc.add(q)
    bw=BMD(PK['bmdr/psr_bow_royal.bmd'])
    bw.jloc=[np.eye(4),M('kBowCenterBack'),Tr(-5,0,0)@rz(160)@RX,Tr(5,0,0)@rz(20),Tr(0,30,0)]
    base=BB@Tr(0,push,0)@BF; bw.jworld=[base@bw.world(i) for i in range(5)]; bw.read_shp(); sc.add(bw)
    sh=BMD(PK['bmdr/psr_g_shield_s.bmd'])
    hold=SB if shield_back else MI('kHold_small'); J=BB if shield_back else HL
    sh.jworld=[J@hold@sh.world(i) for i in range(len(sh.jloc))]; sh.read_shp(); sc.add(sh)
    return sc
if __name__=='__main__':
    ims=[]
    for name in ('MAN_a','MAD_a'):
        for back in (False,True):
            sc=scene(name,back)
            for y in (180,100):
                ims.append(img(sc,y,250,440))
    strip(ims,'onguard3.png')
