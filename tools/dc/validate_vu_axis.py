"""Check the sceVu0Normalize model on axis-aligned and two-component vectors (the monsters mostly move along those).   usage: validate_vu_axis.py [count]"""
import os, random, struct, sys, time
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave
NORMALIZE = 0x00121648
IN, OUT = 0x01F08000, 0x01F30000
f32 = np.float32

def rz(v):
    v = float(v)
    r = f32(v)
    if abs(float(r)) > abs(v):
        r = np.nextafter(r, f32(0))
    return r

def model(v):
    x, y, z = [f32(a) for a in v]
    s = rz(float(rz(float(x) * float(x))) + float(rz(float(y) * float(y))))
    s = rz(float(s) + float(rz(float(z) * float(z))))
    t = rz(float(s) ** 0.5)
    q = rz(1.0 / float(t)) if t != 0 else f32(0)
    return [rz(float(a) * float(q)) for a in (x, y, z)]

def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    rnd = random.Random(3)
    vecs = []
    for i in range(count):
        mag = f32(rnd.choice([rnd.uniform(0.01, 30), rnd.uniform(0.1, 3000), rnd.uniform(11.0, 12.0), rnd.uniform(11.0, 12.0)]))
        sg = rnd.choice([-1, 1])
        k = i % 4
        if k == 0: v = [sg * mag, 0, 0]
        elif k == 1: v = [0, sg * mag, 0]
        elif k == 2: v = [0, 0, sg * mag]
        else: v = [mag, 0, 0]
        vecs.append([f32(a) for a in v])
    with Pine() as p:
        p.load_state(1); time.sleep(6)
        cave = Cave(p); cave.uninstall(); time.sleep(0.3); cave.install(); time.sleep(1)
        blob = b''.join(struct.pack('<4f', v[0], v[1], v[2], 1.0) + b'\0' * 16 for v in vecs)
        for off in range(0, len(blob), 0x400):
            p.write_bytes(IN + off, blob[off:off + 0x400])
        for i in range(0, count, 16):
            cave.call_batch([dict(func=NORMALIZE, a=(OUT + (i + k) * 16, IN + (i + k) * 32)) for k in range(min(16, count - i))], timeout=10.0)
        res = b''.join(p.read_bytes(OUT + off, min(0x400, count * 16 - off)) for off in range(0, count * 16, 0x400))
    out = struct.unpack('<%df' % (count * 4), res)
    bad = 0
    for i, v in enumerate(vecs):
        g = out[4 * i:4 * i + 3]; m = model(v)
        if any(float(a) != float(b) for a, b in zip(g, m)):
            bad += 1
            if bad <= 8: print('in', [float(a) for a in v], 'game', g, 'model', [float(a) for a in m])
    print('differ', bad, 'of', count)

main()
