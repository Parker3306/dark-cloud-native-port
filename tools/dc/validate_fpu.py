"""Find out how the emulated EE FPU rounds: runs single mul.s / add.s / sub.s / div.s / sqrt.s instructions in the live game on random operands and compares the
results with round-to-nearest and round-toward-zero.   usage: validate_fpu.py [count]"""
import os
import random
import struct
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave

IN, OUT = 0x01F08000, 0x01F30000
OPS = {'add': 0x460d6000, 'sub': 0x460d6001, 'mul': 0x460d6002, 'div': 0x460d6003, 'sqrt': 0x460c0004}


def f32(v):
    return np.float32(v)


def rz(v):
    r = f32(v)
    if abs(float(r)) > abs(v):
        r = np.nextafter(r, f32(0))
    return r


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 1600
    rnd = random.Random(11)
    with Pine() as p:
        p.load_state(1)
        time.sleep(6)
        cave = Cave(p)
        cave.uninstall()
        time.sleep(0.3)
        cave.install()
        time.sleep(1)
        for opn, (name, word) in enumerate(OPS.items()):
            WRAP = 0x01F63500 + opn * 0x100   # a new address per instruction: the emulator caches compiled code
            # lwc1 f12,0(a0); lwc1 f13,4(a0); <op>; jr ra; swc1 f0,0(a1) (delay slot)
            code = struct.pack('<6I', 0xC48C0000, 0xC48D0004, word, 0x03E00008, 0xE4A00000, 0)
            p.write_bytes(WRAP, code)
            inputs = []
            for _ in range(count):
                a = f32(rnd.choice([rnd.uniform(-2000, 2000), rnd.uniform(-1, 1), rnd.uniform(-1e-3, 1e-3)]))
                b = f32(rnd.choice([rnd.uniform(-2000, 2000), rnd.uniform(-1, 1), rnd.uniform(-0.01, 0.01)]))
                if name == 'sqrt':
                    a = abs(a)
                inputs.append((a, b))
            blob = b''.join(struct.pack('<2f', float(a), float(b)) for a, b in inputs)
            for off in range(0, len(blob), 0x400):
                p.write_bytes(IN + off, blob[off:off + 0x400])
            for i in range(0, count, 16):
                cave.call_batch([dict(func=WRAP, a=(IN + (i + k) * 8, OUT + (i + k) * 4)) for k in range(min(16, count - i))], timeout=10.0)
            res = b''.join(p.read_bytes(OUT + off, min(0x400, count * 4 - off)) for off in range(0, count * 4, 0x400))
            game = struct.unpack('<%df' % count, res)
            import json
            json.dump([[float(a), float(b), float(g)] for (a, b), g in zip(inputs, game)], open(os.path.join(HERE, '..', '..', 'work', 'fpu_%s.json' % name), 'w'))
            n_rn = n_rz = n_both = 0
            odd = []
            for (a, b), g in zip(inputs, game):
                a, b = float(a), float(b)
                exact = {'add': a + b, 'sub': a - b, 'mul': a * b, 'div': (a / b if b else 0.0), 'sqrt': a ** 0.5}[name]
                rn, rzv = float(f32(exact)), float(rz(exact))
                g = float(g)
                n_rn += g == rn
                n_rz += g == rzv
                if g != rn and g != rzv:
                    odd.append((a, b, g, rn, rzv))
            print('%-4s of %d: equals round-nearest %d, round-toward-zero %d, neither %d' % (name, count, n_rn, n_rz, len(odd)))
            for a, b, g, rn, rzv in odd[:3]:
                print('     %.9g %.9g -> game %.9g nearest %.9g zero %.9g' % (a, b, g, rn, rzv))


if __name__ == '__main__':
    main()
