"""Price table read by GetItemMoney (0x1e6ef0): 0x291A3C + id*4 = {short buy, short sell}."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mem, shop_dump
def price(i):
    return mem.shorts(0x291A3C + i * 4, 2)
if __name__ == '__main__':
    for i in range(0x51, 0x179):
        b, s = price(i)
        print('%03x %-22s buy %5d sell %5d' % (i, shop_dump.name(i), b, s))
