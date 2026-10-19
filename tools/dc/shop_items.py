"""Item table dump for shop research: id, name, ITEM_LIST flags(+4 int, +8 short), value(+0xa short), buy/sell price."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mem, shop_dump, shop_prices
def item_rec(i):
    row = mem.read(0x27DE50 + (i - 0x51) * 8, 8)
    if row[0] != 1: return None
    idx = int.from_bytes(row[2:4], 'little')
    return mem.read(0x27D0A0 + idx * 0x14, 0x14)
if __name__ == '__main__':
    for i in range(0x84, 0x100):
        r = item_rec(i)
        if r is None: continue
        f4 = int.from_bytes(r[4:8], 'little'); f8 = int.from_bytes(r[8:10], 'little'); v = int.from_bytes(r[10:12], 'little', signed=True)
        b, s = shop_prices.price(i)
        print('%02x %-20s f4=%08x f8=%04x val=%5d buy=%5d sell=%5d  %s' % (i, shop_dump.name(i), f4, f8, v, b, s, r.hex()))
