MARK={}; WK=[]
import pickle
exec(open('/home/claude/w/escort/base.py').read())
MS=arc('/mnt/user-data/uploads/Object/Mspecial.arc'); OBJA=arc('/mnt/user-data/uploads/Object/object.arc')
GB={n:arc('/mnt/user-data/uploads/Object/%s.arc'%n)['bmdr/%s.bmd'%n.lower()] for n in ('MAD_a','MAN_a')}
GO='/home/claude/w/escort/guard_out/'
W_={'spear':OBJA['bmdr/b_spear.bmd'],'tall':open(GO+'psr_g_shield_t2.bmd','rb').read(),'sword':open(GO+'psr_g_sword.bmd','rb').read(),
    'small':open(GO+'psr_g_shield_s.bmd','rb').read(),'scab':open(GO+'psr_g_scabbard.bmd','rb').read()}
HOLD=pickle.load(open('/home/claude/w/escort/holds.pkl','rb'))
ARMS=pickle.load(open('/home/claude/w/escort/arms.pkl','rb'))       # left-arm locals from poses A and D
royal=pickle.load(open('/home/claude/w/escort/royal_tex0.pkl','rb'))
def place(data,W):
    m=BMD(data); m.jworld=[W@m.world(i) for i in range(len(m.jloc))]; m.read_shp(); add(m)
SW=lambda n: n.replace('L','#').replace('R','L').replace('#','R') if n[-1] in 'LR' or n[-2] in 'LR' else n
def wl(b,L):
    W=[None]*len(L)
    for i in range(len(L)): W[i]=L[i] if b.parent[i]<0 else W[b.parent[i]]@L[i]
    return W
Mx=np.diag([-1.0,1,1])
def mirrored(bmd,bck,f):
    """the animation played left-right mirrored on the same body; plus the unmirrored world matrices"""
    b=BMD(bmd); nt=joint_names(b); t=BCK(bck); B=wl(b,b.jloc)
    Ls=[srt_matrix(*t.sample(j,f)) if j<t.nj else b.jloc[j] for j in range(len(b.jloc))]
    for j in range(1,len(Ls)): Ls[j][:3,3]=b.jloc[j][:3,3]
    Ws=wl(b,Ls); Wt=[None]*len(nt); L=[None]*len(nt)
    for i,n in enumerate(nt):
        m=nt.index(SW(n)); dR=Mx@(Ws[m][:3,:3]@np.linalg.inv(B[m][:3,:3]))@Mx
        W=np.eye(4); W[:3,:3]=dR@B[i][:3,:3]
        if b.parent[i]<0: d=Ws[m][:3,3]-B[m][:3,3]; d[0]=-d[0]; W[:3,3]=B[i][:3,3]+d
        else: W[:3,3]=(Wt[b.parent[i]]@b.jloc[i])[:3,3]
        Wt[i]=W; L[i]=W if b.parent[i]<0 else np.linalg.inv(Wt[b.parent[i]])@W
    return L,Ws
def spear_hold(bmd):
    L,Ws=mirrored(bmd,MS['bck/m_walk_stick.bck'],0); b=BMD(bmd); nt=joint_names(b); Wt=wl(b,L)
    M4=np.diag([-1.0,1,1,1]); Wsp=M4@Ws[nt.index('handL')]@M4
    return np.linalg.inv(Wt[nt.index('handR')])@Wsp
def body_at(name,L,base):
    b=BMD(GB[name]); W=wl(b,L); b.jworld=[base@w for w in W]; b.read_shp(); add(b); return b
exec(open('/home/claude/w/escort/spearhold.py').read())
_SH={}
def spearman(name,anim,f,at):
    gb=GB[name]; b0=BMD(gb); nt=joint_names(b0)
    L=walk_L(gb,anim,f)                                   # the game's own spear walk: spear in the steady right hand
    for j in ('shoulderL','armL1','armL2','handL'): L[nt.index(j)]=ARMS['A'][name][j]
    b=body_at(name,L,Tr(*at)); J=lambda n: b.jworld[nt.index(n)]
    if name not in _SH: _SH[name]=hold_forward2(gb,name)
    place(W_['spear'],J('handR')@_SH[name]); place(W_['tall'],J('handL')@HOLD['tall']); place(W_['scab'],J('waist')@HOLD['scab'])
