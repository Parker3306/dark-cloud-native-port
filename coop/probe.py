"""Quick status of the running game: which overlay is loaded and the key dungeon globals.

Usage: python -I coop/probe.py [--load-state N] [--slot PINE_SLOT]
"""
import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pine import Pine  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OVERLAY_BASE = 0x01DABD00

# main-segment globals (notes/symbols.csv)
G = {
    "BtGameModeFlag": 0x002A355C, "UserStatus*": 0x002A3468, "NowDngMap*": 0x002A34B8,
    "NowMonstorUnit*": 0x002A34D0, "CharaFrame*": 0x002A3500, "selectMapNo": 0x002A3594,
    "BtUraDongeon": 0x002A34B4, "NowWeapon": 0x002A34F0, "existFlag": 0x002A35F0,
    "GameMode(1F50)": 0x002A1F50,
}


def overlay_loaded(p):
    """Compare the live overlay area with DUN.BIN and TITLE.BIN."""
    live = p.read_bytes(OVERLAY_BASE + 0x80, 256)
    for name in ("DUN.BIN", "TITLE.BIN"):
        data = (ROOT / "extracted" / name).read_bytes()
        if data[0x80:0x180] == live:
            return name
    return "unknown"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--load-state", type=int)
    ap.add_argument("--slot", type=int, default=28011)
    args = ap.parse_args()
    with Pine(args.slot) as p:
        print(p.title(), p.game_id(), p.crc(), "status", p.status())
        if args.load_state:
            p.load_state(args.load_state)
            time.sleep(3)
        print("overlay:", overlay_loaded(p))
        for name, addr in G.items():
            v = p.read(addr, 4)
            print(f"  {name:18} @0x{addr:08X} = 0x{v:08X} ({struct.unpack('<i', struct.pack('<I', v))[0]})")


if __name__ == "__main__":
    main()
