"""The field around Beedle's tent as four boxes (morning, day, evening, night) for his shop stage:
four sides each (the roof and floor hide the rest), 512 per face, drawn round the shop by PsrSky."""
import sys, os, numpy as np
from PIL import Image
sys.path[:0]=['/home/claude/anim']
from multibmd import write_multi
from rarcwrite import write_rarc
S=2500.0; EYE=140.0
up=np.array([0,1.0,0])
# render face -> (view direction, image right) in the shop's frame (+z is the doorway)
FACES={'front':((0,0,-1),(1,0,0)), 'back':((0,0,1),(-1,0,0)), 'left':((-1,0,0),(0,0,-1)), 'right':((1,0,0),(0,0,1))}
files={}
for t,short in (('morning','m'),('day','d'),('evening','e'),('night','n')):
    groups=[]
    for f,(d,r) in FACES.items():
        d=np.array(d,float); r=np.array(r,float); c=d*S+np.array([0,EYE,0])
        TL,TR,BR,BL=c-r*S+up*S, c+r*S+up*S, c+r*S-up*S, c-r*S-up*S
        img=np.array(Image.open(f'final/{t}_{f}.png').convert('RGBA').resize((512,512),Image.LANCZOS)); img[...,3]=255
        tris=[(np.array([TL,TR,BR]),np.array([[0,0],[1,0],[1,1]],float)),(np.array([TL,BR,BL]),np.array([[0,0],[1,1],[0,1]],float))]
        groups.append((img,False,tris))
    p=f'/home/claude/skyout/sky_{short}.bmd'
    write_multi(p,groups,jointname='sky_'+short,cull=0,maxtex=512)
    files[f'bmdr/sky_{short}.bmd']=open(p,'rb').read(); print(t,len(files[f'bmdr/sky_{short}.bmd'])//1024,'KB')
write_rarc('/home/claude/skyout/PsrSky.arc','PsrSky',files)
print('PsrSky.arc',os.path.getsize('/home/claude/skyout/PsrSky.arc')//1024,'KB')
