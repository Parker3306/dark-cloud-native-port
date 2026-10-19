"""Decimate the fitted TRELLIS mesh to the game budget, unwrap it fresh, and bake the (projected) high-resolution colour onto it.

Run: blender -b --python blender/fit_bake.py -- <mesh.npz> <warp.npz> <hi-res texture png> <out dir> <target triangles> [atlas size]
Input mesh = TRELLIS mesh arrays (Blender space) whose vertex positions are REPLACED by warp.npz['game'] (game space, Y up, +Z front).
Output: <out>/low.npz (pos [V,3] game space, tri [T,3], uv [T,3,2] per corner), <out>/atlas.png (baked colour), <out>/low_check.blend
Game -> Blender mapping: (x, y, z) -> (x, -z, y).
"""
import math
import os
import sys
from pathlib import Path

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
MESH, WARP, TEX, OUT = argv[0], argv[1], argv[2], Path(argv[3])
TARGET = int(argv[4])
ATLAS = int(argv[5]) if len(argv) > 5 else 2048
ANGLE = float(argv[6]) if len(argv) > 6 else 80.0
REGIONS = argv[7] if len(argv) > 7 else None      # ref_regions.npz: cylindrical per-region unwrap (few seams) instead of smart-project
OUT.mkdir(parents=True, exist_ok=True)

m = np.load(MESH)
game = np.load(WARP)["game"]
tri, uv = m["tri"], m["uv"]
bl = np.stack([game[:, 0], -game[:, 2], game[:, 1]], axis=1)

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene


def make_obj(name, verts, faces):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts.tolist(), [], faces.tolist())
    me.update()
    ob = bpy.data.objects.new(name, me)
    sc.collection.objects.link(ob)
    return ob


high = make_obj("high", bl, tri)
uvl = high.data.uv_layers.new(name="UV")
for fi, poly in enumerate(high.data.polygons):
    for k, li in enumerate(poly.loop_indices):
        uvl.data[li].uv = tuple(uv[fi][k])
for p in high.data.polygons:
    p.use_smooth = True

mat = bpy.data.materials.new("hi")
mat.use_nodes = True
nt = mat.node_tree
bsdf = nt.nodes["Principled BSDF"]
img = bpy.data.images.load(str(Path(TEX).resolve()))
img.colorspace_settings.name = "sRGB"
tn = nt.nodes.new("ShaderNodeTexImage")
tn.image = img
tn.interpolation = "Linear"
nt.links.new(tn.outputs["Color"], bsdf.inputs["Base Color"])
high.data.materials.append(mat)

# weld the TRELLIS shell (duplicate vertices at its patch borders): a connected mesh decimates and unwraps far better
import bmesh
bm = bmesh.new()
bm.from_mesh(high.data)
nv0 = len(bm.verts)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=2e-4)
print(f"welded {nv0} -> {len(bm.verts)} verts")
# TRELLIS fused the two boot cuffs into one band: cut the faces that bridge the feet near the centre line (game x ~ 0, y 0.9..2.9)
_fc = [float(v) for v in os.environ.get('DC_FEET_CUT', '0,0,0').split(',')]            # x tolerance, y min, y max (game units); default: no cut
gone = [f for f in bm.faces if _fc[0] > 0 and all(abs(v.co.x) < _fc[0] and _fc[1] < v.co.z < _fc[2] for v in f.verts)]
bmesh.ops.delete(bm, geom=gone, context="FACES")
print(f"removed {len(gone)} boot-bridge faces")
bm.to_mesh(high.data)
bm.free()
high.data.update()

import os
if os.environ.get("DC_EXPORT_HIGH"):                 # write the fitted, welded high mesh (geometry only) for a retopology pass, then stop
    for o in sc.objects:
        o.select_set(o == high)
    bpy.context.view_layer.objects.active = high
    high.data.materials.clear()
    bpy.ops.export_scene.gltf(filepath=os.environ["DC_EXPORT_HIGH"], use_selection=True, export_format="GLB", export_materials="NONE")
    print("EXPORTED high mesh")
    sys.exit(0)

