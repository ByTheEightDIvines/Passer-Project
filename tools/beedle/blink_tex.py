"""Beedle's blink: his own open eye (t0) with the upper lid down halfway, and shut. Lid in his face's skin
(median of the face in his body atlas), the lash line in his eye ring's brown, darker."""
import numpy as np; from PIL import Image
E=np.asarray(Image.open('/home/claude/beedle/beedle_t0.png').convert('RGBA')).astype(float)
S=4; N=256*S; C=127.5*S; R=83.5*S                       # the eye disk: x,y 44..211
LASH=np.array([58,44,26.])
# The lid's skin: his face's own skin, taken from his body atlas (t1) at the very spot the eye covers
# (eye texture -> model x,y -> face texture), so colour and grain match what's around the eye.
M=np.load('/home/claude/beedle/eye_map.npz'); A,J,UV0,C0=M['A'],M['J'],M['UV'],M['C']
T1=np.asarray(Image.open('/home/claude/beedle/beedle_t1.png').convert('RGB')).astype(float)
def skin_base():
    g=(np.mgrid[0:N,0:N][::-1]+0.5)/N                     # u (x), v (y) of the eye texture
    xy=np.stack([g[0],g[1],np.ones_like(g[0])],-1)@A
    uv=UV0+(xy[...,0:1]-C0[0])*J[0]+(xy[...,1:2]-C0[1])*J[1]
    fx=uv[...,0]*512-0.5; fy=uv[...,1]*512-0.5
    x0=np.floor(fx).astype(int); y0=np.floor(fy).astype(int); ax=(fx-x0)[...,None]; ay=(fy-y0)[...,None]
    S_=lambda yy_,xx_: T1[np.clip(yy_,0,511),np.clip(xx_,0,511)]
    return (S_(y0,x0)*(1-ax)*(1-ay)+S_(y0,x0+1)*ax*(1-ay)+S_(y0+1,x0)*(1-ax)*ay+S_(y0+1,x0+1)*ax*ay)
SK=None
yy,xx=np.mgrid[0:N,0:N]+0.5; u=(xx-C)/R                 # u: -1..1 across the eye
def lid(center_drop, bow):
    """y of the lid's edge across the eye. bow > 0: highest in the middle (an open lid's arch, up over
    the eyeball); bow < 0: lowest in the middle (a shut eye's curve)."""
    return C + center_drop*R - bow*R*(1-np.clip(u,-1,1)**2)
def make(drop, bow, lower=False):
    big=np.asarray(Image.fromarray(E.astype(np.uint8)).resize((N,N),Image.LANCZOS)).astype(float)
    rgb=big[...,:3].copy()
    edge=lid(drop,bow)
    above=yy<edge
    # the lid: lighter up top, a touch darker into the crease near its edge
    global SK
    if SK is None: SK=skin_base()
    # shading: the lid is round over the eyeball - lit from above, darker toward its rim and into the
    # crease just above the lash line
    r=np.clip(np.hypot(xx-C,yy-C)/R,0,1)
    ball=1.0+0.07*(1-r**2)                                 # a soft bulge, matching the face at the rim
    top=1.0+0.04*np.clip((C-yy)/R,-1,1)*(1-r**2)           # light from above, gone by the rim
    crease=1.0-0.18*np.exp(-((edge-yy)/(0.16*R))**2)*(yy<edge)
    lidc=SK*(ball*top*crease)[...,None]
    rgb=np.where(above[...,None],lidc,rgb)
    # its shadow on the eye just under it
    if not lower:
        sh=np.exp(-((yy-edge)/(0.09*R))**2)*(yy>edge)
        rgb=rgb*(1-0.45*sh[...,None])
    else:
        low=np.clip(np.hypot(xx-C,yy-C)/R,0,1)
        shade=(1.0+0.05*(1-low**2))*(1.0-0.14*np.exp(-((yy-edge)/(0.12*R))**2))   # round, and tucked under the lash
        rgb=np.where(~above[...,None], SK*shade[...,None], rgb)   # shut: skin below too
    # the lash line along the lid's edge
    d=yy-edge
    if lower: lash=0.55*np.exp(-(d/(0.045*R))**2)
    else: lash=0.75*np.exp(-(np.maximum(d,0)/(0.05*R))**2)*np.exp(-(np.minimum(d,0)/(0.025*R))**2)
    rgb=rgb*(1-lash[...,None])+LASH[None,None]*lash[...,None]
    # the eye's own ring stays on top (its outline): keep the original wherever it's the dark ring
    ring=(np.hypot(xx-C,yy-C)>R*0.90)&(yy>edge+0.04*R)&(not lower)
    rgb=np.where(ring[...,None],big[...,:3],rgb)
    out=np.concatenate([rgb,big[...,3:]],-1)
    im=Image.fromarray(np.clip(out,0,255).astype(np.uint8)).resize((256,256),Image.LANCZOS)
    a=np.asarray(im).copy(); a[...,3]=E[...,3].astype(np.uint8)       # exactly his eye's cut-out
    return Image.fromarray(a)
make(0.30,0.28).save('/home/claude/beedle/eye_half.png')
make(0.12,-0.22,lower=True).save('/home/claude/beedle/eye_shut.png')
