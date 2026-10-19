"""Compare the C++ SearchiDoPutArea (item/monster placement) with the game's own on the live floor, for random rectangles and rand() states.
usage: validate_put.py [count]"""
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave

ISO = os.path.join(HERE, '..', '..', 'iso', 'Dark Cloud (USA).iso')
EXE = os.path.join(HERE, '..', '..', 'native', 'build', 'core_release', 'dcput.exe')
SRAND, PUT = 0x001046E8, 0x001C03C0
NOW_DNG_MAP, SELECT_MAP, IMPURE = 0x002A34B8, 0x002A3594, 0x0024FDEC
OUT = 0x01F7C000


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    rnd = random.Random(99)
    bad = 0
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        dng = p.read(NOW_DNG_MAP)
        seed, rooms, mapno = p.read(dng + 0x468), p.read(dng + 0xb650), p.read(SELECT_MAP)
        n_rooms = rooms
        rects = [tuple(p.read(dng + 0xb550 + i * 16 + k * 4) for k in range(4)) for i in range(n_rooms)]
        print('floor seed %d rooms %d map %d' % (seed, rooms, mapno))
        for t in range(count):
            rx, ry, rw, rh = rects[rnd.randrange(len(rects))] if rnd.random() < 0.6 else (rnd.randrange(0, 15), rnd.randrange(0, 15), rnd.randrange(1, 6), rnd.randrange(1, 6))
            s0 = rnd.randrange(1, 2 ** 31)
            p.write_bytes(OUT, bytes(16))
            res = cave.call_batch([dict(func=SRAND, a=(s0,)), dict(func=PUT, a=(dng + 0x9c50, rx, ry, rw), t=(rh, OUT))], timeout=10.0)
            gn = struct.unpack('<i', struct.pack('<I', res[1]))[0]
            gx, gy, gz, gw = struct.unpack('<4f', p.read_bytes(OUT, 16))
            gstate = p.read(p.read(IMPURE) + 0x58)
            out = subprocess.run([EXE, ISO, str(seed), str(rooms), str(mapno), str(rx), str(ry), str(rw), str(rh), str(s0)], capture_output=True, text=True).stdout.split()
            mn, mx, my, mz, mstate = int(out[0]), float(out[1]), float(out[2]), float(out[3]), int(out[4])
            ok = mn == gn and abs(mx - gx) < 0.05 and abs(my - gy) < 0.05 and abs(mz - gz) < 0.05
            if not ok:
                bad += 1
                print('MISMATCH rect %s state %d: game n=%d (%.3f %.3f %.3f) rand %d; port n=%d (%.3f %.3f %.3f) rand %d' % ((rx, ry, rw, rh), s0, gn, gx, gy, gz, gstate, mn, mx, my, mz, mstate))
    print('mismatches %d of %d' % (bad, count))


main()
