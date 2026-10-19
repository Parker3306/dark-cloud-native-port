"""Build the original game model in Blender from tools/dc/export_model.py output, save a .blend and render reference views.

Run: blender -b --python blender/dc_build.py -- <model dir> <out .blend> [<render dir> [<size>]]
Game space is Y-up with +Z = the character's front (right-handed); Blender space is Z-up with the front toward -Y:
(x, y, z)_game -> (x, -z, y)_blender (a proper rotation, so triangle winding is preserved).
"""
import json
import math
import os
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
MODEL = Path(argv[0])
OUT_BLEND = Path(argv[1])
RENDER_DIR = Path(argv[2]) if len(argv) > 2 else None
SIZE = int(argv[3]) if len(argv) > 3 else 1024
SMOOTH = int(argv[4]) if len(argv) > 4 else 0       # subdivision levels for the REFERENCE RENDER only (rounder silhouettes, smooth shading)


BULK = float(__import__("os").environ.get("DC_BULK", "0"))      # 0 = original; 1 = full arm/shoulder muscle bulk (reference render only)


def bulk_arms(pos):
    """Game-space skin vertices: thicken shoulders/upper arms/forearms radially around the arm axis, tapering to nothing at the glove cuff."""
    pos = pos.copy()
    ax = np.abs(pos[:, 0])
    arm = (ax > 1.4) & (pos[:, 1] > 11.8)
    f = np.interp(ax, [1.4, 2.0, 3.0, 3.7, 4.6, 5.3, 5.6], [1.0, 1.35, 1.8, 1.65, 1.45, 1.2, 1.0])
    f = 1.0 + (f - 1.0) * BULK
    cy, cz = 12.85, 0.0
    dy = pos[:, 1] - cy
    dz = pos[:, 2] - cz
    pos[:, 1] = np.where(arm, cy + dy * f, pos[:, 1])
    pos[:, 2] = np.where(arm, cz + dz * f, pos[:, 2])
    return pos


def to_blender(p):
    return np.stack([p[:, 0], -p[:, 2], p[:, 1]], axis=1)


def load_image(name):
    path = MODEL / "tex" / f"{name}.png"
    if not path.exists():
        return None
    img = bpy.data.images.load(str(path))
    img.colorspace_settings.name = "sRGB"
    return img


