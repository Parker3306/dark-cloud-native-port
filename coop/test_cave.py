"""Live test of the code cave: load a dungeon save state, install the hook, run a harmless call.

Usage: python -I coop/test_cave.py [state_slot]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from hooks import Cave  # noqa: E402
from pine import Pine  # noqa: E402

MG_GET_VSYNC_COUNT = 0x0012C210  # MGGetVSyncCount__Fv, pure read of a counter


def main(slot=1):
    with Pine() as p:
        p.load_state(slot)
        time.sleep(3)
        cave = Cave(p)
        cave.install()
        print("hook installed:", cave.installed())
        h0 = cave.heartbeat()
        time.sleep(1.0)
        h1 = cave.heartbeat()
        print(f"heartbeat {h0} -> {h1} in ~1s (60 expected while the game runs)")
        v1 = cave.call(MG_GET_VSYNC_COUNT)
        time.sleep(0.5)
        v2 = cave.call(MG_GET_VSYNC_COUNT)
        print(f"MGGetVSyncCount via mailbox: {v1} then {v2} (should increase by ~30)")
        # position read to prove the game is still simulating normally
        import struct
        x, y, z = struct.unpack("<3f", p.read_bytes(0x01EA1D30, 12))
        print(f"player at ({x:.1f}, {y:.1f}, {z:.1f})")


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
