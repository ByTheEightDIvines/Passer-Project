import sys, re
sys.path[:0]=['/home/claude/w/anim']
import numpy as np
from bmd import BMD, joint_names
from PIL import Image
import render
inc=open('/home/claude/passer-project/src/bow_fit.inc').read()
def M(n):
    v=[float(x) for x in re.findall(r'-?\d+\.\d+',re.search(n+r' = \{(.*?)\};',inc,re.S).group(1))]; m=np.eye(4); m[:3,:]=np.array(v).reshape(3,4); return m
def Tr(x,y,z): m=np.eye(4); m[:3,3]=(x,y,z); return m
def rz(d):
    a=np.radians(d); m=np.eye(4); m[:2,:2]=[[np.cos(a),-np.sin(a)],[np.sin(a),np.cos(a)]]; return m
def ry(d):
    a=np.radians(d); m=np.eye(4); m[0,0]=m[2,2]=np.cos(a); m[0,2]=np.sin(a); m[2,0]=-np.sin(a); return m
RX=np.diag([1,-1,-1,1.0])
def posed_bow(data, a=160, b=20, line=(0,30,0)):
    bw=BMD(data)
    bw.jloc=[np.eye(4),np.eye(4),Tr(-5,0,0)@rz(a)@RX,Tr(5,0,0)@rz(b),Tr(*line)]
    bw.jworld=[None]*5
    for i in range(5): bw.jworld[i]=bw.world(i)
    bw.read_shp(); return bw
def img(b,y,W,H,bounds=None):
    im=np.asarray(render.render(b,y,W=W,H=H,bounds=bounds))
    if im.max()<=1.01: im=im*255
    return Image.fromarray(np.clip(im,0,255).astype('uint8'))
def strip(ims,path):
    c=Image.new('RGB',(sum(i.width for i in ims),max(i.height for i in ims)),(46,46,52));x=0
    for i in ims: c.paste(i,(x,0)); x+=i.width
    c.save(path)
