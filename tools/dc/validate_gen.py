"""Run the game's own buildRandomMap on the live dungeon for many random states and compare with the C++ port.
usage: validate_gen.py [count] [rooms-min] [rooms-max]
Needs PCSX2 running in a dungeon (state loaded), unpaused. Overwrites the live floor (reload the state afterwards)."""
import os, random, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave

EXE = os.path.join(HERE, '..', '..', 'native', 'build', 'core_release', 'dcdungeon.exe')
BASE = 0x01DC4BE0            # MainDungeonMap
SRAND, BUILD = 0x001046E8, 0x001CB670
IMPURE = 0x0024FDEC

SCAN = 0x01D569A0   # GetRoomLinkInfo scan array (256 ints), directly in front of the cell array

def mine(seed, rooms, mapno, scan=None):
    args = [EXE, str(seed), str(rooms), str(mapno), '--iso', os.path.join(HERE, '..', '..', 'iso', 'Dark Cloud (USA).iso')]
    if scan is not None:
        args += ['--scan', ','.join(str(v) for v in scan)]
    out = subprocess.run(args, capture_output=True, text=True).stdout.split(chr(10))
    n = int(out[0].split()[1])
    rects = [tuple(map(int, out[1 + i].split())) for i in range(n)]
    cells = [tuple(map(int, out[1 + n + i].split())) for i in range(400)]
    scan_after = list(map(int, out[2 + n + 400].split()[1:]))
    boxes = [float(v) for v in out[3 + n + 400].split()[2:]]
    return rects, cells, scan_after, boxes

def derive(state):
    # rand() then fptosi(float(r) / 100000.0f): ask the tool for it via a tiny helper (seed = value printed by "dcdungeon --derive")
    out = subprocess.run([EXE, str(state), '0', '0', '--derive'], capture_output=True, text=True).stdout.split()
    return int(out[0])

def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]

def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    rmin = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    rmax = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    rnd = random.Random(1234)
    bad = 0
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        mapno = p.read(0x002A3594)
        for t in range(count):
            s0 = rnd.randrange(1, 2**31)
            rooms = rnd.randint(rmin, rmax)
            scan_before = [s32(v) for v in struct.unpack('<256I', p.read_bytes(SCAN, 1024))]
            cave.call_batch([dict(func=SRAND, a=(s0,)), dict(func=BUILD, a=(BASE, rooms, 1))], timeout=10.0)
            seed = p.read(BASE + 0x468)
            n = p.read(BASE + 0xb650)
            rects = [tuple(p.read(BASE + 0xb550 + i * 16 + k * 4) for k in range(4)) for i in range(n)]
            words = p.read_bytes(BASE + 0x9c50, 6400)
            live = [(s32(struct.unpack_from('<I', words, i * 16)[0]), struct.unpack_from('<I', words, i * 16 + 4)[0]) for i in range(400)]
            want = derive(s0)
            mr, mc, mscan, mboxes = mine(want, rooms, mapno, scan_before)
            nbox = p.read(BASE + 0xbc60)
            gboxes = [struct.unpack('<3f', p.read_bytes(BASE + 0xb670 + i * 0x40, 12)) for i in range(nbox)]
            boxes_ok = len(gboxes) * 3 == len(mboxes) and all(abs(a - b) < 0.05 for g, m in zip(gboxes, zip(mboxes[0::3], mboxes[1::3], mboxes[2::3])) for a, b in zip(g, m))
            scan_live = [s32(v) for v in struct.unpack('<256I', p.read_bytes(SCAN, 1024))]
            diffs = [i for i in range(400) if live[i] != mc[i][:2]]
            ok = (seed == want and mr == rects and not diffs and scan_live == mscan and boxes_ok)
            if not ok:
                bad += 1
            print('%3d s0=%d rooms=%d seed live/mine=%d/%d  rects_equal=%s  cell_diffs=%d scan_equal=%s boxes_ok=%s %s' %
                  (t, s0, rooms, seed, want, mr == rects, len(diffs), scan_live == mscan, boxes_ok, '' if ok else [(i % 20, i // 20, live[i], mc[i][:2]) for i in diffs[:6]]))
    print('mismatching floors:', bad, 'of', count)

main()
