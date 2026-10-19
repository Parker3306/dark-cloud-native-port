"""Put a rebuilt character pack on a fresh COPY of the ISO (iso/Dark Cloud (USA).patched.iso). Usage: python -I tools/dc/patch_character.py <new .chr> [pack name in the disc]"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import disc as D  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "iso" / "Dark Cloud (USA).iso"
DST = ROOT / "iso" / "Dark Cloud (USA).patched.iso"


def main(chr_path, name=r"dun\mainchara\c01d.chr"):
    d = D.make_copy(SRC, DST)
    data = Path(chr_path).read_bytes()
    before = d.dat_size
    off = d.put(name, data)
    d.commit()
    print(f"wrote {len(data)} bytes at DATA.DAT offset {off:#x} (DATA.DAT {before} -> {d.dat_size}); verify:", d.verify() or "OK")
    print("read back equal:", d.read(name) == data, "| space left in the filler:", d.capacity_left())
    d.close()


if __name__ == "__main__":
    main(*sys.argv[1:3])
