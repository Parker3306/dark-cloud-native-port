"""Put several rebuilt character packs on ONE fresh copy of the ISO. Usage: python -I tools/dc/patch_all.py [name=path ...]  (default: the current best packs)"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import disc as D  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "iso" / "Dark Cloud (USA).iso"
DST = ROOT / "iso" / "Dark Cloud (USA).patched.iso"
DEFAULT = {
    r"dun\mainchara\c01d.chr": "work/models/toan/fitB/c01d.new.chr",
    r"dun\mainchara\c04b.chr": "work/models/c04b/fitB/c04b.new.chr",
    r"dun\mainchara\c05a.chr": "work/models/c05a/fitB/c05a.new.chr",
    r"dun\mainchara\c10b.chr": "work/models/c10b/fitB/c10b.new.chr",
}


def main(packs):
    d = D.make_copy(SRC, DST)
    for name, path in packs.items():
        data = (ROOT / path).read_bytes()
        d.put(name, data)
        print(f"{name}: {len(data)} bytes")
    d.commit()
    print("verify:", d.verify() or "OK", "| filler left:", d.capacity_left())
    d.close()


if __name__ == "__main__":
    packs = dict(a.split("=", 1) for a in sys.argv[1:]) or DEFAULT
    main(packs)
