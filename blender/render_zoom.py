"""Zoomed orthographic renders of 'high' or 'low' from low_check.blend (unlit, textured).
Run: blender -b <blend> --python blender/render_zoom.py -- <out dir> <object: high|low> <centre x,y,z game> <ortho size> [size]"""
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

a = sys.argv[sys.argv.index("--") + 1:]
OUT = Path(a[0]); OUT.mkdir(parents=True, exist_ok=True)
which = a[1]
cx, cy, cz = (float(v) for v in a[2].split(","))
scale = float(a[3])
SIZE = int(a[4]) if len(a) > 4 else 900
sc = bpy.context.scene
for o in list(sc.objects):
    if o.type == "MESH" and o.name != which:
        bpy.data.objects.remove(o)
ob = bpy.data.objects[which]
if which == "low":
    mat = bpy.data.materials["lo"]
sc.render.engine = "BLENDER_WORKBENCH"
sc.render.resolution_x = sc.render.resolution_y = SIZE
sh = sc.display.shading
sh.light = "FLAT"; sh.color_type = "TEXTURE"
sc.world = bpy.data.worlds.new("w"); sc.world.color = (0.5, 0.5, 0.5)
cd = bpy.data.cameras.new("c"); cd.type = "ORTHO"; cd.ortho_scale = scale
cam = bpy.data.objects.new("c", cd); sc.collection.objects.link(cam); sc.camera = cam
centre = Vector((cx, -cz, cy))                      # game -> blender
for name, yaw in (("front", 0), ("side", 90), ("three_quarter", 35)):
    r = math.radians(yaw)
    cam.location = centre + Vector((math.sin(r) * 40, -math.cos(r) * 40, 0))
    cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = str(OUT / f"{which}_{name}.png")
    bpy.ops.render.render(write_still=True)
