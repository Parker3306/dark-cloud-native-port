"""Stage object tables of the opening (static data in TITLE.BIN, copied to the stack by the Op?_InitProcess / MapLoad functions).

Each table is an array of 0x20-byte entries:
    +0x00 u32 pointer to the model path ("opdat/norn/e01h01_0.mds") or 0 = "reuse the previous model" (another instance of it)
    +0x04 u32 pointer to the shadow model path or 0
    +0x08 float x, +0x0C y, +0x10 z   (the game multiplies them by 10 = world position)
    +0x14 float rx, +0x18 ry, +0x1C rz (degrees; the game converts with pi/180)
Every entry creates one CMapObject (OP_NornMapObj / OP_NornMapObj2: 0xF0 bytes each) holding the model instance at that position.
Table addresses/counts were read from the instructions of the loaders (see notes/opening.md).

usage: opening_stage.py [B1|B2|C1|C2|C3|C4|D1] ...      (default: all)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TITLE_BIN = os.path.join(HERE, '..', '..', 'extracted', 'TITLE.BIN')
BASE = 0x01DABD00

TABLES = {
    # name: (address, count, loader, meaning)
    'B1': (0x01DD6B40, 0x44, 'OpB_InitProcess (scene 1, norn.cfg)', 'Norune night: ground, houses (+shadow), trees/fences/roads/torch posts -> OP_NornMapObj'),
    'B2': (0x01DD73C0, 0x17, 'OpB_InitProcess', 'second layer (t0006/t0001/t0003: water, grass, fences) -> OP_NornMapObj2; entry 0 also finds frame "sun3"'),
    'C1': (0x01DD7F30, 0x4B, 'MapLoad (scene 3, norn2.cfg)', 'festival Norune: buildings etc -> OP_NornMapObj'),
    'C2': (0x01DD8890, 0x57, 'MapLoad', 'festival Norune layer 2 (stage props, benches, instruments) -> OP_NornMapObj2'),
    'C3': (0x01DD9370, 0x4C, 'MapLoad2 (destruction, norn3.pak)', 'destroyed Norune -> OP_NornMapObj'),
    'C4': (0x01DD9CF0, 0x3E, 'MapLoad2', 'destroyed Norune layer 2 -> OP_NornMapObj2'),
    'D1': (0x01DDAF00, 0x0A, 'OpD_InitProcess2 (scene 9, seirei2.pak)', 'Spirit King scene 2 props'),
}


def cstr(d, a):
    o = a - BASE
    if not (0 <= o < len(d) - 1):
        return None
    e = d.find(b'\0', o, o + 96)
    s = d[o:e]
    return s.decode('latin1') if s and all(32 <= c < 127 for c in s) else None


def read_table(name, d=None):
    d = d or open(TITLE_BIN, 'rb').read()
    addr, count = TABLES[name][:2]
    out = []
    for i in range(count):
        o = addr - BASE + i * 32
        p, s = struct.unpack_from('<II', d, o)
        f = struct.unpack_from('<6f', d, o + 8)
        out.append(dict(index=i, model=cstr(d, p) if p else None, shadow=cstr(d, s) if s else None,
                        pos=tuple(x * 10 for x in f[:3]), rot_deg=tuple(f[3:])))
    return out


def main():
    names = sys.argv[1:] or list(TABLES)
    d = open(TITLE_BIN, 'rb').read()
    for n in names:
        addr, count, loader, meaning = TABLES[n]
        print(f'### {n} @0x{addr:08X} x{count}  {loader}: {meaning}')
        cur = None
        for e in read_table(n, d):
            if e['model']:
                cur = e['model'].split('/')[-1]
            print(f"  {e['index']:3d} {cur:16s} shadow={e['shadow'].split('/')[-1] if e['shadow'] else '-':14s} "
                  f"pos=({e['pos'][0]:8.1f},{e['pos'][1]:6.1f},{e['pos'][2]:8.1f}) rot=({e['rot_deg'][0]:g},{e['rot_deg'][1]:g},{e['rot_deg'][2]:g})")


if __name__ == '__main__':
    main()
