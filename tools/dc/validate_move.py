"""Run random MoveCheck cases through the game's own MoveCheck (PCSX2, dungeon loaded, hooks installable) and compare with the C++ port.
usage: validate_move.py [cases] [rng seed] [seed rooms map stem]
The polygons of every case are uploaded to free RAM and fed to both implementations, so this checks the movement algorithm (MoveCheck, CheckHit,
CheckHits, GetFootPoly, CheckWidth) independently of how the polygons were gathered."""
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave

ISO = os.path.join(HERE, '..', '..', 'iso', 'Dark Cloud (USA).iso')
EXE = os.path.join(HERE, '..', '..', 'native', 'build', 'core_release', 'dcmove.exe')
MOVE_CHECK, CHECK_HIT = 0x0014A680, 0x00149D50
TOL = 0.0 if os.environ.get('DC_EXACT') else 2e-3   # DC_EXACT=1: bit-for-bit comparison
POLYS, POS, VEL, OUT, INFO = 0x01F70000, 0x01F7A000, 0x01F7A010, 0x01F7A020, 0x01F7A100


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def cases(count, rng, seed, rooms, mapno, stem):
    out = subprocess.run([EXE, ISO, stem, str(seed), str(rooms), str(mapno), str(count), str(rng)], capture_output=True, text=True).stdout.split(chr(10))
    i = 0
    while i < len(out):
        if out[i].startswith('C '):
            f = out[i].split()
            n = int(f[1])
            c = dict(n=n, pos=list(map(float, f[2:5])), vel=list(map(float, f[5:8])), mask=int(f[8]))
            c['polys'] = b''.join(struct.pack('<20I', *[int(x, 16) for x in out[i + 1 + k].split()[1:]]) for k in range(n))
            r = out[i + 1 + n].split()
            c['exp'] = dict(out=list(map(float, r[1:4])), vel=list(map(float, r[4:7])), blocked=int(r[7]), grounded=int(r[8]), foot=int(r[9]), hit=list(map(float, r[10:13])))
            c['segs'] = []
            i += n + 2
            while i < len(out) and out[i].startswith('S '):
                f = out[i].split()
                c['segs'].append(dict(a=list(map(float, f[1:4])), b=list(map(float, f[4:7])), mask=int(f[7]), i0=int(f[8]), h0=list(map(float, f[9:12])), i1=int(f[12]), h1=list(map(float, f[13:16]))))
                i += 1
            yield c
        else:
            i += 1


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 50
    rng = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    seed, rooms, mapno, stem = (int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]) if len(sys.argv) > 6 else (8097, 6, 0, 'd01main_a')
    bad = 0
    total = 0
    seg_total = seg_bad = 0
    stats = dict(grounded=0, foot=0, blocked=0, moved_by_width=0)
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        for c in cases(count, rng, seed, rooms, mapno, stem):
            total += 1
            p.write_bytes(POLYS, c['polys'])
            p.write_bytes(POS, struct.pack('<4f', *c['pos'], 1.0))
            p.write_bytes(VEL, struct.pack('<4f', *c['vel'], 0.0))
            p.write_bytes(OUT, bytes(16))
            p.write_bytes(INFO, bytes(256))
            ret = cave.call(MOVE_CHECK, a=(POS, VEL, OUT, INFO), t=(POLYS, c['n'], c['mask']), timeout=10.0)
            out = struct.unpack('<3f', p.read_bytes(OUT, 12))
            vel = struct.unpack('<3f', p.read_bytes(VEL, 12))
            info = struct.unpack('<64I', p.read_bytes(INFO, 256))
            hit = struct.unpack('<3f', p.read_bytes(INFO + 0xC0, 12))
            for sg in c['segs']:
                p.write_bytes(POS, struct.pack('<4f', *sg['a'], 1.0))
                p.write_bytes(VEL, struct.pack('<4f', *sg['b'], 1.0))
                for nearest, key_i, key_h in ((0, 'i0', 'h0'), (1, 'i1', 'h1')):
                    p.write_bytes(OUT, bytes(16))
                    gi = cave.call(CHECK_HIT, a=(POLYS, c['n'], POS, VEL), t=(OUT, nearest, sg['mask']), timeout=10.0)
                    gi = struct.unpack('<i', struct.pack('<I', gi & 0xFFFFFFFF))[0]
                    gh = struct.unpack('<3f', p.read_bytes(OUT, 12))
                    seg_total += 1
                    if gi != sg[key_i] or (gi >= 0 and max(abs(x - f32(y)) for x, y in zip(gh, sg[key_h])) > TOL):
                        seg_bad += 1
                        if seg_bad <= 6:
                            print('CHECKHIT MISMATCH nearest=%d a=%s b=%s mask=%d: game idx %d hit %s, port idx %d hit %s' % (nearest, sg['a'], sg['b'], sg['mask'], gi, gh, sg[key_i], sg[key_h]))
            p.write_bytes(POS, struct.pack('<4f', *c['pos'], 1.0))
            p.write_bytes(VEL, struct.pack('<4f', *c['vel'], 0.0))
            e = c['exp']
            stats['grounded'] += e['grounded']; stats['foot'] += e['foot']; stats['blocked'] += e['blocked']
            d_out = max(abs(a - f32(b)) for a, b in zip(out, e['out']))
            d_vel = max(abs(a - f32(b)) for a, b in zip(vel, e['vel']))
            grounded, foot = info[0], info[0x18]
            ok = d_out <= TOL and d_vel <= (TOL if TOL == 0.0 else 1e-5) and grounded == e['grounded'] and foot == e['foot'] and (not foot or max(abs(a - f32(b)) for a, b in zip(hit, e['hit'])) <= TOL)
            if not ok:
                bad += 1
                print('MISMATCH case %d: n=%d pos=%s vel=%s mask=%d' % (total, c['n'], c['pos'], c['vel'], c['mask']))
                print('   game: out=%s vel=%s grounded=%d foot=%d hit=%s ret=%d' % (out, vel, grounded, foot, hit, ret & 0xFF))
                print('   port: out=%s vel=%s grounded=%d foot=%d hit=%s blocked=%d' % (e['out'], e['vel'], e['grounded'], e['foot'], e['hit'], e['blocked']))
    print('cases %d, mismatches %d, coverage %s; CheckHit segments %d, mismatches %d' % (total, bad, stats, seg_total, seg_bad))


main()
