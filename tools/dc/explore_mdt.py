"""Exploration: print the blocks of a mesh (MDT) of an MDS, with samples, to learn what each block holds.

Usage: python -I tools/dc/explore_mdt.py <pack> <mds name> <object name>
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from pack_list import entries  # noqa: E402


def main(pack, mds, objname):
    d = Path(pack).read_bytes()
    m = next(d[o:o + s] for n, o, s in entries(d) if n == mds)
    _, _, nobj, objoff, _, rec = struct.unpack_from("<6I", m, 0)
    for i in range(nobj):
        r = objoff + i * rec
        nm = m[r + 8:r + 0x28].split(b"\0")[0].decode("latin1")
        if nm == objname:
            mesh, = struct.unpack_from("<I", m, r + 0x28)
            break
    h = struct.unpack_from("<16I", m, mesh)
    print(f"{objname}: mesh@{mesh:#x} header:", " ".join(f"{x:x}" for x in h))
    mt = m[mesh:mesh + h[2]]
    f4 = lambda off, n: [tuple(round(x, 3) for x in struct.unpack_from("<4f", mt, off + 16 * k)) for k in range(n)]
    print(" positions[0:4]:", f4(h[4], 4))
    print(" block+0x18 [0:4]:", f4(h[6], 4))
    print(" block+0x24 [0:6]:", f4(h[9], 6))
    print(" uv block+0x30 [0:4]:", f4(h[12], 4))
    # sizes implied by offsets, sorted
    offs = sorted({h[4]: "pos", h[6]: "+0x18", h[9]: "+0x24", h[10]: "groups", h[12]: "+0x30(uv)", h[14]: "materials", h[2]: "end"}.items())
    for (a, na), (b, nb) in zip(offs, offs[1:]):
        print(f"  {na:10} {a:#7x}..{b:#7x} = {b - a:6} bytes = {(b - a) / 16:.1f} x16")
    # groups
    g = h[10]
    gh = struct.unpack_from("<4I", mt, g)
    print(" groups header:", [hex(x) for x in gh])
    ngroups = gh[2]
    stride = 4 if h[8] not in (0xFFFFFFFF, 0) and struct.unpack_from("<i", mt, 0x20)[0] > 0 else 3
    pos = g + 0x10
    for k in range(ngroups):
        typ, cnt, mat = struct.unpack_from("<3i", mt, pos)
        pos += 12
        tup = [struct.unpack_from(f"<{stride}i", mt, pos + 4 * stride * j) for j in range(min(cnt, 4))]
        print(f"  group {k}: type={typ} tuples={cnt} material={mat} first={tup}")
        pos += 4 * stride * cnt
    print(" groups end at", hex(pos), "(block +0x24 starts at", hex(h[9]), ")")
    mats = h[14]
    for k in range(h[13]):
        mr = mt[mats + 0x60 * k: mats + 0x60 * (k + 1)]
        print(f"  material {k}: texture={mr[0x34:0x54].split(chr(0).encode())[0].decode('latin1')!r} head={struct.unpack_from('<12f', mr, 0)[:6]}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
