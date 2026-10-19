"""Find what the circling puppet changes: snapshot RAM when healthy and after the scene degrades, in a circling
run and in a control run (spawned, but no per-frame calls); report 4 KB pages that changed only in the circling run.

Usage: python -I coop/memdiff.py
"""
import math
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_puppet as tp  # noqa: E402
from hooks import Cave, f32  # noqa: E402
from pine import Pine  # noqa: E402

LO, HI = 0x002A2380, 0x01F06B00     # game data + overlay + heap (not the stack, not our cave)
PAGE = 0x1000


def snapshot(p):
    return p.read_bytes(LO, HI - LO)


def spawn(p, cave):
    dng = p.read(tp.NOW_DNG_MAP)
    slot = next(s for s in (3, 2, 1) if p.read(dng + s * tp.SLOT_STRIDE + 0xCFC4) == 0)
    pack = (tp.ROOT / "data" / "dun" / "mainchara" / "c01d.chr").read_bytes()
    ptr = p.read(tp.READ_BUFFER_PTR)
    p.write_bytes(ptr, pack)
    p.write_bytes(tp.SCRATCH, struct.pack("<8f", 0, 0, 0, 1, 0, 0, 0, 0))
    old = p.read_bytes(tp.NPC_CFG_NAME, 9)
    p.write_bytes(tp.NPC_CFG_NAME, b"base.cfg\0")
    try:
        cave.call(tp.SET_NPC, a=[dng, slot, ptr, 0], t=[tp.SCRATCH, tp.SCRATCH + 0x10, 1, 0], stack9=tp.POOL, timeout=10)
    finally:
        p.write_bytes(tp.NPC_CFG_NAME, old)
    return dng, slot


def run(p, circle, seconds=22.0):
    p.load_state(1)
    time.sleep(3)
    cave = Cave(p)
    cave.install()
    dng, slot = spawn(p, cave)
    px, py, pz = struct.unpack("<3f", p.read_bytes(tp.PLAYER + 0x10, 12))
    t0, early = time.time(), None
    while time.time() - t0 < seconds:
        if circle:
            ang = (time.time() - t0) * 1.5
            cave.call_batch([dict(func=tp.CLEAR_NPC, a=[dng]),
                             dict(func=tp.RESERV_DRAW, a=[dng, slot, 0],
                                  f=[px + 70 * math.cos(ang), py, pz + 70 * math.sin(ang)])], phase="draw")
        else:
            time.sleep(0.016)
        if early is None and time.time() - t0 > 2.0:
            early = snapshot(p)            # healthy
    late = snapshot(p)                     # degraded (circle) / still healthy (control)
    return early, late


def changed_pages(a, b):
    return {i for i in range(0, len(a), PAGE) if a[i:i + PAGE] != b[i:i + PAGE]}


def main():
    with Pine(timeout=30) as p:
        ea, la = run(p, circle=True)
        ca = changed_pages(ea, la)
        print(f"circling run: {len(ca)} pages changed between t=2s and t=22s")
        eb, lb = run(p, circle=False)
        cb = changed_pages(eb, lb)
        print(f"control run : {len(cb)} pages changed")
        only = sorted(ca - cb)
        print(f"{len(only)} pages changed only in the circling run:")
        for off in only[:80]:
            a = LO + off
            # which 4-byte words differ, and how
            diffs = [(a + j, struct.unpack_from('<I', ea, off + j)[0], struct.unpack_from('<I', la, off + j)[0])
                     for j in range(0, PAGE, 4)
                     if ea[off + j:off + j + 4] != la[off + j:off + j + 4]]
            print(f"  0x{a:08X}: {len(diffs):4d} words, e.g. " +
                  ", ".join(f"{x:08X}:{o:08X}->{n:08X}" for x, o, n in diffs[:3]))


if __name__ == "__main__":
    main()
