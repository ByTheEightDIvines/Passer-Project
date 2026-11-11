import sys, math
sys.argv=['x']; sys.path.insert(0,'/home/claude/anim')
exec(open('run_fight.py').read().split("if __name__")[0])
src,tgt,moi=load_rigs()
hip=tgt.rest_loc[0][1,3]/src.rest_loc[0][1,3]
inj=BCK(arcf(BASE+'Moi2.arc','moi_injury_walk.bck'))
walk=BCK(arcf(BASE+'Mgeneral.arc','m_walk_a.bck'))
ARM={'shoulderL','armL1','armL2','handL'}
frames=[]
for f in range(walk.dur+1):
    fs=f*inj.dur/walk.dur
    frames.append(retarget_frame(src,tgt,anim_locals(inj,26,fs),hip,override=ARM,base_locals=anim_locals(walk,19,f)))
d=write_bck(frames,loop=walk.loop); open('out/psr_walk_torch.bck','wb').write(d); print('walk_torch',len(d))
torch=BMD(arcf(BASE+'Moi2.arc','torch.bmd'))
body=BMD(arcf(PP+'P_MON_a.arc','mon_a.bmd'))
gear=rarc(open(PP+'PsrGear.arc','rb').read())
pack=BMD(gear['bmdr/pack_mon_a.bmd'])
C=np.eye(4); C[:3,:3]=tgt.rest_world[tgt.idx['handL']][:3,:3].T@src.rest_world[src.idx['handL']][:3,:3]
print('torch grip in passer handL frame (rotation part):\n',C.round(3))
picks=[0,8,17,25]
scenes=[]
for f in picks:
    tr,W=posed_tris(moi,anim_locals(inj,26,f*inj.dur/walk.dur)); s1=Scene(); s1.add(moi,tr); s1.add(torch,list(torch.tris),W[src.idx['handL']])
    tr,W=posed_tris(body,frames_to_locals(frames[f])); s2=Scene(); s2.add(body,tr); s2.add(pack,list(pack.tris),W[tgt.idx['backbone']]); s2.add(torch,list(torch.tris),W[tgt.idx['handL']]@C)
    scenes.append((f,s1,s2))
ims=[];labs=[]
for yaw in (35,-60):
  a_=math.radians(yaw);c,s=math.cos(a_),math.sin(a_);R=np.array([[c,0,s],[0,1,0],[-s,0,c]])
  allp=np.concatenate([np.concatenate([t[1] for t in sc.tris]) for _,s1,s2 in scenes for sc in (s1,s2)])@R.T
  bd=(allp.min(0),allp.max(0))
  for row in (1,2):
    for f,s1,s2 in scenes:
      ims.append(render(s1 if row==1 else s2,yaw,W=230,H=300,bounds=bd)); labs.append(('Rusl injury_walk ' if row==1 else 'passer walk+torch arm ')+f'f{f}')
sheet(ims,labs,4,'/mnt/user-data/outputs/torch_sheet.png')
