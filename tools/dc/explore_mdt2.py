"""Exploration: where do MDT group boundaries fall relative to the unexplained header word at +0x24, for all meshes?"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from pack_list import entries  # noqa: E402

pack = ROOT / sys.argv[1] if len(sys.argv) > 1 else ROOT / "data/dun/mainchara/c01d.chr"
d = pack.read_bytes()
for name, off, size in entries(d):
    if not name.endswith(".mds"):
        continue
    m = d[off:off + size]
    nobj, objoff = struct.unpack_from("<II", m, 8)
    for i in range(nobj):
        r = objoff + i * 0x70
        mesh, = struct.unpack_from("<I", m, r + 0x28)
        if not mesh:
            continue
        oname = m[r + 8:r + 0x28].split(b"\0")[0].decode("latin1")
        h = struct.unpack_from("<16I", m, mesh)
        mt = m[mesh:]
        stride = 4 if struct.unpack_from("<i", mt, 0x20)[0] > 0 else 3
        g = h[10]
        ngroups = struct.unpack_from("<I", mt, g + 8)[0]
        pos, starts = g + 0x10, []
        for k in range(ngroups):
            typ, cnt, mat = struct.unpack_from("<3i", mt, pos)
            starts.append((pos, typ, mat))
            pos += 12 + 4 * stride * cnt
        hit = [k for k, (s, _, _) in enumerate(starts) if s == h[9]]
        print(f"{name}:{oname:10} verts={h[3]:4} norms={h[5]:4} uvs={h[11]:4} mats={h[13]:2} groups={ngroups:3} stride={stride} "
              f"end={pos:#x} size={h[2]:#x} +0x24={h[9]:#x} -> group boundary #{hit[0] if hit else None} "
              f"(first mat after: {starts[hit[0]][2] if hit else '-'}) +0x1c={h[7]:#x} +0x34? w15={h[15]:#x}")
