"""Memory image of the game as seen by the decompile: main ELF (0x100000..) plus the dungeon overlay DUN.BIN at 0x1DABD00.
Usage: mem.py str <addr>... | mem.py hex <addr> <n> | mem.py w <addr> <n> (words) | mem.py s <addr> <n> (shorts) | mem.py f <addr> <n>
"""
import os
import struct
import sys

R = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import elfdata  # noqa: E402

_elf = elfdata.Elf(open(os.path.join(R, 'extracted', 'SCUS_971.11'), 'rb').read())
_dun = open(os.path.join(R, 'extracted', 'DUN.BIN'), 'rb').read()
DUN_BASE = 0x1DABD00


def read(a, n):
    if DUN_BASE <= a < DUN_BASE + len(_dun):
        return _dun[a - DUN_BASE:a - DUN_BASE + n]
    return _elf.read(a, n)


def cstr(a, mx=64):
    return read(a, mx).split(b'\0')[0].decode('latin1')


def words(a, n):
    return struct.unpack('<%dI' % n, read(a, 4 * n))


def swords(a, n):
    return struct.unpack('<%di' % n, read(a, 4 * n))


def shorts(a, n):
    return struct.unpack('<%dh' % n, read(a, 2 * n))


def floats(a, n):
    return struct.unpack('<%df' % n, read(a, 4 * n))


if __name__ == '__main__':
    m = sys.argv[1]
    if m == 'str':
        for x in sys.argv[2:]:
            print(x, repr(cstr(int(x, 0))))
    else:
        a, n = int(sys.argv[2], 0), int(sys.argv[3], 0)
        if m == 'hex':
            d = read(a, n)
            for i in range(0, n, 16):
                print('%08x ' % (a + i) + d[i:i + 16].hex(' '))
        elif m == 'w':
            for i, v in enumerate(swords(a, n)):
                print('%08x %d (%#x)' % (a + 4 * i, v, v & 0xffffffff))
        elif m == 's':
            print(' '.join(str(v) for v in shorts(a, n)))
        elif m == 'f':
            for i, v in enumerate(floats(a, n)):
                print('%08x %g' % (a + 4 * i, v))
