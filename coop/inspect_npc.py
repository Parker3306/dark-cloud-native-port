"""Dump the four NPC slots' state and draw lists (live), optionally after spawning a puppet on one slot.

Usage: python -I coop/inspect_npc.py [spawn_slot]
"""
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import memdiff  # noqa: E402  (reuses spawn logic)
import test_puppet as tp  # noqa: E402
from hooks import Cave  # noqa: E402
from pine import Pine  # noqa: E402


def dump(p, dng, title):
    print(f"--- {title}")
    for s in range(4):
        base = dng + s * tp.SLOT_STRIDE
        act, en, hid = p.read_many([(base + 0xCFC4, 4), (base + 0xCFC8, 4), (base + 0xCFCC, 4)])
        cnt = p.read(base + 0xD110)
        ents = [struct.unpack("<3f", p.read_bytes(base + 0xCFD0 + i * 0x10, 12)) for i in range(min(cnt, 6))]
        print(f"  slot {s}: active={act} enable={en} cfcc={hid:#x} count={cnt} entries="
              + " ".join("(%.0f,%.0f,%.0f)" % e for e in ents))


def main(spawn_slot=None):
    with Pine() as p:
        p.load_state(1)
        time.sleep(3)
        dng = p.read(tp.NOW_DNG_MAP)
        dump(p, dng, "state as saved")
        if spawn_slot is not None:
            cave = Cave(p)
            cave.install()
            memdiff.spawn(p, cave)           # uses the first free slot among 3,2,1
            time.sleep(1.0)
            dump(p, dng, "1 s after SetNPC")
            time.sleep(2.0)
            dump(p, dng, "3 s after SetNPC")


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else None)
