"""Compare the C++ chest item pick (EventTables::preset_item) with the game's PresetSmallItemNo_Get (0x001BFEF0) on the live dungeon: item id and the rand() state afterwards.
usage: validate_preset.py [count] [floor]    (the game must be in a dungeon in free movement; the floor of the call is a parameter, the map is the live one)"""
import os, random, struct, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave, PHASES

ROOT = os.path.join(HERE, '..', '..')
EXE = os.path.join(ROOT, 'native', 'build', 'core_release', 'dcpreset.exe')
ISO = os.path.join(ROOT, 'iso', 'Dark Cloud (USA).iso')
SRAND, PRESET = 0x001046E8, 0x001BFEF0
SELECT_MAP, IMPURE = 0x002A3594, 0x0024FDEC


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]


def main():
    args = [a for a in sys.argv[1:] if a.isdigit()]
    count = int(args[0]) if args else 40
    rnd = random.Random(7)
    bad = 0
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        gate = PHASES['update']['mailbox'] + 8
        p.write(gate + 4, 0)
        p.write(gate, 1)
        time.sleep(0.5)
        mapno = p.read(SELECT_MAP)
        floors = [int(args[1])] if len(args) > 1 else [0, 1, 2, 5, 9, 14, 20]
        for t in range(count):
            r = rnd.randrange(1, 2**31)
            floor = floors[t % len(floors)]
            table = t // 2 % 2
            small = t % 2
            cave.call_batch([dict(func=SRAND, a=(r,))])
            item = s32(cave.call_batch([dict(func=PRESET, a=(mapno, floor, table, small))])[0])
            rs = p.read(p.read(IMPURE) + 0x58)
            out = subprocess.run([EXE, ISO, str(mapno), str(floor), str(table), str(small), str(r)], capture_output=True, text=True).stdout.split()
            ok = out and int(out[1]) == item and int(out[3]) == rs
            bad += 0 if ok else 1
            print('%2d map %d floor %2d table %d small %d r=%d live item %d rand %d | mine %s %s %s' % (t, mapno, floor, table, small, r, item, rs, out[1] if out else '-', out[3] if out else '-', 'OK' if ok else 'DIFF'))
        p.write(gate, 0)
    print('mismatches:', bad, 'of', count)


main()
