import bpy, pickle, numpy as np, math, sys, os
argv=sys.argv[sys.argv.index('--')+1:]
TIME, OUT, RES = argv[0], argv[1], int(argv[2])
YAW = 9935/65536*2*math.pi
import json
_K=json.load(open('/home/claude/fieldbox/field_keys.json')); _Z=np.load('/home/claude/fieldbox/field_mats.npz')
D={(m,t):{'P':_Z[f'P{i}'],'UV':_Z[f'UV{i}'],'C':_Z[f'C{i}']} for i,(m,t) in enumerate(_K)}
TINT = {'morning':(0.95,0.84,0.72), 'day':(1.0,1.0,1.0), 'evening':(0.88,0.6,0.42), 'night':(0.045,0.055,0.1)}[TIME]
SKY = {'morning':((0.45,0.55,0.75),(1.0,0.8,0.62)), 'day':((0.25,0.5,0.88),(0.72,0.84,0.96)),
       'evening':((0.2,0.2,0.45),(0.98,0.55,0.3)), 'night':((0.004,0.006,0.02),(0.02,0.03,0.07))}[TIME]
# distance haze toward the horizon colour: start, end, how far it goes
HAZE = {'morning':(1200.0,12000.0,0.65), 'day':(3000.0,25000.0,0.35), 'evening':(2000.0,18000.0,0.45), 'night':(2000.0,15000.0,0.6)}[TIME]
CLOUD = {'morning':(1.0,0.85,0.75), 'day':(1.0,1.0,1.0), 'evening':(1.0,0.68,0.5), 'night':(0.03,0.035,0.05)}[TIME]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc=bpy.context.scene
sc.render.engine='CYCLES'; sc.cycles.samples=16; sc.cycles.use_denoising=False; sc.cycles.max_bounces=0
sc.render.resolution_x=sc.render.resolution_y=RES; sc.view_settings.view_transform='Standard'
cs,sn=math.cos(-YAW),math.sin(-YAW)
def conv(P):   # game (tent-centred) -> turned so the tent's door faces game +z -> blender (x,-z,y)
    x=P[...,0]*cs+P[...,2]*sn; z=-P[...,0]*sn+P[...,2]*cs
    return np.stack([x,-z,P[...,1]],-1)
