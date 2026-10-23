"""Where does the game put the lock-on cursor of a monster that has no _STATUS_SET_LOCKON_TRG frame? setTargetCursor (0x01dc07a0) asks the monster's CCharacter through virtual function
+0xa0 (`(**(vtable + 0xa0))(chara, out)`) and adds 8 to y. This rig calls that very function for every living monster of the live floor and prints it next to the position the
port uses (the position the monster was put at, C + 0x10).
usage: probe_lockpos.py   (PCSX2 in a dungeon, tools/dc/reset_game.sh)"""
import os, struct, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave, PHASES

NOW_MONSTOR = 0x002A34D0
OUT = 0x01F7C000


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]


def main():
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        gate = PHASES['update']['mailbox'] + 8
        p.write(gate + 4, 0)
        p.write(gate, 1)
        time.sleep(0.5)
        unit = p.read(NOW_MONSTOR)
        for k in range(16):
            U = unit + 0x1e3d0 + k * 400
            C = unit + 0x1fcd0 + k * 0x3510
            if s32(p.read(U)) == -1:
                continue
            vt = p.read(C + 0xa0)
            fn = p.read(vt + 0xa0)
            p.write_bytes(OUT, bytes(16))
            cave.call_batch([dict(func=fn, a=(C, OUT))], timeout=20.0)
            got = struct.unpack('<3f', p.read_bytes(OUT, 12))
            pos = struct.unpack('<3f', p.read_bytes(C + 0x10, 12))
            print('slot %2d hp %3d  object pos (%.1f %.1f %.1f)  GetPos (%.1f %.1f %.1f)  diff y %.1f  lock frame set: %s' % (
                k, s32(p.read(U + 0x24)), pos[0], pos[1], pos[2], got[0], got[1], got[2], got[1] - pos[1], 'yes' if p.read(U + 0xfc) else 'no'))
        p.write(gate, 0)


main()