def make_material(name, image):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    if image:
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = image
        tex.interpolation = "Closest"
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
    mat.blend_method = "BLEND" if hasattr(mat, "blend_method") else None
    return mat


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    meta = json.loads((MODEL / "model.json").read_text())
    data = np.load(MODEL / "model.npz")
    images = {n: load_image(n) for n in meta["textures"]}
    mats = {}
    coll = bpy.data.collections.new("DC_model")
    bpy.context.scene.collection.children.link(coll)
    flipped = 0
    for m in meta["meshes"]:
        k = m["key"]
        gpos = data[f"{k}_pos"]
        bw = MODEL / "bind_world.json"
        if bw.exists() and m["name"] != "skin" and os.environ.get("DC_BIND", "1") == "1":     # rigid parts sit in the rest pose; bring them to the bind (T) pose of the skin
            bind = json.loads(bw.read_text())
            b = meta["objects"][m["object"]]["parent"]
            if b is not None and b >= 0:
                rest = np.array(meta["objects"][b]["world"])
                bm = np.array(bind[b])
                if 0.05 < np.abs(rest[3, :3] - bm[3, :3]).max() < 7.0:
                    gpos = (np.c_[gpos, np.ones(len(gpos))] @ np.linalg.inv(rest) @ bm)[:, :3].astype(np.float32)
        if BULK and m["name"] == "skin":
            gpos = bulk_arms(gpos)
        pos = to_blender(gpos)
        uv = data[f"{k}_uv"]
        nrm = data[f"{k}_nrm"]
        tri = data[f"{k}_tri"]
        mat_idx = data[f"{k}_mat"]
        mesh = bpy.data.meshes.new(m["name"])
        faces = tri[:, :, 0].tolist()
        # winding check: compare the geometric normal with the stored vertex normal (converted)
        nb = to_blender(nrm)
        a, b, c = pos[tri[:, 0, 0]], pos[tri[:, 1, 0]], pos[tri[:, 2, 0]]
        geo = np.cross(b - a, c - a)
        stored = nb[tri[:, 0, 1]] + nb[tri[:, 1, 1]] + nb[tri[:, 2, 1]]
        flip = float((geo * stored).sum(axis=1).mean()) < 0
        if flip:
            faces = [[f[0], f[2], f[1]] for f in faces]
            flipped += 1
        mesh.from_pydata(pos.tolist(), [], faces)
        mesh.update()
        uvl = mesh.uv_layers.new(name="UV")
        for fi, poly in enumerate(mesh.polygons):
            corners = tri[fi] if not flip else tri[fi][[0, 2, 1]]
            for li, c_ in zip(poly.loop_indices, corners):
                u, v = uv[c_[2]]
                uvl.data[li].uv = (u, 1.0 - v)
        # materials
        for mi, tname in enumerate(m["materials"]):
            key = tname
            if key not in mats:
                mats[key] = make_material(tname, images.get(tname))
            mesh.materials.append(mats[key])
        for fi, poly in enumerate(mesh.polygons):
            poly.material_index = int(mat_idx[fi])
        obj = bpy.data.objects.new(m["name"], mesh)
        coll.objects.link(obj)
        if SMOOTH:
            for poly in mesh.polygons:
                poly.use_smooth = True
            mod = obj.modifiers.new("smooth", "SUBSURF")
            mod.levels = mod.render_levels = SMOOTH
    # skin weights as vertex groups on the 'skin' mesh
    names = [o["name"] for o in meta["objects"]]
    if "weights" in meta:
        skin = bpy.data.objects.get("skin")
        if skin:
            groups = {}
            for v, ws in meta["weights"]["vertices"].items():
                for b, w in ws.items():
                    g = groups.get(b) or skin.vertex_groups.new(name=names[int(b)])
                    groups[b] = g
                    g.add([int(v)], w / 100.0, "REPLACE")
    # joints as empties (for later skeleton building)
    for i, o in enumerate(meta["objects"]):
        if o["name"].startswith(("jnt", "ctr")):
            w = np.array(o["world"])[3, :3][None, :]
            e = bpy.data.objects.new(f"J_{o['name']}", None)
            e.empty_display_type = "SPHERE"
            e.empty_display_size = 0.15
            e.location = Vector(to_blender(w)[0].tolist())
            coll.objects.link(e)
    print(f"built {len(meta['meshes'])} meshes ({flipped} with flipped winding), {len(images)} images")


def setup_render():
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x = sc.render.resolution_y = SIZE
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sh = sc.display.shading
    sh.light = "STUDIO" if SMOOTH else "FLAT"
    sh.color_type = "TEXTURE"
    sh.show_backface_culling = False
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = float(__import__('os').environ.get('DC_ORTHO', '19.0'))
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    sc.camera = cam
    return cam


def render_views(cam):
    center = Vector((0, 0, float(__import__('os').environ.get('DC_CENTER_Y', '7.9'))))
    for name, yaw in (("front", 0), ("three_quarter", 35), ("side", 90), ("back", 180)):
        a = math.radians(yaw)
        cam.location = center + Vector((math.sin(a) * 40, -math.cos(a) * 40, 0))
        direction = center - cam.location
        cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        bpy.context.scene.render.filepath = str(RENDER_DIR / f"{name}.png")
        bpy.ops.render.render(write_still=True)
        print("rendered", name)


build()
OUT_BLEND.parent.mkdir(parents=True, exist_ok=True)
if RENDER_DIR:
    RENDER_DIR.mkdir(parents=True, exist_ok=True)
    render_views(setup_render())
bpy.ops.wm.save_as_mainfile(filepath=str(OUT_BLEND))
print("saved", OUT_BLEND)