def swordsman(name,f,at):
    b0=BMD(GB[name]); nt=joint_names(b0); t=BCK(MG['bck/m_walk_a.bck'])
    L=[srt_matrix(*t.sample(j,f)) if j<t.nj else b0.jloc[j] for j in range(len(b0.jloc))]
    for j in range(1,len(L)): L[j][:3,3]=b0.jloc[j][:3,3]
    for j in ('shoulderL','armL1','armL2','handL'): L[nt.index(j)]=ARMS['D'][name][j]
    b=body_at(name,L,Tr(*at)); J=lambda n: b.jworld[nt.index(n)]
    place(W_['small'],J('handL')@HOLD['small']); place(W_['scab'],J('waist')@HOLD['scab'])
horse=pose(BMD(A['bmdr/horse.bmd']),A['bck/hu_walk_slow.bck'],5); add(horse)

exec(open('/home/claude/w/escort/royal_parts2.py').read())      # wheel_tris, rim/spoke/hub textures, classify, load
exec(open('/home/claude/w/escort/royalsheet.py').read().split("if __name__")[0])
exec(open('/home/claude/w/escort/piecelist.py').read().split('if __name__')[0].split('c=load')[0])
REGS=REGS; 
SHEET,_n=make_sheet(royal)
harn=pose(BMD(A['bmdr/harness.bmd'])); bs=harn.jworld[joint_names(harn).index('bs_loc')]
coach=pose(BMD(A['bmdr/coach.bmd']),base=Tr(*bs[:3,3]))
def newtex(t):
    sc.tex.append(np.dstack([np.clip(t[...,:3],0,255),np.full(t.shape[:2],255)]).astype(np.uint8) if t.shape[2]==3 else t)
    sc.mats.append((len(sc.tex)-1,np.ones(4))); return len(sc.mats)-1