imgs={}
for (mname,tname),e in D.items():
    if tname is None or 'fbtex' in tname: continue
    P=conv(e['P'].astype(float)); n=len(P)
    me=bpy.data.meshes.new(mname)
    verts=P.reshape(-1,3); faces=[(3*i,3*i+1,3*i+2) for i in range(n)]
    me.from_pydata(verts.tolist(),[],faces)
    uvl=me.uv_layers.new(); uv=e['UV'].reshape(-1,2).copy(); uv[:,1]=1-uv[:,1]; uvl.data.foreach_set('uv',uv.ravel())
    ca=me.color_attributes.new('vc','FLOAT_COLOR','CORNER'); ca.data.foreach_set('color',e['C'].reshape(-1,4).ravel())
    ob=bpy.data.objects.new(mname,me); sc.collection.objects.link(ob)
    m=bpy.data.materials.new(mname); m.use_nodes=True; nt=m.node_tree; nt.nodes.clear()
    o=nt.nodes.new('ShaderNodeOutputMaterial'); em=nt.nodes.new('ShaderNodeEmission')
    tx=nt.nodes.new('ShaderNodeTexImage')
    if tname not in imgs: imgs[tname]=bpy.data.images.load(f'/home/claude/fieldbox/tex/{tname}.png')
    tx.image=imgs[tname]; tx.interpolation='Linear'
    vc=nt.nodes.new('ShaderNodeVertexColor'); vc.layer_name='vc'
    mul=nt.nodes.new('ShaderNodeMix'); mul.data_type='RGBA'; mul.blend_type='MULTIPLY'; mul.inputs[0].default_value=1.0
    nt.links.new(tx.outputs['Color'],mul.inputs[6]); nt.links.new(vc.outputs['Color'],mul.inputs[7])
    tint=nt.nodes.new('ShaderNodeMix'); tint.data_type='RGBA'; tint.blend_type='MULTIPLY'; tint.inputs[0].default_value=1.0
    nt.links.new(mul.outputs[2],tint.inputs[6]); tint.inputs[7].default_value=(*TINT,1)
    cd=nt.nodes.new('ShaderNodeCameraData'); mr=nt.nodes.new('ShaderNodeMapRange')
    mr.inputs['From Min'].default_value=HAZE[0]; mr.inputs['From Max'].default_value=HAZE[1]
    mr.inputs['To Min'].default_value=0.0; mr.inputs['To Max'].default_value=HAZE[2]
    nt.links.new(cd.outputs['View Distance'],mr.inputs['Value'])
    hz=nt.nodes.new('ShaderNodeMix'); hz.data_type='RGBA'; hz.blend_type='MIX'
    nt.links.new(mr.outputs[0],hz.inputs[0]); nt.links.new(tint.outputs[2],hz.inputs[6]); hz.inputs[7].default_value=(*SKY[1],1)
    nt.links.new(hz.outputs[2],em.inputs['Color'])
    # alpha: overlays (names ending _x) blend by texture alpha x vertex alpha; leaf cards cut out
    am=nt.nodes.new('ShaderNodeMath'); am.operation='MULTIPLY'
    nt.links.new(tx.outputs['Alpha'],am.inputs[0]); nt.links.new(vc.outputs['Alpha'],am.inputs[1])
    alpha=am.outputs[0]
    if mname.endswith('_x'):
        pass
    elif not mname.startswith('bb_'):
        one=nt.nodes.new('ShaderNodeValue'); one.outputs[0].default_value=1.0; alpha=one.outputs[0]
    else:
        gt=nt.nodes.new('ShaderNodeMath'); gt.operation='GREATER_THAN'; gt.inputs[1].default_value=0.5
        nt.links.new(tx.outputs['Alpha'],gt.inputs[0]); alpha=gt.outputs[0]
    tr=nt.nodes.new('ShaderNodeBsdfTransparent'); mx=nt.nodes.new('ShaderNodeMixShader')
    nt.links.new(alpha,mx.inputs[0]); nt.links.new(tr.outputs[0],mx.inputs[1]); nt.links.new(em.outputs[0],mx.inputs[2])
    nt.links.new(mx.outputs[0],o.inputs[0])
    m.blend_method='BLEND'
    me.materials.append(m)
# sky: gradient from the horizon up
w=bpy.data.worlds.new('w'); sc.world=w; w.use_nodes=True; nt=w.node_tree; nt.nodes.clear()
wo=nt.nodes.new('ShaderNodeOutputWorld'); bg=nt.nodes.new('ShaderNodeBackground')
tc=nt.nodes.new('ShaderNodeTexCoord'); sep=nt.nodes.new('ShaderNodeSeparateXYZ'); ramp=nt.nodes.new('ShaderNodeValToRGB')
nt.links.new(tc.outputs['Generated'],sep.inputs[0])
ramp.color_ramp.elements[0].position=0.5; ramp.color_ramp.elements[0].color=(*SKY[1],1)
ramp.color_ramp.elements[1].position=0.75; ramp.color_ramp.elements[1].color=(*SKY[0],1)
nt.links.new(sep.outputs['Z'],ramp.inputs[0]); nt.links.new(ramp.outputs[0],bg.inputs[0]); nt.links.new(bg.outputs[0],wo.inputs[0])
tc.object=None
if TIME=='night':   # stars: small bright points on the upper sky
    vor=nt.nodes.new('ShaderNodeTexVoronoi'); vor.inputs['Scale'].default_value=260
    nt.links.new(tc.outputs['Generated'],vor.inputs['Vector'])
    lt=nt.nodes.new('ShaderNodeMath'); lt.operation='LESS_THAN'; lt.inputs[1].default_value=0.035
    nt.links.new(vor.outputs['Distance'],lt.inputs[0])
    hi=nt.nodes.new('ShaderNodeMath'); hi.operation='GREATER_THAN'; hi.inputs[1].default_value=0.56
    nt.links.new(sep.outputs['Z'],hi.inputs[0])
    st=nt.nodes.new('ShaderNodeMath'); st.operation='MULTIPLY'; nt.links.new(lt.outputs[0],st.inputs[0]); nt.links.new(hi.outputs[0],st.inputs[1])
    add=nt.nodes.new('ShaderNodeMix'); add.data_type='RGBA'; add.blend_type='ADD'
    nt.links.new(st.outputs[0],add.inputs[0]); nt.links.new(ramp.outputs[0],add.inputs[6]); add.inputs[7].default_value=(0.75,0.78,0.9,1)
    nt.links.new(add.outputs[2],bg.inputs[0])
