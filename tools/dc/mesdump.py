"""Dump a .mes message bank (same decode as dccore/text.cpp).  usage: mesdump.py <path relative to data/ or absolute> [id ...]"""
import os, struct, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'data')
P = {0xfd1f: '[', 0xfd20: ']', 0xfd55: "'", 0xfd57: '"', 0xfd58: '!', 0xfd59: '?', 0xfd5a: '(', 0xfd5b: '*', 0xfd5d: '-', 0xfd5f: '&',
     0xfd60: '%', 0xfd61: '(', 0xfd62: ')', 0xfd6b: ':', 0xfd6c: ',', 0xfd6d: '.', 0xfd6e: '/', 0xff02: ' ', 0xff00: '\n', 0xff03: chr(92) + 'p'}


def glyph(c):
    if 0xfd21 <= c <= 0xfd3a: return chr(65 + c - 0xfd21)
    if 0xfd3b <= c <= 0xfd54: return chr(97 + c - 0xfd3b)
    if 0xfd6f <= c <= 0xfd78: return chr(48 + c - 0xfd6f)
    if 0xfafa <= c <= 0xfaff: return '<name%d>' % (c - 0xfafa)
    if c in P: return P[c]
    return '{%04x}' % c


def load(path):
    if not os.path.isabs(path): path = os.path.join(ROOT, path)
    b = open(path, 'rb').read()
    n = len(b) // 2
    sh = struct.unpack('<%dH' % n, b[:n * 2])
    table = sh[0]
    out = {}
    for i in range(table // 2):
        ident, off = sh[2 + 2 * i], sh[3 + 2 * i]
        p = table + 1 + off
        s = []
        while p < n and sh[p] != 0xff01:
            s.append(glyph(sh[p])); p += 1
        out.setdefault(ident, ''.join(s))
    return out


if __name__ == '__main__':
    m = load(sys.argv[1])
    ids = [int(x) for x in sys.argv[2:]] or sorted(m)
    for i in ids:
        print(i, repr(m.get(i)))
