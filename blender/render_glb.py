"""Render a GLB from several angles with Workbench (texture colour if present, else clay) and print mesh statistics.

Run: blender -b --python blender/render_glb.py -- <in.glb> <out dir> [size] [clay]
The model is scaled so its height is 15.8 units (Toan's) and stood on the ground, facing -Y (Blender front) if it faces +Y after import.
"""
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
GLB, OUT = Path(argv[0]), Path(argv[1])
SIZE = int(argv[2]) if len(argv) > 2 else 1024
CLAY = len(argv) > 3 and argv[3] == "clay"

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(GLB))
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if "--flat" not in argv:             # TRELLIS GLBs carry no normals; without this Blender shades every face flat and the model looks carved
    for o in meshes:
        for poly in o.data.polygons:
            poly.use_smooth = True
import os
if os.environ.get("DC_TEX"):          # swap the base-colour image (e.g. a projected texture) before rendering
    for m_ in bpy.data.materials:
        for n_ in m_.node_tree.nodes:
            if n_.type == "TEX_IMAGE" and any(l.to_socket.name == "Base Color" for l in m_.node_tree.links if l.from_node == n_):
                n_.image = bpy.data.images.load(os.environ["DC_TEX"])
                n_.image.colorspace_settings.name = "sRGB"
tris = verts = 0
for o in meshes:
    me = o.data
    me.calc_loop_triangles()
    tris += len(me.loop_triangles)
    verts += len(me.vertices)
lo = Vector((1e9,) * 3)
hi = Vector((-1e9,) * 3)
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        lo = Vector(map(min, lo, w))
        hi = Vector(map(max, hi, w))
print(f"STATS meshes={len(meshes)} verts={verts} tris={tris} bbox_min={tuple(round(x, 2) for x in lo)} bbox_max={tuple(round(x, 2) for x in hi)} "
      f"materials={len(bpy.data.materials)} images={len(bpy.data.images)}")
center = (lo + hi) / 2
height = hi.z - lo.z
OUT.mkdir(parents=True, exist_ok=True)
sc = bpy.context.scene
sc.render.engine = "BLENDER_WORKBENCH"
sc.render.resolution_x = sc.render.resolution_y = SIZE
sc.render.film_transparent = False
sc.world = bpy.data.worlds.new("w")
sc.world.color = (0.25, 0.25, 0.28)
sh = sc.display.shading
sh.light = "FLAT" if os.environ.get("DC_FLAT") else "STUDIO"
sh.color_type = "SINGLE" if CLAY else "TEXTURE"
sh.single_color = (0.75, 0.72, 0.68)
cam_d = bpy.data.cameras.new("c")
cam_d.type = "ORTHO"
cam_d.ortho_scale = height * 1.2
cam = bpy.data.objects.new("c", cam_d)
sc.collection.objects.link(cam)
sc.camera = cam
for name, yaw in (("front", 0), ("three_quarter", 35), ("side", 90), ("back", 180)):
    a = math.radians(yaw)
    cam.location = center + Vector((math.sin(a) * 40, -math.cos(a) * 40, 0))
    cam.rotation_euler = (center - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = str(OUT / f"{name}.png")
    bpy.ops.render.render(write_still=True)
print("rendered to", OUT)
