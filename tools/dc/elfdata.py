"""Read initialised data out of the game's main ELF (SCUS_971.11 on the disc) by virtual address.
usage: elfdata.py <hex addr> [count] [f|i|x]     (prints `count` 4-byte words as floats, ints or hex)"""
import os
import struct
import sys

ISO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'iso', 'Dark Cloud (USA).iso')


def load_elf(iso=ISO):
    with open(iso, 'rb') as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        root_lba = struct.unpack('<I', pvd[158:162])[0]
        root_len = struct.unpack('<I', pvd[166:170])[0]
        f.seek(root_lba * 2048)
        d = f.read(root_len)
        p = 0
        while p < len(d):
            n = d[p]
            if n == 0:
                p = (p // 2048 + 1) * 2048
                continue
            name = d[p + 33:p + 33 + d[p + 32]].decode('latin1')
            if name.startswith('SCUS'):
                lba = struct.unpack('<I', d[p + 2:p + 6])[0]
                size = struct.unpack('<I', d[p + 10:p + 14])[0]
                f.seek(lba * 2048)
                return f.read(size)
            p += n
    raise RuntimeError('SCUS file not found')


class Elf:
    def __init__(self, data=None):
        self.data = data or load_elf()
        off = struct.unpack('<I', self.data[28:32])[0]
        n = struct.unpack('<H', self.data[44:46])[0]
        self.segs = []
        for i in range(n):
            t, o, va, pa, fs, ms, fl, al = struct.unpack('<8I', self.data[off + i * 32:off + i * 32 + 32])
            if t == 1:
                self.segs.append((va, o, fs))

    def read(self, va, n):
        for v, o, fs in self.segs:
            if v <= va < v + fs:
                return self.data[o + va - v:o + va - v + n]
        raise KeyError(hex(va))

    def floats(self, va, n=1):
        return struct.unpack('<%df' % n, self.read(va, 4 * n))

    def ints(self, va, n=1):
        return struct.unpack('<%di' % n, self.read(va, 4 * n))


if __name__ == '__main__':
    e = Elf()
    addr = int(sys.argv[1], 0)
    cnt = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    mode = sys.argv[3] if len(sys.argv) > 3 else 'f'
    vals = e.floats(addr, cnt) if mode == 'f' else e.ints(addr, cnt) if mode == 'i' else [hex(x) for x in struct.unpack('<%dI' % cnt, e.read(addr, 4 * cnt))]
    for i, v in enumerate(vals):
        print(hex(addr + 4 * i), v)
