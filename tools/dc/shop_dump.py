"""Dump the item shop table ItemShopList2 (0x292020, 18 shops x 20 item ids) with names from meswin/_systeme.bin.
Name message = ComItemInfo[id-0x51].short@+6 + 100."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mem, menu_mes
ROOT = os.path.join(HERE, '..', '..')
names = menu_mes.load_bytes(open(os.path.join(ROOT, 'data', 'meswin', '_systeme.bin'), 'rb').read())

def msgno(i):
    """Name message of item/weapon/attach id > 0x50: ComItemInfo row (id-0x51) short at +6, plus 100 (GetWeaponMsgNo @0x22a7e0).
    For ids < 0x101 this equals id+100; for weapons it is a separate sequence (NOT id-156)."""
    row = mem.read(0x27DE50 + (i - 0x51) * 8, 8)
    return int.from_bytes(row[6:8], 'little') + 100

def name(i):
    if i <= 0x50:
        return '#%d' % i
    m = msgno(i)
    return names.get(m, '?%d' % m)

if __name__ == '__main__':
    for s in range(18):
        ids = mem.shorts(0x292020 + s * 0x28, 20)
        print('shop %d:' % s, ', '.join('%02x %s' % (i, name(i)) for i in ids if i > 0))
