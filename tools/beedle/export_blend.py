"""Run inside Blender (blender -b FILE.blend --python export_blend.py -- OUT.pkl): Beedle's meshes in the rest
pose, in the game's units and axes (centimetres, Y up, facing +Z), per triangle corner: position, normal,
uv (v flipped), bone weights (backbone1 + backbone2 merged into the game's 'backbone'); each material's
base-colour image; the bones' heads. Read by build_beedle.py."""
import bpy, sys, pickle
import numpy as np
out = sys.argv[sys.argv.index('--') + 1]
TO_GAME = lambda v: (v[0] * 100.0, v[2] * 100.0, -v[1] * 100.0)
RENAME = {'backbone1': 'backbone', 'backbone2': 'backbone'}
arm = bpy.data.objects['Beedle_Armature']
bones = {}
for b in arm.data.bones:
    bones[RENAME.get(b.name, b.name)] = bones.get(RENAME.get(b.name, b.name)) or TO_GAME(arm.matrix_world @ b.head_local)
def image_of(mat):
    if mat is None or not mat.use_nodes: return None
    for n in mat.node_tree.nodes:
        if n.type == 'BSDF_PRINCIPLED':
            l = n.inputs['Base Color'].links
            if l and l[0].from_node.type == 'TEX_IMAGE': return l[0].from_node.image
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image is not None: return n.image
    return None
images = {}
meshes = []
dg = bpy.context.evaluated_depsgraph_get()
for o in bpy.data.objects:
    if o.type != 'MESH' or not o.visible_get(): continue
    # rest pose: armature modifiers off while we read it, the object's own transform applied
    saved = [(m, m.show_viewport) for m in o.modifiers if m.type == 'ARMATURE']
    for m, _ in saved: m.show_viewport = False
    dg.update()
    ev = o.evaluated_get(dg); me = ev.to_mesh()
    me.calc_loop_triangles()
    try: me.calc_normals_split()
    except Exception: pass
    W = o.matrix_world; Wn = W.to_3x3().inverted().transposed()
    uvl = me.uv_layers.active
    def uv_layer_for(mat):
        # a material that names its own UV map (the eyes use Original_UV, the rest the TP atlas)
        if mat is not None and mat.use_nodes:
            for nd in mat.node_tree.nodes:
                if nd.type == 'UVMAP' and nd.uv_map in me.uv_layers: return me.uv_layers[nd.uv_map]
        return uvl
    gnames = {g.index: RENAME.get(g.name, g.name) for g in o.vertex_groups}
    # an object parented to a bone with no groups: all of it on that bone
    whole = o.parent_bone if (o.parent_type == 'BONE' and o.parent_bone) else None
    tris = []
    for t in me.loop_triangles:
        mi = t.material_index
        mat = o.material_slots[mi].material if mi < len(o.material_slots) else None
        corners = []
        ul = uv_layer_for(mat)
        for li, vi in zip(t.loops, t.vertices):
            v = me.vertices[vi]
            p = TO_GAME(W @ v.co)
            n = Wn @ me.loops[li].normal; n.normalize(); n = TO_GAME(n)
            uv = (ul.data[li].uv[0], 1.0 - ul.data[li].uv[1]) if ul else (0.0, 0.0)
            w = {}
            for g in v.groups:
                nm = gnames.get(g.group)
                if nm in bones and g.weight > 1e-4: w[nm] = w.get(nm, 0.0) + g.weight
            if not w and whole: w = {RENAME.get(whole, whole): 1.0}
            corners.append((p, tuple(x / 100.0 for x in n), uv, w))
        tris.append((mat.name if mat else '-', corners))
    for m, s in saved: m.show_viewport = s
    for s in o.material_slots:
        if s.material and s.material.name not in images:
            im = image_of(s.material)
            if im is not None:
                im.pixels[0]  # load
                px = np.array(im.pixels[:], np.float32).reshape(im.size[1], im.size[0], im.channels)
                if im.channels == 3: px = np.concatenate([px, np.ones(px.shape[:2] + (1,), np.float32)], -1)
                images[s.material.name] = (im.name, (np.clip(px[::-1], 0, 1) * 255).astype(np.uint8))
            else:
                bc = s.material.diffuse_color
                images[s.material.name] = (None, np.array([[[int(bc[0]*255), int(bc[1]*255), int(bc[2]*255), 255]]], np.uint8))
    meshes.append((o.name, tris))
    ev.to_mesh_clear()
# the eye textures for blinking (open, half, closed, look down)
eyes = {}
for im in bpy.data.images:
    if im.name.startswith('bs_eye') and im.size[0] > 0:
        px = np.array(im.pixels[:], np.float32).reshape(im.size[1], im.size[0], im.channels)
        eyes[im.name] = (np.clip(px[::-1], 0, 1) * 255).astype(np.uint8)
pickle.dump(dict(bones=bones, meshes=meshes, images=images, eyes=eyes), open(out, 'wb'))
print('EXPORTED', {m: len(t) for m, t in meshes}, list(images), list(eyes))
