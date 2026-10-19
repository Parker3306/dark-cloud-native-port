"""In-game check: load a dungeon save state on a (patched) disc and make the game re-read a character pack from disc.

Save states made on the ORIGINAL disc contain the game's in-memory file index (a name tree built at boot), so after loading one the
game still reads files from their old places. This tool fixes that for the files we changed: it asks the game's own SearchFile
@0x0013E980 for each file's record and overwrites size (+8), first sector (+0xC) and sector count (+0x10) with the values from
the patched ISO. (A fresh boot on the patched disc needs none of this.)
Then it calls the game's selectChrUnit(charIndex, force) @0x001D1030, the code behind the character switch, which does
LoadFile("dun/mainchara/<char>.chr") + LoadChara2, all on the game thread through the hook mailbox.

Usage: python -I tools/dc/ingame_reload.py <pine_slot> [character_index] [patched_iso]
"""
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "coop"))
sys.path.insert(0, str(Path(__file__).parent))
from hooks import SCRATCH, Cave  # noqa: E402
from pine import Pine  # noqa: E402
import disc as D  # noqa: E402

SELECT_CHR_UNIT = 0x001D1030
SEARCH_FILE = 0x0013E980
CHAR_PATHS = ["dun/mainchara/c01d.chr", "dun/mainchara/c04b.chr", "dun/mainchara/c06b.chr",
              "dun/mainchara/c05a.chr", "dun/mainchara/c10b.chr", "dun/mainchara/c18a.chr"]


def sync_index(p, cave, disc, path):
    """Make the game's in-memory record for `path` match the patched ISO. Returns the record's address."""
    # SCRATCH is only 0x100 bytes long: the hook stub starts right after it (0x01F61000). Never write past it.
    p.write_bytes(SCRATCH + 0x20, path.encode() + b"\0")
    rec = cave.call(SEARCH_FILE, a=[SCRATCH + 0x20])
    if rec == 0:
        raise RuntimeError(f"{path}: not found in the game's file index")
    _, off, size, sec, cnt = disc.entry(path.replace("/", "\\"))
    old = struct.unpack("<3I", p.read_bytes(rec + 8, 12))
    p.write_bytes(rec + 8, struct.pack("<3I", size, sec, cnt))
    print(f"  index record {rec:#010x} for {path}: size/sector/count {old} -> {(size, sec, cnt)}")
    return rec


def main(slot, chara=0, iso=None):
    disc = D.Disc(iso or ROOT / "iso" / "Dark Cloud (USA).patched.iso", readonly=True)
    with Pine(slot) as p:
        p.load_state(1)
        time.sleep(3)
        if chara != 0:                  # the other characters have no weapon yet at this point of the game: give them the default one or the loader reads a bad path
            us = p.read(0x002A3468)
            wid = p.read(0x0027E790 + 4 * chara) + 1
            p.write(us + chara + 0x4340, 0, 1)
            p.write(us + 0x450C + chara * 0xAA8, wid, 2)
            print(f"gave character {chara} weapon id {wid}")
        cave = Cave(p)
        cave.install()
        print("syncing the file index with the patched disc ...")
        sync_index(p, cave, disc, CHAR_PATHS[chara])
        print("hook installed; reloading character", chara, "from disc ...", flush=True)
        r = cave.call(SELECT_CHR_UNIT, a=[chara, 1], timeout=60)
        print("selectChrUnit returned", hex(r))
        time.sleep(1.5)
        cave.uninstall()
    disc.close()


if __name__ == "__main__":
    main(int(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) > 2 else 0, sys.argv[3] if len(sys.argv) > 3 else None)
