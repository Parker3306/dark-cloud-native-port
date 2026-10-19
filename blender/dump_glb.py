"""Dump a GLB's mesh arrays (Blender space: +Z up, front toward -Y) and its texture images for numpy processing.

Run: blender -b --python blender/dump_glb.py -- <in.glb> <out dir>
Writes mesh.npz (pos [V,3], tri [T,3], uv [T,3,2] per corner, nrm [T,3]), tex_<node image name>.png and prints which image feeds which socket.
"""
import sys
from pathlib import Path

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
OUT = Path(argv[1])
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=argv[0])
obj = [o for o in bpy.context.scene.objects if o.type == "MESH"][0]
me = obj.data
me.calc_loop_triangles()
mw = np.array(obj.matrix_world)
pos = np.array([v.co[:] for v in me.vertices])
pos = pos @ mw[:3, :3].T + mw[:3, 3]
tri = np.array([t.vertices[:] for t in me.loop_triangles])
uvl = me.uv_layers[0].data
uv = np.array([[uvl[l].uv[:] for l in t.loops] for t in me.loop_triangles])
a, b, c = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
nrm = np.cross(b - a, c - a)
nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12
np.savez(OUT / "mesh.npz", pos=pos, tri=tri, uv=uv, nrm=nrm)
print("DUMP", pos.shape, tri.shape, pos.min(0), pos.max(0))
for m in me.materials:
    nt = m.node_tree
    for n in nt.nodes:
        if n.type == "TEX_IMAGE":
            tgt = [(l.to_node.type, l.to_socket.name) for l in nt.links if l.from_node == n]
            p = OUT / f"tex_{n.image.name}.png"
            n.image.filepath_raw = str(p)
            n.image.file_format = "PNG"
            n.image.save()
            print("TEX", n.image.name, n.image.size[:], tgt)
