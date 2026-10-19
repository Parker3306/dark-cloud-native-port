"""Exploration: compare the 'skin' mesh of Toan with c01d.wgt / c01d.bbp / c01d.mot headers.

Usage: python -I tools/dc/explore_skin.py
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from pack_list import entries  # noqa: E402

pack = (ROOT / "data/dun/mainchara/c01d.chr").read_bytes()
files = {n: pack[o:o + s] for n, o, s in entries(pack)}


def hexrows(b, n=0x80, w=16):
    for i in range(0, min(n, len(b)), w):
        print(f"    {i:05X}: " + " ".join(f"{x:02X}" for x in b[i:i + w]))


mds = files["c01d.mds"]
mesh = 0x1BB50
hdr = struct.unpack_from("<16I", mds, mesh)
print("skin MDT header:", " ".join(f"{x:x}" for x in hdr))
npos, nnorm, nuv = hdr[5], None, hdr[11]
print(f" positions: {hdr[5]} (at +{hdr[4]:#x}), uv entries: {hdr[11]}, groups at +{hdr[10]:#x}, materials at +{hdr[14]:#x}")
for name in ("c01d.wgt", "c01d.bbp", "c01d.mot"):
    f = files[name]
    print(f"{name}: {len(f)} bytes")
    hexrows(f, 0x60)
print("c01d.wgt words:", [hex(x) for x in struct.unpack_from("<16I", files["c01d.wgt"], 0)])
print("c01d.bbp words:", [hex(x) for x in struct.unpack_from("<16I", files["c01d.bbp"], 0)])
print("c01d.mot words:", [hex(x) for x in struct.unpack_from("<16I", files["c01d.mot"], 0)])