# wheels: the castle doors' own iron (riveted iron strap, plain iron, the iron stud), not recoloured
strap=img('Z_lv9_door11',(0.27,0.0,0.49,1.0))                  # vertical riveted iron strap
TEX['rim']=rs(np.rot90(strap),256,32)                          # laid round the tyre, repeated 4 times
TEX['spoke']=rs(strap,32,128)
TEX['hub']=rs(img('j_lv9_door12',(0.80,0.36,1.0,0.72)),64,64)  # the dark iron pyramid stud
# side boxes: 3 matching carved panels per side, along the box's own length
PANEL=rs(img('Z_Lv9Door',(0.02,0.015,0.84,0.235)),256,128)
# gables: a row of 5 carved panels on the upright part, plain wood above the shoulder, molding between
GX0,GX1,GY0,GY1=-100.0,100.0,163.0,277.0
exec(open('/home/claude/w/escort/gable.py').read())
G=make_gable(PART['plank'],PART['stile'])
M_SHEET=newtex(SHEET)
M_PANEL=newtex(PANEL); M_STILE=newtex(rs(PART['stile'],32,256)); M_GABLE=newtex(G)
SQ=img('Lv9Door00_512',(0.12,0.345,0.82,0.505)); BAND=img('Lv9Door00_512',(0.06,0.28,0.82,0.35))
# plain boards: the castle door's own smooth wood, laid flat as two long boards with one thin seam
_b=rs(np.rot90(PART['plank']),256,32); BOARDS=np.concatenate([_b,_b[:,::-1]],0); BOARDS[30:34]*=0.55; BOARDS[:2]*=0.7; BOARDS[-2:]*=0.7
M_SQ=newtex(BOARDS); M_BAND=newtex(BOARDS); M_PLANK=newtex(rs(np.tile(PART['plank'],(1,6,1)),64,128))
M_HWOOD=newtex(rs(np.tile(PART['plank'],(1,10,1)),128,128))
WM={k:newtex(TEX[k]) for k in ('rim','spoke','hub')}
c0=load('coach'); h0=load('harness'); cn=joint_names(c0); hn=joint_names(h0); wheels={}
off=bs[:3,3]
MARK['body0']=len(sc.tris)
for k,mi,p,uv,jn in classify(c0,'coach'):
    if k=='cover': continue                                     # the royal cover model goes on instead
    if k=='wheel': j=max(set(jn),key=jn.count); wheels.setdefault(('c',j),[]).append((p,uv)); continue
    p=np.asarray(p,float); uv=np.asarray(uv,float); r=REGION(uv) if k=='wood' else ''
    if r=='rail' and p[:,1].max()<=84:                           # the box side: 3 panels end to end
        sc.tris.append((M_PANEL,p+off,np.c_[(p[:,2]+249.0)/324.0*3,(82.0-p[:,1])/65.0])); continue
    if r=='rail':                                                # the posts above it: stile, upright
        n=np.cross(p[1]-p[0],p[2]-p[0]); t=np.cross([0,1,0],n); t=t/(np.linalg.norm(t)+1e-9)
        sc.tris.append((M_STILE,p+off,np.c_[(p@t)/16.0,-p[:,1]/145.0])); continue
    if r=='beam':                                                # gables: flat on the face, no stretching
        sc.tris.append((M_GABLE,p+off,np.c_[(p[:,0]-GX0)/(GX1-GX0),(GY1-p[:,1])/(GY1-GY0)])); continue
    if r in ('floor','panel'):
        n=np.cross(p[1]-p[0],p[2]-p[0]); n=n/(np.linalg.norm(n)+1e-9); a=np.abs(n)
        if r=='floor' and a[0]>0.75:                              # bed sides: 3 iron-strapped rails from the arch's door under the 3 carved ones
            sc.tris.append((M_BAND,p+off,np.c_[(p[:,2]+249.0)/162.0,(25.0-p[:,1])/46.0])); continue
        if r=='panel' and a[2]>0.7:                               # front board and back step: carved squares from the arch's own door
            sc.tris.append((M_SQ,p+off,np.c_[(p[:,0]+56.0)/112.0,(28.0-p[:,1])/35.0])); continue
        ax=[i for i in range(3) if i!=int(np.argmax(a))]           # their edges and tops: plain door wood
        sc.tris.append((M_PLANK,p+off,np.c_[p[:,ax[0]]/66.0,p[:,ax[1]]/100.0])); continue
    sc.tris.append((M_SHEET,p+off,uv))                           # everything else: Telma's own layout
MARK['body1']=len(sc.tris)
for k,mi,p,uv,jn in classify(h0,'harness'):
    if k=='wheel': j=max(set(jn),key=jn.count); wheels.setdefault(('h',j),[]).append((p,uv)); continue
    sc.tris.append((M_HWOOD,np.asarray(p,float),np.asarray(uv,float)))
MARK['harn1']=len(sc.tris)
for (src_,j),tris in wheels.items():
    WK.append(((src_,j),len(sc.tris)))
    b=c0 if src_=='c' else h0; names=cn if src_=='c' else hn; o=off if src_=='c' else 0
    if 'wheel' not in j:
        for p,uv in tris: sc.tris.append((WM['spoke'],np.asarray(p,float)+o,np.c_[p[:,0]/40,p[:,2]/40]))
        continue
    center=b.jworld[names.index(j)][:3,3]
    for t,p,uv in wheel_tris(tris,center): sc.tris.append((WM[t],np.asarray(p,float)+o,uv*[4,1] if t=='rim' else uv))
MARK['wheels1']=len(sc.tris)
place(open(GO+'psr_royal_cover.bmd','rb').read(),Tr(*bs[:3,3]))
MARK['cover1']=len(sc.tris)
sys.path[:0]=['/home/claude/w']
from cargo_royal import royal_cargo
MARK['cargo0']=len(sc.tris); royal_cargo(sc,bs[:3,3]); MARK['cargo1']=len(sc.tris)
if __name__=='__main__': pass