# low-poly mesh: either a retopologised GLB (DC_LOW_GLB, welded here) or a collapse-decimated copy of the high mesh
LOW_GLB = os.environ.get("DC_LOW_GLB")
if LOW_GLB:
    before = set(sc.objects)
    bpy.ops.import_scene.gltf(filepath=LOW_GLB)
    low = [o for o in sc.objects if o not in before and o.type == "MESH"][0]
    mw = low.matrix_world.copy()
    low.parent = None
    low.data.transform(mw)
    low.matrix_world = type(mw)()
    bm = bmesh.new()
    bm.from_mesh(low.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(low.data)
    bm.free()
    low.name = "low"
    low.data.update()
    for lay in list(low.data.uv_layers):
        low.data.uv_layers.remove(lay)
    low.data.materials.clear()
    print(f"retopo mesh: {len(low.data.vertices)} verts, {len(low.data.polygons)} faces")
else:
    low = high.copy()
    low.data = high.data.copy()
    low.name = "low"
    sc.collection.objects.link(low)
    low.data.materials.clear()
    bpy.context.view_layer.objects.active = low
    for o in sc.objects:
        o.select_set(o == low)
    for lay in list(low.data.uv_layers):
        low.data.uv_layers.remove(lay)
    ratio = min(1.0, TARGET / len(tri))
    mod = low.modifiers.new("dec", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = ratio
    mod.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier="dec")
    low.data.update()
    print(f"decimated {len(tri)} -> {len(low.data.polygons)} faces")
bpy.context.view_layer.objects.active = low

# fresh unwrap
def region_unwrap():
    """Few seams on purpose (every UV seam duplicates vertex tuples in the game): split the body into 6 regions from the original skeleton,
    cut each region into a front and a back half (each half is a topological disc), unwrap each half angle-based, then pack."""
    rr = np.load(REGIONS)
    me = low.data
    vp = np.array([v.co[:] for v in me.vertices])
    gp = np.stack([vp[:, 0], vp[:, 2], -vp[:, 1]], axis=1)                  # game space
    lab = np.zeros(len(gp), int)
    for s0 in range(0, len(gp), 128):
        d = ((gp[s0:s0 + 128, None, :] - rr["pts"][None]) ** 2).sum(axis=2)
        lab[s0:s0 + 128] = rr["lab"][d.argmin(axis=1)]
    faces = np.array([p.vertices[:] for p in me.polygons])
    assert faces.shape[1] == 3
    fl = lab[faces][:, 0]
    for _ in range(3):                                                      # majority smoothing: face <- vertex <- faces
        vl = np.zeros((len(gp), 6))
        for k in range(3):
            np.add.at(vl, (faces[:, k], fl), 1)
        vlab = vl.argmax(axis=1)
        votes = np.stack([vlab[faces[:, k]] for k in range(3)], axis=1)
        fl = np.array([np.bincount(v, minlength=6).argmax() for v in votes])
    cen = gp[faces].mean(axis=1)
    group = np.zeros(len(faces), int)
    for r in range(6):
        sel = fl == r
        if not sel.any():
            continue
        zc = np.median(cen[sel, 2])
        group[sel] = r * 2 + (cen[sel, 2] > zc)
    me.uv_layers.new(name="UV")
    bpy.ops.object.mode_set(mode="EDIT")
    bm = bmesh.from_edit_mesh(me)
    bm.faces.ensure_lookup_table()
    bpy.ops.mesh.select_mode(type="FACE")
    for g in sorted(set(group.tolist())):
        bpy.ops.mesh.select_all(action="DESELECT")
        for fi in np.nonzero(group == g)[0]:
            bm.faces[int(fi)].select_set(True)
        bmesh.update_edit_mesh(me)
        bpy.ops.uv.unwrap(method="ANGLE_BASED", fill_holes=True, margin=0.0)
        print(f"group {g}: {int((group == g).sum())} faces unwrapped")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.average_islands_scale()
    # the head (region 1 = groups 2 and 3) is what people look at: give it more texels than the rest (pack_islands keeps relative island scale)
    fscale = float(os.environ.get("DC_FACE_SCALE", "1.7"))
    bm = bmesh.from_edit_mesh(me)
    bm.faces.ensure_lookup_table()
    uvl = bm.loops.layers.uv.active
    for g in (2, 3):
        fi = [int(i) for i in np.nonzero(group == g)[0]]
        if not fi:
            continue
        pts = np.array([lp[uvl].uv[:] for i in fi for lp in bm.faces[i].loops])
        c = pts.mean(axis=0)
        for i in fi:
            for lp in bm.faces[i].loops:
                u_, v_ = lp[uvl].uv
                lp[uvl].uv = (c[0] + (u_ - c[0]) * fscale, c[1] + (v_ - c[1]) * fscale)
    bmesh.update_edit_mesh(me)
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(margin=0.006, rotate=True)
    bpy.ops.object.mode_set(mode="OBJECT")


if REGIONS:
    region_unwrap()
else:
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(ANGLE), island_margin=0.003)
    bpy.ops.object.mode_set(mode="OBJECT")
for p in low.data.polygons:
    p.use_smooth = True

# bake target
lm = bpy.data.materials.new("lo")
lm.use_nodes = True
low.data.materials.append(lm)
bimg = bpy.data.images.new("atlas", ATLAS, ATLAS, alpha=False)
bn = lm.node_tree.nodes.new("ShaderNodeTexImage")
bn.image = bimg
lm.node_tree.nodes.active = bn

sc.render.engine = "CYCLES"
sc.cycles.device = "CPU"
sc.cycles.samples = 1
sc.cycles.use_denoising = False
sc.render.bake.use_pass_direct = False
sc.render.bake.use_pass_indirect = False
sc.render.bake.use_pass_color = True
sc.render.bake.use_selected_to_active = True
sc.render.bake.cage_extrusion = 0.15
sc.render.bake.max_ray_distance = 0.6
sc.render.bake.margin = 10
sc.render.bake.margin_type = "EXTEND"
for o in sc.objects:
    o.select_set(o == high)
low.select_set(True)
bpy.context.view_layer.objects.active = low
bpy.ops.object.bake(type="DIFFUSE")
bimg.filepath_raw = str(OUT / "atlas.png")
bimg.file_format = "PNG"
bimg.save()

# export arrays in game space
low.data.calc_loop_triangles()
vp = np.array([v.co[:] for v in low.data.vertices])
gpos = np.stack([vp[:, 0], vp[:, 2], -vp[:, 1]], axis=1)
ltri = np.array([t.vertices[:] for t in low.data.loop_triangles])
lu = low.data.uv_layers[0].data
luv = np.array([[lu[l].uv[:] for l in t.loops] for t in low.data.loop_triangles])
np.savez(OUT / "low.npz", pos=gpos, tri=ltri, uv=luv)
print(f"LOW {len(gpos)} verts {len(ltri)} tris")
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "low_check.blend"))
