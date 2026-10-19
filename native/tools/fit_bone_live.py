"""For the player's skin in the running game: group vertices that follow ONE bone at 100 %, fit the affine map (rest -> live) per bone and print it, to compare with our M_b."""
import json, struct, subprocess, sys
import numpy as np
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "coop")); sys.path.insert(0, str(ROOT / "tools" / "dc"))
from pine import Pine
import pack as P, mds as M, wgt as W
iso = sys.argv[1]; packname = sys.argv[2]; skin_obj = int(sys.argv[3]); stem = sys.argv[4]
CHARA = 0x01EA1D20
with Pine(28011) as p:
    F = p.read(CHARA + 0xBC); vis = p.read(F + skin_obj * 0x270 + 0x260); mdt = p.read(vis + 0x20); n = p.read(mdt + 0xC)
    buf = p.read_bytes(mdt + p.read(mdt + 0x10), n * 16)
    live = np.array([struct.unpack_from("<3f", buf, 16 * i) for i in range(n)])
sys.path.insert(0, str(Path(__file__).resolve().parent)); from dccore_disc import read_pack
pk = read_pack(iso, packname)
mds = M.parse(pk.get(stem + ".mds").data)
skin = mds.objects[skin_obj]
rest = np.array(skin.mesh.positions)[:, :3]
recs, _ = W.parse(pk.get(stem + ".wgt").data)
vw = W.vertex_weights(recs)
by_bone = {}
for v, ws in vw.items():
    if len(ws) == 1:
        b = next(iter(ws)); by_bone.setdefault(b, []).append(v)
print("bones with single-bone vertices:", {b: len(v) for b, v in sorted(by_bone.items())})
for b, vs in sorted(by_bone.items(), key=lambda x: -len(x[1]))[:6]:
    if len(vs) < 6: continue
    X = np.c_[rest[vs], np.ones(len(vs))]; Y = live[vs]
    T, res, rk, sv = np.linalg.lstsq(X, Y, rcond=None)
    err = np.abs(X @ T - Y).max()
    print(f"bone {b} ({mds.objects[b].name}), {len(vs)} verts, fit error {err:.5f}\n rotation rows {np.round(T[:3],3).tolist()}\n translation {np.round(T[3],3).tolist()}")
