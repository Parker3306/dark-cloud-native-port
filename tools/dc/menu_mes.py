"""Dump a message bank (.mes / .bin of the menus, the _systeme.bin of meswin/) with the CORRECT entry count.

Header: short[0] = number of {id, offset} entries (NOT the number of shorts: the table has short[0] entries, so 2 + 2 * short[0] shorts),
short[1] = byte offset of the text block (unused by the lookup), then the entries; text of an entry starts at short index  short[0] + 1 + offset
(ClsMes::GetTextLineDataTop @0x14f4b0: `psVar1 + *psVar1 + psVar1[i*2+3] + 1`).  Messages end with 0xFF01.
(dccore/text.cpp and tools/dc/mesdump.py read only short[0] / 2 entries: they lose the second half of every bank.)

usage: menu_mes.py <file.mes|bin> [id ...]   or   menu_mes.py --pak <pack file under data/> <entry> [id ...]
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.join(HERE, '..', '..')

P = {0xfd1f: '[', 0xfd20: ']', 0xfd55: "'", 0xfd57: '"', 0xfd58: '!', 0xfd59: '?', 0xfd5a: '(', 0xfd5b: '*', 0xfd5d: '-', 0xfd5f: '&',
     0xfd60: '%', 0xfd61: '(', 0xfd62: ')', 0xfd6b: ':', 0xfd6c: ',', 0xfd6d: '.', 0xfd6e: '/', 0xff02: ' ', 0xff00: '\n', 0xff03: '<PAGE>'}


def glyph(c):
    if 0xfd21 <= c <= 0xfd3a:
        return chr(65 + c - 0xfd21)
    if 0xfd3b <= c <= 0xfd54:
        return chr(97 + c - 0xfd3b)
    if 0xfd6f <= c <= 0xfd78:
        return chr(48 + c - 0xfd6f)
    if 0xfafa <= c <= 0xfaff:
        return '<name%d>' % (c - 0xfafa)
    if c in P:
        return P[c]
    return '{%04x}' % c


def load_bytes(b, lang_text=False):
    n_all = len(b) // 2
    sh = struct.unpack('<%dH' % n_all, b[:n_all * 2])
    n = sh[0]
    base = n + 1
    out = {}
    for i in range(n):
        if 3 + 2 * i >= n_all:
            break
        ident, off = sh[2 + 2 * i], sh[3 + 2 * i]
        p = base + off
        s = []
        while p < n_all and sh[p] != 0xff01:
            s.append(glyph(sh[p]))
            p += 1
        out.setdefault(ident, ''.join(s))
    return out


def main():
    a = sys.argv[1:]
    if a and a[0] == '--pak':
        import pack
        path = a[1] if os.path.isabs(a[1]) else os.path.join(ROOT, 'data', a[1])
        e = pack.parse(open(path, 'rb').read()).get(a[2])
        m = load_bytes(e.data)
        a = a[3:]
    else:
        path = a[0] if os.path.isabs(a[0]) else (os.path.join(ROOT, 'data', a[0]) if not os.path.exists(a[0]) else a[0])
        m = load_bytes(open(path, 'rb').read())
        a = a[1:]
    ids = [int(x) for x in a] or sorted(m)
    for i in ids:
        print(i, repr(m.get(i)))


if __name__ == '__main__':
    main()
