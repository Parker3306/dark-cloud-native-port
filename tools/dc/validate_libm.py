"""Compare the C++ copies of the game's sinf / cosf / atanf / atan2f (dccore/libm_ps2.cpp, `dclibm.exe`) with the real functions in the running game, bit for bit.
usage: validate_libm.py [count]
A tiny wrapper (loads f12/f13 from memory, calls the function in a2, stores f0) is written to free RAM and called through the hook mailbox."""
import os
import random
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave
from mips import assemble

EXE = os.path.join(HERE, '..', '..', 'native', 'build', 'core_release', 'dclibm.exe')
FUNCS = {'sin': 0x0011D8A0, 'cos': 0x0011D6B0, 'atan': 0x0011D408, 'atan2': 0x0011DD40}
WRAP, IN, OUT = 0x01F63400, 0x01F08000, 0x01F30000


def bits(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    rnd = random.Random(5)
    with Pine() as p:
        p.load_state(1)
        time.sleep(6)
        cave = Cave(p)
        cave.uninstall()
        time.sleep(0.3)
        cave.install()
        time.sleep(1)
        code = assemble('''
            addiu sp, sp, -0x20
            sd    ra, 0(sp)
            sd    a1, 8(sp)
            lwc1  f12, 0(a0)
            lwc1  f13, 4(a0)
            jalr  ra, a2
            nop
            ld    a1, 8(sp)
            swc1  f0, 0(a1)
            ld    ra, 0(sp)
            addiu sp, sp, 0x20
            jr    ra
            nop
        ''', WRAP)
        p.write_bytes(WRAP, code)
        for name, addr in FUNCS.items():
            inputs = []
            for i in range(count):
                if name in ('sin', 'cos'):
                    x = [rnd.uniform(-7, 7), rnd.uniform(-0.8, 0.8), rnd.uniform(-60, 60), rnd.uniform(-3.2, 3.2)][i % 4]
                    inputs.append((x, 0.0))
                elif name == 'atan':
                    inputs.append((rnd.choice([rnd.uniform(-3, 3), rnd.uniform(-100, 100), rnd.uniform(-0.5, 0.5)]), 0.0))
                else:
                    inputs.append((rnd.uniform(-60, 60), rnd.uniform(-60, 60)))
            inputs = [(struct.unpack('<f', struct.pack('<f', a))[0], struct.unpack('<f', struct.pack('<f', b))[0]) for a, b in inputs]
            blob = b''.join(struct.pack('<2f', a, b) for a, b in inputs)
            for off in range(0, len(blob), 0x400):
                p.write_bytes(IN + off, blob[off:off + 0x400])
            for i in range(0, count, 16):
                calls = [dict(func=WRAP, a=(IN + (i + k) * 8, OUT + (i + k) * 4, addr)) for k in range(min(16, count - i))]
                cave.call_batch(calls, timeout=10.0)
            res = b''.join(p.read_bytes(OUT + off, min(0x400, count * 4 - off)) for off in range(0, count * 4, 0x400))
            game = struct.unpack('<%dI' % count, res)
            stdin = '\n'.join('%08x %08x' % (bits(a), bits(b)) for a, b in inputs) + '\n'
            mine = subprocess.run([EXE, name], input=stdin, capture_output=True, text=True).stdout.split()
            bad = [(inputs[i], game[i], int(mine[i], 16)) for i in range(count) if game[i] != int(mine[i], 16)]
            import json
            json.dump([[a, b, g] for (a, b), g in zip(inputs, game)], open(os.path.join(HERE, '..', '..', 'work', 'libm_%s.json' % name), 'w'))
            print('%-6s %d of %d differ' % (name, len(bad), count))
            for (a, b), g, m in bad[:4]:
                print('    in %.9g %.9g: game %08x (%.9g) port %08x (%.9g)' % (a, b, g, struct.unpack('<f', struct.pack('<I', g))[0], m, struct.unpack('<f', struct.pack('<I', m))[0]))


if __name__ == '__main__':
    main()
