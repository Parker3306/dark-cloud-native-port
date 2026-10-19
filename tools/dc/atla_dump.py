"""Dump the Atla (Georama) menu data: 6 villages x up to 24 boards (EditPartsData @0x2540D0, 0xbc bytes each, 0x125c per village) and the chip table
(EditElementData @0x25AF00, 100 x 12 bytes per village) with the texts of commenu/a_usa/emenu.pak:atrames.bin.
Board: int0 valid(>0), int3 = name message offset (msg = 1000 + 200*village + int3), int4 = ?, then 6 chip records of 0x1c bytes at +0x14:
{int chip index (<0x28 object chip, >=0x28 villager chip), int ?, ptr, ptr, ptr, ptr, int flag(>=0: villager must have talked)}.
Chip entry: int0 = cell in obtip/obperson (36x36 cells, 7 per row), int1 = message offset (msg = 1000 + 200*village + 0x28 (+0x28 for villagers) + int1), int2 = ?.
usage: atla_dump.py [village]"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mem, pack, menu_mes
ROOT = os.path.join(HERE, '..', '..')
msgs = menu_mes.load_bytes(pack.parse(open(os.path.join(ROOT, 'data', 'commenu', 'a_usa', 'emenu.pak'), 'rb').read()).get('atrames.bin').data)

def t(i):
    return msgs.get(i, '?%d' % i).split(chr(10))[0].strip(chr(34))

def chip(v, idx):
    e = mem.swords(0x25AF00 + v * 0x4b0 + (idx + 0x28) * 12, 3)
    base = 1000 + 200 * v + e[1] + (0x28 if idx < 0x28 else 0x50)
    return e, base

if __name__ == '__main__':
    vs = [int(sys.argv[1])] if len(sys.argv) > 1 else range(6)
    for v in vs:
        print('village', v)
        for p in range(24):
            w = mem.swords(0x2540D0 + v * 0x125c + p * 0xbc, 47)
            if w[0] <= 0:
                continue
            chips = []
            for k in range(6):
                c = w[5 + k * 7: 12 + k * 7]
                if c[0] < 0 or c[0] > 99:
                    continue
                e, m = chip(v, c[0])
                chips.append('%d(%s,flag %d)' % (c[0], t(m), c[6]))
            print('  board %2d  %-22s ints %s  chips %s' % (p, t(1000 + 200 * v + w[3]), w[:5], ', '.join(chips)))
