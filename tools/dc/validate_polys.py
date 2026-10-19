"""Compare the collision polygons the C++ code gathers for a cell with what the game's own CFrame::PickUpNearPoly returns for the same cell of the live floor.
usage: validate_polys.py [stem]   (the live floor must be the one for seed/rooms in SEED/ROOMS below: read from the game)"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave

ISO = os.path.join(HERE, '..', '..', 'iso', 'Dark Cloud (USA).iso')
EXE = os.path.join(HERE, '..', '..', 'native', 'build', 'core_release', 'dccoll.exe')
NOW_DNG_MAP = 0x002A34B8
SET_ROTATION, SET_POSITION, PICKUP = None, None, 0x0012A390
OUTBUF, BOXBUF = 0x01F70000, 0x01F7F000


def f2i(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def sym(name):
    for line in open(os.path.join(HERE, '..', '..', 'notes', 'symbols.csv')):
        f = line.strip().split(',')
        if len(f) >= 5 and f[4] == name:
            return int(f[0], 16)
    raise KeyError(name)


def main():
    stem = 'd01main_a'
    cam = len(sys.argv) > 1 and sys.argv[1] == 'cam'     # compare the camera collision pieces (frame +0x494, rotation +0x498) instead
    set_rot = sym('SetRotation__6CFrameFfff')
    set_pos = sym('SetPosition__6CFrameFfff')
    bad = checked = 0
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        dng = p.read(NOW_DNG_MAP)
        seed = p.read(dng + 0x468)
        rooms = p.read(dng + 0xb650)
        mapno = p.read(0x002A3594)
        print('live map at %08X seed %d rooms %d map %d' % (dng, seed, rooms, mapno))
        for cy in range(20):
            for cx in range(20):
                part = struct.unpack('<i', struct.pack('<I', p.read(dng + 0x9c50 + (cx + cy * 20) * 16)))[0]
                if part < 0 or part >= 0x2c:
                    continue
                rot = p.read(dng + 0x9c54 + (cx + cy * 20) * 16)
                frame = p.read(dng + part * 0x1d0 + (0x494 if cam else 0x49c))
                prot = struct.unpack('<h', p.read_bytes(dng + part * 0x1d0 + (0x498 if cam else 0x4a0), 2))[0]
                if not frame:
                    continue
                i = rot + prot
                if i > 3:
                    i -= 3
                if i == 3:
                    i = -1
                ang = 3.1415927410125732 * i * -90.0 / 180.0
                p.write_bytes(BOXBUF, struct.pack('<8f', 1e6, 1e6, 1e6, 1.0, -1e6, -1e6, -1e6, 1.0))
                p.write_bytes(OUTBUF, bytes(0x5000))
                res = cave.call_batch([
                    dict(func=set_rot, a=(frame,), f=(0.0, ang, 0.0)),
                    dict(func=set_pos, a=(frame,), f=(cx * 160.0, 0.0, cy * 160.0)),
                    dict(func=PICKUP, a=(frame, OUTBUF, BOXBUF)),
                ], timeout=10.0)
                n = res[2]
                game = p.read_bytes(OUTBUF, n * 0x50)
                out = subprocess.run([EXE, ISO, stem, str(seed), str(rooms), str(mapno), str(cx * 160 + 1), str(cy * 160 + 1), '--dump-cam' if cam else '--dump'], capture_output=True, text=True).stdout.split(chr(10))
                mine = [struct.pack('<20I', *[int(w, 16) for w in l.split()[1:]]) for l in out if l.startswith('P ')]
                # compare as multisets of triangles: greedy nearest match of the sorted vertex lists, tolerance 0.05
                def tri(b):
                    v = struct.unpack('<20f', b)
                    return sorted([(v[0], v[1], v[2]), (v[4], v[5], v[6]), (v[8], v[9], v[10])])

                import itertools

                def close(t1, t2):
                    return any(all(abs(a - b) < 0.05 for p1, p2 in zip(t1, perm) for a, b in zip(p1, p2)) for perm in itertools.permutations(t2))

                gt = [tri(game[k * 0x50:(k + 1) * 0x50]) for k in range(n)]
                mt = [tri(m) for m in mine]
                checked += 1
                ok = len(gt) == len(mt)
                pairs = {}
                if ok:
                    free = list(range(len(mt)))
                    for gi, g in enumerate(gt):
                        for fi, mi in enumerate(free):
                            if close(g, mt[mi]):
                                pairs[gi] = mi
                                free.pop(fi)
                                break
                        else:
                            ok = False
                            break
                # normal direction check (same sign as the game's) on the matching polygons
                if ok and n:
                    for gi, mi in pairs.items():
                        gn = struct.unpack('<4f', game[gi * 0x50 + 0x30:gi * 0x50 + 0x40])
                        mn = struct.unpack('<4f', mine[mi][0x30:0x40])
                        gl = sum(c * c for c in gn[:3]) ** 0.5 or 1.0
                        ml = sum(c * c for c in mn[:3]) ** 0.5 or 1.0
                        if sum(a / gl * b / ml for a, b in zip(gn[:3], mn[:3])) < 0.99:
                            ok = False
                            print('   normal differs at cell (%d,%d): game %s mine %s' % (cx, cy, gn[:3], mn[:3]))
                            break
                        ga = struct.unpack('<4I', game[gi * 0x50 + 0x40:gi * 0x50 + 0x50])
                        ma = struct.unpack('<4I', mine[mi][0x40:0x50])
                        if ga[:2] != ma[:2]:
                            ok = False
                            print('   attribute differs at cell (%d,%d): game %s mine %s' % (cx, cy, [hex(x) for x in ga], [hex(x) for x in ma]))
                            break
                if not ok:
                    bad += 1
                    print('cell (%d,%d) part %d rot %d: game %d polys, mine %d polys' % (cx, cy, part, rot, n, len(mine)))
    print('cells checked %d, mismatching %d' % (checked, bad))


main()
