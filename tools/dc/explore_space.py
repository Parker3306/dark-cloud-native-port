"""Exploration: world positions of Toan's joints and where each mesh sits (local bbox vs the object's world position)."""
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).parent))
import mds as M  # noqa: E402
import pack as P  # noqa: E402

pk = P.parse((ROOT / "data/dun/mainchara/c01d.chr").read_bytes())
mds = M.parse(pk.get("c01d.mds").data)
bbp = pk.get("c01d.bbp").data
world = []
for i, o in enumerate(mds.objects):
    local = np.array(o.matrix, dtype=np.float64).reshape(4, 4)
    world.append(local if o.parent < 0 else local @ world[o.parent])          # row-vector convention
bind = [np.array(struct.unpack_from("<16f", bbp, 64 * i)).reshape(4, 4) for i in range(len(mds.objects))]
print("object            parent  world translation (accumulated)      .bbp translation")
for i, o in enumerate(mds.objects):
    if o.name.startswith(("jnt", "ctr", "c01d_1")) or o.mesh:
        w, b = world[i][3, :3], bind[i][3, :3]
        bb = ""
        if o.mesh:
            p = np.array(o.mesh.positions)[:, :3]
            bb = f" mesh bbox min {np.round(p.min(0), 1)} max {np.round(p.max(0), 1)}"
        print(f"{i:3} {o.name:10} p{o.parent:3} world {np.round(w, 2)}  bbp {np.round(b, 2)}{bb}")
