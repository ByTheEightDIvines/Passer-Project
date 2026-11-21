"""The cook's outfit: shirt, vest and sleeves cream (a cook's jacket), the sash plain cloth; scarf, shorts,
skin and the rest untouched. Recoloured from his own texture: each garment keeps its folds and shading
(its brightness relative to its own average), on a new base colour."""
import numpy as np; from PIL import Image, ImageFilter
src=Image.open('/home/claude/cook/t0.png').convert('RGBA'); A=np.asarray(src).astype(float)
rgb=A[...,:3]; H,W=rgb.shape[:2]
L=rgb@np.array([0.299,0.587,0.114])
yy,xx=np.mgrid[0:H,0:W]
def box(x0,y0,x1,y1): return (xx>=x0)&(xx<x1)&(yy>=y0)&(yy<y1)
CREAM=np.array([228,218,192.]); CLOTH=np.array([176,160,128.])
out=rgb.copy()
def tint(mask,base,strength=1.0,smooth=0):
    l=L if not smooth else np.asarray(Image.fromarray(L.astype(np.uint8)).filter(ImageFilter.GaussianBlur(smooth))).astype(float)
    m=l[mask].mean(); det=np.clip(l/m,0.45,1.35)
    det=1+(det-1)*strength
    out[mask]=(base[None]*det[mask][:,None])
torso=box(44,92,128,166)          # vest, shirt front and back, sleeve openings
r,g,b=rgb[...,0],rgb[...,1],rgb[...,2]
dark_bg=L<35                       # the texture's empty background between islands: leave it
vest=torso&(g>r*0.95)&~dark_bg
shirt=torso&~vest&~dark_bg
openings=box(44,108,62,162)       # the sleeve openings (striped): plain cream, their folds kept
emblem=box(70,100,104,140)         # the belly emblem: kept, but faint, like stitching
tint(vest&~openings,CREAM,0.75); tint(shirt&~openings&~emblem,CREAM,0.75)
tint(emblem&torso&~vest,CREAM,0.30); tint(openings,CREAM,0.35,smooth=2.5)
sleeve=box(66,166,81,228)&~dark_bg # the short sleeves (striped): plain cream
tint(sleeve,CREAM,0.8,smooth=2.5)
sash=box(0,96,29,160)
tint(sash,CLOTH,0.8,smooth=2.2)   # the pattern softened away, its folds kept
o=np.concatenate([np.clip(out,0,255),A[...,3:]],-1).astype(np.uint8)
Image.fromarray(o).save('/home/claude/cook/t0_cook.png')

# Grey hair, brows and moustache (Chef Boyardee): his dark blue-black hair, lifted to silver, its strands kept
A2=np.asarray(Image.open('/home/claude/cook/t0_cook.png').convert('RGBA')).astype(float)
c=A2[...,:3]; r,g,b=c[...,0],c[...,1],c[...,2]; Lh=c@np.array([0.299,0.587,0.114])
head=box(0,0,128,92)
hair=head&(b>r+6)                                       # the blue-black: hair, brows, moustache (skin is warm)
soft=np.asarray(Image.fromarray((hair*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.8))).astype(float)/255
lo,hi=np.percentile(Lh[hair],5),np.percentile(Lh[hair],95)
t=np.clip((Lh-lo)/(hi-lo),0,1)
grey=(70+150*t)[...,None]*np.array([1.0,1.0,1.03])[None,None]   # dark grey in the shadows up to near white
c=c*(1-soft[...,None])+grey*soft[...,None]
Image.fromarray(np.concatenate([np.clip(c,0,255),A2[...,3:]],-1).astype(np.uint8)).save('/home/claude/cook/t0_cook.png')
