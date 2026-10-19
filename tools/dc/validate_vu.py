"""Measure sceVu0Normalize / DistVector of the running game on random vectors and save them (work/vu_normalize.json, work/vu_dist.json) for fitting a rounding model.
usage: validate_vu.py [count]"""
import json
import os
import random
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave

NORMALIZE, DIST = 0x00121648, 0x00123590
IN, OUT = 0x01F08000, 0x01F30000   # 16-byte aligned vec4 slots: input A at IN + 32*i, input B at IN + 32*i + 16; output at OUT + 16*i


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    rnd = random.Random(21)
    with Pine() as p:
        p.load_state(1)
        time.sleep(6)
        cave = Cave(p)
        cave.uninstall()
        time.sleep(0.3)
        cave.install()
        time.sleep(1)
        vecs = []
        for _ in range(count):
            scale = rnd.choice([1.0, 30.0, 300.0, 2000.0, 0.05])
            v = [struct.unpack('<f', struct.pack('<f', rnd.uniform(-scale, scale)))[0] for _ in range(3)]
            w = [struct.unpack('<f', struct.pack('<f', rnd.uniform(-scale, scale)))[0] for _ in range(3)]
            vecs.append((v, w))
        blob = b''.join(struct.pack('<8f', v[0], v[1], v[2], 1.0, w[0], w[1], w[2], 1.0) for v, w in vecs)
        for off in range(0, len(blob), 0x400):
            p.write_bytes(IN + off, blob[off:off + 0x400])
        for i in range(0, count, 16):
            cave.call_batch([dict(func=NORMALIZE, a=(OUT + (i + k) * 16, IN + (i + k) * 32)) for k in range(min(16, count - i))], timeout=10.0)
        res = b''.join(p.read_bytes(OUT + off, min(0x400, count * 16 - off)) for off in range(0, count * 16, 0x400))
        out = struct.unpack('<%df' % (count * 4), res)
        json.dump([[list(v), list(out[4 * i:4 * i + 4])] for i, (v, w) in enumerate(vecs)], open(os.path.join(HERE, '..', '..', 'work', 'vu_normalize.json'), 'w'))
        dres = []
        for i in range(0, count, 16):
            r = cave.call_batch([dict(func=DIST, a=(IN + (i + k) * 32, IN + (i + k) * 32 + 16)) for k in range(min(16, count - i))], timeout=10.0)
            dres.extend(r)
        json.dump([[vecs[i][0], vecs[i][1], struct.unpack('<f', struct.pack('<I', dres[i] & 0xFFFFFFFF))[0]] for i in range(count)], open(os.path.join(HERE, '..', '..', 'work', 'vu_dist.json'), 'w'))
    print('saved', count)


if __name__ == '__main__':
    main()