def card(name, img, size, loc, rot, color, alpha_scale=1.0, uv_tile=1.0):
    bpy.ops.mesh.primitive_plane_add(size=size, location=loc, rotation=rot)
    ob=bpy.context.object; ob.name=name
    if uv_tile!=1.0:
        for l in ob.data.uv_layers.active.data: l.uv=(l.uv[0]*uv_tile,l.uv[1]*uv_tile)
    m=bpy.data.materials.new(name); m.use_nodes=True; nt2=m.node_tree; nt2.nodes.clear()
    o=nt2.nodes.new('ShaderNodeOutputMaterial'); em=nt2.nodes.new('ShaderNodeEmission'); tx=nt2.nodes.new('ShaderNodeTexImage')
    tx.image=bpy.data.images.load(f'/home/claude/fieldbox/tex/{img}.png')
    mul=nt2.nodes.new('ShaderNodeMix'); mul.data_type='RGBA'; mul.blend_type='MULTIPLY'; mul.inputs[0].default_value=1.0
    nt2.links.new(tx.outputs['Color'],mul.inputs[6]); mul.inputs[7].default_value=(*color,1); nt2.links.new(mul.outputs[2],em.inputs['Color'])
    am=nt2.nodes.new('ShaderNodeMath'); am.operation='MULTIPLY'; am.inputs[1].default_value=alpha_scale
    nt2.links.new(tx.outputs['Alpha'],am.inputs[0])
    tr=nt2.nodes.new('ShaderNodeBsdfTransparent'); mx=nt2.nodes.new('ShaderNodeMixShader')
    nt2.links.new(am.outputs[0],mx.inputs[0]); nt2.links.new(tr.outputs[0],mx.inputs[1]); nt2.links.new(em.outputs[0],mx.inputs[2])
    nt2.links.new(mx.outputs[0],o.inputs[0]); ob.data.materials.append(m)
    return ob
# clouds: the game's cloud texture tiled across a high layer (it repeats, as the sky dome's does)
card('clouds','cloudtx_01',120000,(0,0,7000),(0,0,0),CLOUD,0.9,uv_tile=14.0)
if TIME=='night':   # the game's moon, low in the sky behind the castle side
    md=math.radians(30); el=math.radians(28); dist=40000
    card('moon','f_moon',3600,(dist*math.cos(el)*math.sin(md),dist*math.cos(el)*math.cos(md),dist*math.sin(el)),
         (math.radians(90)-el,0,math.pi-md),(0.9,0.92,1.0))
# camera: at the tent, eye height through the windows
cam=bpy.data.cameras.new('c'); cam.lens_unit='FOV'; cam.angle=math.radians(90); cam.clip_start=5; cam.clip_end=80000
co=bpy.data.objects.new('c',cam); sc.collection.objects.link(co); sc.camera=co
co.location=(0,0,140)
FACES={'front':(math.radians(90),0,math.radians(0)), 'back':(math.radians(90),0,math.radians(180)),
       'left':(math.radians(90),0,math.radians(90)), 'right':(math.radians(90),0,math.radians(-90)),
       'up':(math.radians(180),0,0), 'down':(0,0,0)}
os.makedirs(OUT,exist_ok=True)
for f,rot in FACES.items():
    co.rotation_euler=rot
    sc.render.filepath=f'{OUT}/{TIME}_{f}.png'
    bpy.ops.render.render(write_still=True)
    print('rendered',f)
