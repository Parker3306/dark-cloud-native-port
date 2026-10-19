"""Export a character/monster pack to a neutral format for Blender: world-space triangles, UVs, materials, skin weights, skeleton, PNG textures.

Usage: python -I tools/dc/export_model.py <pack path relative to data/> <mds name> <wgt name|-> <img name> <out dir>
Example: python -I tools/dc/export_model.py dun/mainchara/c01d.chr c01d.mds c01d.wgt c01d01_dun.img work/models/toan
Writes <out>/model.json (+ model.npz with the big arrays) and <out>/tex/<name>.png.
Coordinates stay in the game's space (Y up, +Z = character front, right-handed); Blender import converts.
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).parent))
import img as I  # noqa: E402
import mds as M  # noqa: E402
import pack as P  # noqa: E402
import wgt as W  # noqa: E402


def world_matrices(mds):
    world = []
    for o in mds.objects:
        local = np.array(o.matrix, dtype=np.float64).reshape(4, 4)
        world.append(local if o.parent < 0 else local @ world[o.parent])        # row-vector convention
    return world


def triangulate(group, stride):
    """Triangle list (type 3) or strip (type 4) -> list of (tuple, tuple, tuple); degenerate triangles dropped."""
    t = group.tuples
    tris = []
    if group.type == 3:
        tris = [(t[i], t[i + 1], t[i + 2]) for i in range(0, len(t) - 2, 3)]
    else:
        for i in range(len(t) - 2):
            a, b, c = (t[i], t[i + 1], t[i + 2]) if i % 2 == 0 else (t[i + 1], t[i], t[i + 2])
            tris.append((a, b, c))
    return [x for x in tris if len({x[0][0], x[1][0], x[2][0]}) == 3]


def main(pack_rel, mds_name, wgt_name, img_name, out):
    out = Path(out)
    (out / "tex").mkdir(parents=True, exist_ok=True)
    pk = P.parse((ROOT / "data" / pack_rel).read_bytes())
    mds = M.parse(pk.get(mds_name).data)
    world = world_matrices(mds)
    meta = {"pack": pack_rel, "mds": mds_name, "objects": [], "meshes": [], "textures": {}}
    arrays = {}
    for i, o in enumerate(mds.objects):
        meta["objects"].append({"name": o.name, "parent": o.parent, "has_mesh": o.mesh is not None,
                                "world": world[i].tolist(), "local": np.array(o.matrix).reshape(4, 4).tolist()})
    for i, o in enumerate(mds.objects):
        m = o.mesh
        if not m:
            continue
        pos = np.array(m.positions)[:, :3]
        wp = np.c_[pos, np.ones(len(pos))] @ world[i]
        nrm = np.array(m.normals)[:, :3]
        uv = np.array(m.uvs)[:, :2]
        tris, mats = [], []
        for g in m.groups:
            for a, b, c in triangulate(g, m.stride):
                tris.append([[a[0], a[1], a[2]], [b[0], b[1], b[2]], [c[0], c[1], c[2]]])
                mats.append(g.material)
        key = f"m{i}"
        arrays[f"{key}_pos"] = wp[:, :3].astype(np.float32)
        arrays[f"{key}_nrm"] = nrm.astype(np.float32)
        arrays[f"{key}_uv"] = uv.astype(np.float32)
        arrays[f"{key}_tri"] = np.array(tris, dtype=np.int32)       # (n, 3 corners, [pos, nrm, uv])
        arrays[f"{key}_mat"] = np.array(mats, dtype=np.int32)
        meta["meshes"].append({"object": i, "name": o.name, "key": key, "materials": m.texture_names(),
                               "n_pos": len(pos), "n_tri": len(tris)})
    if wgt_name != "-":
        recs, _ = W.parse(pk.get(wgt_name).data)
        vw = W.vertex_weights(recs)
        meta["weights"] = {"target": recs[0].target, "vertices": {str(v): {str(b): w for b, w in ws.items()} for v, ws in vw.items()}}
    if img_name != "-":
        for name, t in I.textures(pk.get(img_name).data):
            im = t.decode(raw_alpha=False)
            im.save(out / "tex" / f"{name}.png")
            meta["textures"][name] = [t.width, t.height, t.bpp]
    np.savez_compressed(out / "model.npz", **arrays)
    (out / "model.json").write_text(json.dumps(meta))
    tris = sum(m["n_tri"] for m in meta["meshes"])
    print(f"exported {len(meta['objects'])} objects, {len(meta['meshes'])} meshes, {tris} triangles, "
          f"{len(meta['textures'])} textures -> {out}")


if __name__ == "__main__":
    main(*sys.argv[1:6])
