import sys, os
sys.argv=['x']; sys.path.insert(0,'/home/claude/anim')
exec(open('run_fight.py').read().split("if __name__")[0])
src, tgt, moi = load_rigs()
moir = Rig(BMD(arcf(BASE+'MoiR.arc','moir.bmd')))
hip = tgt.rest_loc[0][1,3]/src.rest_loc[0][1,3]
JOBS = [  # (arc, source, out name, rig)
 ('Moi2','moi_s_fight_a.bck','psr_s_fight_a.bck',src),
 ('Moi2','moi_s_pullout.bck','psr_s_pullout.bck',src),
 ('Moi2','moi_s_setin.bck','psr_s_setin.bck',src),
 ('Moi2','moi_s_wait.bck','psr_s_wait.bck',src),
 ('MoiR2','moir_squareup.bck','psr_squareup.bck',moir),
 ('MoiR2','moir_squareup_step.bck','psr_squareup_step.bck',moir),
 ('MoiR2','moir_driveaway.bck','psr_driveaway.bck',moir),
 ('MoiR2','moir_surprised.bck','psr_surprised.bck',moir),
 ('MoiR3','moir_run.bck','psr_run.bck',moir),
 ('Moi2','moi_injury_walk.bck','psr_injury_walk.bck',src),
 ('Moi2','moi_injury_wait.bck','psr_injury_wait.bck',src),
]
os.makedirs('/home/claude/anim/out',exist_ok=True)
worst=0
for arc,name,out,rig in JOBS:
    h=tgt.rest_loc[0][1,3]/rig.rest_loc[0][1,3]
    a,frames=retarget_anim(rig,tgt,arc,name,h)
    d=write_bck(frames,loop=a.loop); open('/home/claude/anim/out/'+out,'wb').write(d)
    # verify: world joint positions from file vs from frames
    b=BCK(d); err=0
    for f in range(a.dur+1):
        Wa=world_mats(tgt.parent,frames_to_locals(frames[f])); Wb=world_mats(tgt.parent,anim_locals(b,19,f))
        err=max(err,max(np.abs(x[:3,3]-y[:3,3]).max() for x,y in zip(Wa,Wb)))
    worst=max(worst,err)
    print(f'{out:24s} {a.dur:3d}f loop={a.loop} {len(d):6d}B  maxJointErr={err:.3f}')
