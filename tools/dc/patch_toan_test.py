"""Stage 0 gate test: put a visibly changed Toan (hat x1.8) plus an extra 8 KB pack entry (forces the append-to-DATA.DAT path)
onto a fresh copy of the ISO.

Usage: python -I tools/dc/patch_toan_test.py [--identity]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import disc as D  # noqa: E402
import mds as M  # noqa: E402
import pack as P  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "iso" / "Dark Cloud (USA).iso"
DST = ROOT / "iso" / "Dark Cloud (USA).patched.iso"
NAME = "dun\\mainchara\\c01d.chr"


def main():
    d = D.make_copy(SRC, DST)
    pack = P.parse(d.read(NAME))
    if "--identity" not in sys.argv:
        mds = M.parse(pack.get("c01d.mds").data)
        hat = next(o for o in mds.objects if o.name == "boushi")
        hat.mesh.positions = [(x * 1.8, y * 1.8, z * 1.8, w) for x, y, z, w in hat.mesh.positions]
        pack.replace("c01d.mds", M.dump(mds))
    pack.entries.append(P.Entry("dc_padding.bin", bytes(8192), 0))   # forces the file past its old slot
    new = P.dump(pack)
    before = d.dat_size
    off = d.put(NAME, new)
    d.commit()
    print(f"wrote {len(new)} bytes at DATA.DAT offset {off:#x} (DATA.DAT {before} -> {d.dat_size}); verify:", d.verify() or "OK")
    print("read back equal:", d.read(NAME) == new)
    d.close()


if __name__ == "__main__":
    main()
