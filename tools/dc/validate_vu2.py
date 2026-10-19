"""Checks the rounding model of the VU0 helpers the collision code uses (sceVu0InnerProduct, OuterProduct, SubVector, AddVector, ScaleVector) on random vectors in the live game:
every add/subtract/multiply truncates (round toward zero); inner product = (x*x' + y*y') + z*z'; outer product = RZ(RZ(a.y*b.z) - RZ(b.y*a.z)) per lane.
usage: validate_vu2.py [count]"""
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

OUTER, INNER, ADD, SUB, SCALE = 0x00121600, 0x00121620, 0x001217A0, 0x001217B8, 0x001217E8
IN, OUT = 0x01F08000, 0x01F30000
f32 = np.float32


def rz(v):
    v = float(v)
    r = f32(v)
    if abs(float(r)) > abs(v):
        r = np.nextafter(r, f32(0))
    return r


def mul(a, b):
    return rz(float(a) * float(b))


def add(a, b):
    return rz(float(a) + float(b))


def sub(a, b):
    return rz(float(a) - float(b))


def model_outer(a, b):
    return [sub(mul(a[1], b[2]), mul(b[1], a[2])), sub(mul(a[2], b[0]), mul(b[2], a[0])), sub(mul(a[0], b[1]), mul(b[0], a[1]))]


def model_inner(a, b):
    return add(add(mul(a[0], b[0]), mul(a[1], b[1])), mul(a[2], b[2]))


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    rnd = random.Random(8)
    vecs = []
    for _ in range(count):
        scale = rnd.choice([1.0, 30.0, 300.0, 2000.0, 0.05])
        a = [f32(rnd.uniform(-scale, scale)) for _ in range(3)]
        b = [f32(rnd.uniform(-scale, scale)) for _ in range(3)]
        vecs.append((a, b))
    with Pine() as p:
        p.load_state(1)
        time.sleep(6)
        cave = Cave(p)
        cave.uninstall()
        time.sleep(0.3)
        cave.install()
        time.sleep(1)
        blob = b''.join(struct.pack('<8f', a[0], a[1], a[2], 1.0, b[0], b[1], b[2], 1.0) for a, b in vecs)
        for off in range(0, len(blob), 0x400):
            p.write_bytes(IN + off, blob[off:off + 0x400])
        res = {}
        for name, fn in (('outer', OUTER), ('sub', SUB), ('add', ADD)):
            for i in range(0, count, 16):
                cave.call_batch([dict(func=fn, a=(OUT + (i + k) * 16, IN + (i + k) * 32, IN + (i + k) * 32 + 16)) for k in range(min(16, count - i))], timeout=10.0)
            raw = b''.join(p.read_bytes(OUT + off, min(0x400, count * 16 - off)) for off in range(0, count * 16, 0x400))
            res[name] = struct.unpack('<%df' % (count * 4), raw)
        inner = []
        for i in range(0, count, 16):
            inner.extend(cave.call_batch([dict(func=INNER, a=(IN + (i + k) * 32, IN + (i + k) * 32 + 16)) for k in range(min(16, count - i))], timeout=10.0))
    # (sceVu0ScaleVector takes the scalar in f12, which the call mailbox cannot set; it is a plain truncating multiply)
    bad = dict(outer=0, sub=0, add=0, inner=0)
    for i, (a, b) in enumerate(vecs):
        o = model_outer(a, b)
        if any(float(x) != float(res['outer'][4 * i + k]) for k, x in enumerate(o)):
            bad['outer'] += 1
        if any(float(sub(a[k], b[k])) != float(res['sub'][4 * i + k]) for k in range(3)):
            bad['sub'] += 1
        if any(float(add(a[k], b[k])) != float(res['add'][4 * i + k]) for k in range(3)):
            bad['add'] += 1
        g = struct.unpack('<f', struct.pack('<I', inner[i] & 0xFFFFFFFF))[0]
        if float(model_inner(a, b)) != float(g):
            bad['inner'] += 1
    print('count', count, 'mismatches', bad)


if __name__ == '__main__':
    main()
