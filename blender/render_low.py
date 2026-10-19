"""Render the low-poly mesh from <dir>/low_check.blend with its baked atlas (unlit), 4 views + wire overlay.
Run: blender -b <dir>/low_check.blend --python blender/render_low.py -- <out dir> [size]"""
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
OUT = Path(argv[0]); OUT.mkdir(parents=True, exist_ok=True)
SIZE = int(argv[1]) if len(argv) > 1 else 1024
sc = bpy.context.scene
for o in list(sc.objects):
    if o.name == "high":
        bpy.data.objects.remove(o)
low = bpy.data.objects["low"]
sc.render.engine = "BLENDER_WORKBENCH"
sc.render.resolution_x = sc.render.resolution_y = SIZE
sh = sc.display.shading
sh.light = "FLAT"
sh.color_type = "TEXTURE"
sc.world = bpy.data.worlds.new("w")
sc.world.color = (0.5, 0.5, 0.5)
cam_d = bpy.data.cameras.new("c"); cam_d.type = "ORTHO"; cam_d.ortho_scale = 19.0
cam = bpy.data.objects.new("c", cam_d); sc.collection.objects.link(cam); sc.camera = cam
center = Vector((0, 0, 7.9))
for name, yaw in (("front", 0), ("three_quarter", 35), ("side", 90), ("back", 180)):
    a = math.radians(yaw)
    cam.location = center + Vector((math.sin(a) * 40, -math.cos(a) * 40, 0))
    cam.rotation_euler = (center - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = str(OUT / f"{name}.png")
    bpy.ops.render.render(write_still=True)
# wireframe view
mod = low.modifiers.new("wf", "WIREFRAME"); mod.thickness = 0.01
sc.render.filepath = str(OUT / "wire.png")
cam.location = center + Vector((0, -40, 0)); cam.rotation_euler = (center - cam.location).to_track_quat("-Z", "Y").to_euler()
sh.color_type = "SINGLE"; sh.single_color = (0.9, 0.9, 0.9)
bpy.ops.render.render(write_still=True)
