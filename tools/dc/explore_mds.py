"""Exploration helper: print the object table and mesh headers of an MDS found inside a pack.

Usage: python -I tools/dc/explore_mds.py data/dun/mainchara/c01d.chr c01d.mds
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "scripts"))
from pack_list import entries  # noqa: E402


def cstr(b):
    return b.split(b"\0")[0].decode("latin1")


def main(pack, name):
    d = Path(pack).read_bytes()
    off, size = next((o, s) for n, o, s in entries(d) if n == name)
    m = d[off:off + size]
    magic, ver, nobj, objoff, _, recsz = struct.unpack_from("<6I", m, 0)
    print(f"{name}: {size} bytes, magic={m[:4]!r} version={ver} objects={nobj} table@0x{objoff:X} record={recsz:#x}")
    for i in range(nobj):
        r = objoff + i * recsz
        words = struct.unpack_from("<12I", m, r)
        nm = cstr(m[r + 8:r + 0x28])
        mesh, parent = struct.unpack_from("<Ii", m, r + 0x28)
        pos = struct.unpack_from("<3f", m, r + 0x30 + 0x30)   # translation row of the 4x4 matrix (guess)
        print(f"  [{i:2}] {nm:16} mesh@{mesh:#8x} parent={parent:3} w0..1={words[0]:#x},{words[1]:#x} pos~{tuple(round(x, 2) for x in pos)}")
        if mesh and "-v" in sys.argv:
            hdr = struct.unpack_from("<16I", m, mesh)
            print("       MDT header:", " ".join(f"{x:x}" for x in hdr))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
