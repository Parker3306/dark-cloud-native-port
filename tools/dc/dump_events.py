"""Dumps the dungeon's event structures (events, chests, atra balls, trap circles, event slots) from the running game (PINE) for comparison with dcevents.
usage: dump_events.py"""
import struct
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine

MAP = 0x01DC4BE0


def main():
    with Pine(28011) as p:
        ev = p.read_bytes(MAP + 0x8d50, 0x50 * 0x30 + 8)
        print('events')
        for i in range(0x30):
            b = ev[8 + i * 0x50 - 8 + 8: 8 + i * 0x50 + 0x50]
        # entry i starts at 0x8d50 + 0x50 * i
        for i in range(0x30):
            e = p.read_bytes(MAP + 0x8d50 + 0x50 * i, 0x50)
            ix, iz, typ, state = struct.unpack('<iiii', e[0:16])
            pos = struct.unpack('<4f', e[0x10:0x20])
            rad = struct.unpack('<f', e[0x24:0x28])[0]
            idx = struct.unpack('<i', e[0x28:0x2c])[0]
            if typ != -1:
                print('event', i, 'type', typ, 'idx', idx, 'pos %.4f %.4f %.4f' % pos[:3], 'r %.1f' % rad, 'state', state, 'ix,iz', ix, iz)
        n = struct.unpack('<i', p.read_bytes(MAP + 0xbc60, 4))[0]
        for i in range(0x18):
            c = p.read_bytes(MAP + 0xb660 + 0x40 * i, 0x40)
            act = struct.unpack('<i', c[0:4])[0]
            if act:
                pos = struct.unpack('<4f', c[0x10:0x20])
                item, one, small, st, q = struct.unpack('<5i', c[0x20:0x34])
                print('chest', i, 'item', item, 'small', small, 'quality', q, 'state', st, 'pos %.4f %.4f %.4f' % pos[:3])
        print('chest_count', n)
        na = struct.unpack('<i', p.read_bytes(MAP + 0xbd80, 4))[0]
        for i in range(na):
            a = p.read_bytes(MAP + 0xbc80 + 0x20 * i, 0x20)
            pos = struct.unpack('<4f', a[0:16])
            aid, act, st = struct.unpack('<3i', a[16:28])
            print('atra', i, 'id', aid, 'active', act, 'pos %.4f %.4f %.4f' % pos[:3])
        for i in range(3):
            t = p.read_bytes(MAP + 0x10ab0 + 0x20 * i, 0x20)
            pos = struct.unpack('<4f', t[0:16])
            act, kind, tm = struct.unpack('<iif', t[16:28])
            if act:
                print('trap', i, 'kind', kind, 'state', act, 'pos %.4f %.4f %.4f' % pos[:3])
        print('rand?')


if __name__ == '__main__':
    main()
