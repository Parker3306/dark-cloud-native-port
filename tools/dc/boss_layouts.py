"""Monster layout (BtEnemyLayoutList) of story / boss floors and the monster-table rows they refer to.
usage: boss_layouts.py [dungeon 0-6 ...]     (default: last floors of every dungeon)   or  boss_layouts.py all <dungeon>
Layout row = 9 slots of 12 bytes {kind, monster id (-1 = end), weight} at BtEnemyLayoutList[dungeon] + floor*0x70.  A monster row of the table at 0x27FB00 is 0x9c bytes:
+0x00 model stem (dun/monstor/<stem>.chr), +0x40 script stem (<stem>.stb), +0x78 kind, +0x68/+0x6a shots.  `_SET_MONSTOR_ID(k, ...)` in an event script selects the k-th loaded slot.
"""
import struct
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import elfdata

FLOORS = [15, 17, 18, 18, 15, 26, 100]
LAYOUT, BACK = 0x2917B0, 0x2917D0
MON, NMON = 0x27FB00, 26052 // 0x9c


def cstr(b):
    return b.split(b'\0')[0].decode('latin1')


def main():
    e = elfdata.Elf()
    args = sys.argv[1:]
    allf = bool(args and args[0] == 'all')
    if allf:
        args = args[1:]
    duns = [int(a) for a in args] or list(range(7))
    for d in duns:
        base = struct.unpack('<I', e.read(LAYOUT + 4 * d, 4))[0]
        floors = range(FLOORS[d]) if allf else [FLOORS[d] - 1] + ([FLOORS[d] - 2] if d == 5 else [])
        for f in floors:
            row = []
            for s in range(9):
                kind, mid, wt = struct.unpack('<3i', e.read(base + f * 0x70 + s * 12, 12))
                if mid == -1:
                    break
                m = e.read(MON + mid * 0x9c, 0x9c)
                row.append('%d:id%d %s/%s(kind %d)' % (s, mid, cstr(m[0:16]), cstr(m[0x40:0x50]), struct.unpack('<h', m[0x78:0x7a])[0]))
            print('dungeon %d floor %d: %s' % (d, f, ', '.join(row) or '-'))


if __name__ == '__main__':
    main()
