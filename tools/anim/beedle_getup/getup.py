exec(open('/home/claude/gu/load.py').read())
from scipy.spatial.transform import Rotation as Rot, Slerp
land=BCK(get(PK,'psr_m_land.bck')); iw=BCK(get(PK,'psr_injury_wait.bck')); wait=BCK(get(BE,'psr_bs_wait01.bck'))
NJ=len(bind)
def key(anim,t):
    L=pose_locals(anim,t); return [ (Rot.from_matrix(m[:3,:3]), m[:3,3].copy()) for m in L]
K={'lie':key(land,24),'squat':key(wait,0),'stand':key(iw,0)}
K['squat'][0]=(K['squat'][0][0], K['squat'][0][1]+np.array([0,-14.0,0]))   # feet on the ground (as standing: soles 9 up)
def ease(u): return u*u*(3-2*u)
def blend(a,b,u):
    out=[]
    for (ra,ta),(rb,tb) in zip(K[a],K[b]):
        s=Slerp([0,1],Rot.concatenate([ra,rb]))(u)
        out.append((s, ta+(tb-ta)*u))
    return out
TIMELINE=[(0,'lie'),(22,'squat'),(28,'squat'),(46,'stand'),(52,'stand')]
def at(f):
    for (f0,a),(f1,b) in zip(TIMELINE,TIMELINE[1:]):
        if f0<=f<=f1: return blend(a,b,ease((f-f0)/max(1,f1-f0)))
    return blend(TIMELINE[-1][1],TIMELINE[-1][1],0)
def locals_at(f):
    P=at(f); L=[]
    for j,(r,t) in enumerate(P):
        m=np.eye(4); m[:3,:3]=r.as_matrix(); m[:3,3]=t if j==0 else bind[j][:3,3]; L.append(m)
    return L
if __name__=='__main__':
    bounds=(np.array([-170,-30,-170.]),np.array([170,230,170.]))
    ims=[]
    for f in range(0,53,4):
        b=model_at(locals_at(f)); im=img(b,90,W=220,H=220,bounds=bounds)
        from PIL import ImageDraw; ImageDraw.Draw(im).text((4,4),str(f),fill=(255,255,0)); ims.append(im)
    strip(ims,'getup_side.png',7)

# a groggy shake of the head while he squats (about the neck's own up axis, small, three swings)
HEAD = joint_names(BMD(B0)).index('head')
_locals = locals_at
def locals_at(f):
    L = _locals(f)
    if 21 <= f <= 31:
        u = (f - 21) / 10.0
        ang = np.radians(14.0) * np.sin(u * 3 * np.pi) * np.sin(u * np.pi)
        L[HEAD] = L[HEAD] @ np.block([[Rot.from_rotvec([ang, 0, 0]).as_matrix(), np.zeros((3, 1))], [np.zeros((1, 3)), np.ones((1, 1))]])
    return L
