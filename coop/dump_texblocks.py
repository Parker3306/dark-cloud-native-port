"""Dump CTextureManager block records (0x3c bytes each, starting at manager+0x38) for chosen block ids.

Usage: python -I coop/dump_texblocks.py [state] [--spawn]    (--spawn runs SetNPC on a free slot first, no drawing)
"""
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from hooks import Cave  # noqa: E402
from pine import Pine  # noqa: E402

TM = 0x01C75870           # CTextureManager instance (the 0x1c75870 constant the game passes)
IDS = [0x0F, 0x10, 0x11, 0x1D, 0x26, 0x2A, 0x40, 0x41, 0x42, 0x43, 0x46]


def dump(p, title):
    print(f"--- {title}")
    mgr = struct.unpack("<8I", p.read_bytes(TM, 32))
    print("manager head words:", [hex(x) for x in mgr])
    tail = struct.unpack("<3I", p.read_bytes(TM + 0x4E38, 12))
    print("EE texture heap: base=0x%08X used=%d units cap=%d units" % tail)
    for i in IDS:
        rec = struct.unpack("<15I", p.read_bytes(TM + 0x38 + i * 0x3C, 0x3C))
        print(f"  block 0x{i:02X}: " + " ".join(f"{w:08X}" for w in rec))


def main(state=1, spawn=False):
    with Pine() as p:
        p.load_state(state)
        time.sleep(3)
        dump(p, "state as saved")
        if spawn:
            import test_puppet as tp
            cave = Cave(p)
            cave.install()
            dng = p.read(tp.NOW_DNG_MAP)
            slot = next(s for s in (3, 2, 1) if p.read(dng + s * tp.SLOT_STRIDE + 0xCFC4) == 0)
            pack = (tp.ROOT / "data" / "chara" / "c01d.chr").read_bytes()
            ptr = cave.call(tp.ALLOC, a=[tp.NPC_POOL, (len(pack) + 15) // 16])
            p.write_bytes(ptr, pack)
            p.write_bytes(tp.SCRATCH, struct.pack("<8f", 0, 0, 0, 1, 0, 0, 0, 0))
            cave.call(tp.SET_NPC, a=[dng, slot, ptr, 0], t=[tp.SCRATCH, tp.SCRATCH + 0x10, 1, 0],
                      stack9=tp.NPC_POOL, timeout=10)
            print(f"(SetNPC on slot {slot}, block 0x{0x40 + slot:02X})")
            dump(p, "after SetNPC")


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 1, "--spawn" in sys.argv)
