"""Builds floors with the game's own buildRandomMap + buildEventData (random generator state s0, srand(s0) first) on the live dungeon and compares chests, atra
balls, trap circles and event slots with the C++ port (dcevents --derive).
usage: validate_events.py [count] [rooms-min] [rooms-max]     Needs PCSX2 running in a dungeon; overwrites the live floor (reload the state afterwards)."""
import os, random, struct, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave, PHASES

ROOT = os.path.join(HERE, '..', '..')
EXE = os.path.join(ROOT, 'native', 'build', 'core_release', 'dcevents.exe')
ISO = os.path.join(ROOT, 'iso', 'Dark Cloud (USA).iso')
BASE = 0x01DC4BE0
SRAND, BUILD, EVENTS = 0x001046E8, 0x001CB670, 0x001C8540
SCAN = 0x01D569A0
IMPURE = 0x0024FDEC


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]


def live_events(p):
    out = []
    for i in range(0x30):
        e = p.read_bytes(BASE + 0x8d50 + 0x50 * i, 0x50)
        typ = struct.unpack('<i', e[8:12])[0]
        if typ != -1:
            pos = struct.unpack('<3f', e[0x10:0x1c])
            out.append(('event', i, typ, struct.unpack('<i', e[0x28:0x2c])[0], pos))
    n = p.read(BASE + 0xbc60)
    for i in range(0x18):
        c = p.read_bytes(BASE + 0xb660 + 0x40 * i, 0x40)
        if struct.unpack('<i', c[0:4])[0]:
            item, one, small, st, q = struct.unpack('<5i', c[0x20:0x34])
            out.append(('chest', i, item, small, q, struct.unpack('<3f', c[0x10:0x1c])))
    na = p.read(BASE + 0xbd80)
    for i in range(na):
        a = p.read_bytes(BASE + 0xbc80 + 0x20 * i, 0x20)
        out.append(('atra', i, struct.unpack('<i', a[16:20])[0], struct.unpack('<3f', a[0:12])))
    for i in range(3):
        t = p.read_bytes(BASE + 0x10ab0 + 0x20 * i, 0x20)
        if struct.unpack('<i', t[16:20])[0]:
            out.append(('trap', i, struct.unpack('<i', t[20:24])[0], struct.unpack('<3f', t[0:12])))
    return out


def mine(s0, rooms, mapno, floor, scan, status, rstate=None):
    args = [EXE, ISO, 'd01main_a', str(s0), str(rooms), str(mapno), str(floor), '--derive', '--scan', ','.join(str(v) for v in scan), '--status', status]
    if rstate is not None:
        args += ['--rand-state', str(rstate)]
    lines = subprocess.run(args, capture_output=True, text=True).stdout.split(chr(10))
    out = []
    mine.after_map = int(lines[0].split()[1])
    for ln in lines:
        w = ln.split()
        if not w:
            continue
        if w[0] == 'event':
            out.append(('event', int(w[1]), int(w[3]), int(w[5]), tuple(float(v) for v in w[7:10])))
        elif w[0] == 'chest':
            out.append(('chest', int(w[1]), int(w[3]), int(w[5]), int(w[7]), tuple(float(v) for v in w[9:12])))
        elif w[0] == 'atra':
            out.append(('atra', int(w[1]), int(w[3]), tuple(float(v) for v in w[5:8])))
        elif w[0] == 'trap':
            out.append(('trap', int(w[1]), int(w[3]), tuple(float(v) for v in w[5:8])))
    return out


def same(a, b):
    if len(a) != len(b):
        return False
    for x, y in zip(a, b):
        if x[:-1] != y[:-1]:
            return False
        if any(abs(u - v) > 0.01 for u, v in zip(x[-1], y[-1])):
            return False
    return True


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    rmin = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    rmax = int(sys.argv[3]) if len(sys.argv) > 3 else 7
    rnd = random.Random(99)
    bad = 0
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        gate = PHASES['update']['mailbox'] + 8
        p.write(gate + 4, 0)
        p.write(gate, 1)       # freeze the game at the end of its frame; the stub keeps serving the mailbox
        time.sleep(0.5)
        mapno = p.read(0x002A3594)
        ustat = p.read(0x002A3468)
        floor = struct.unpack('<b', p.read_bytes(ustat + 2, 1))[0]
        status = os.path.join(ROOT, 'work', 'user_status_live.bin')
        open(status, 'wb').write(b''.join(p.read_bytes(ustat + off, 0x400) for off in range(0, 0x9000, 0x400)))
        for t in range(count):
            s0 = rnd.randrange(1, 2**31)
            rooms = rnd.randint(rmin, rmax)
            scan_before = [s32(v) for v in struct.unpack('<256I', p.read_bytes(SCAN, 1024))]
            for attempt in range(5):
                cave.call_batch([dict(func=SRAND, a=(s0,)), dict(func=BUILD, a=(BASE, rooms, 1))], timeout=20.0)
                time.sleep(0.05)
                rs = p.read(p.read(IMPURE) + 0x58)
                if rs != s0:
                    break
            nb = p.read(BASE + 0xbc60)
            print('   boxes after map (live):', nb, [struct.unpack('<3f', p.read_bytes(BASE + 0xb670 + i * 0x40, 12)) for i in range(nb)])
            cave.call_batch([dict(func=EVENTS, a=(BASE, floor, p.read(BASE + 0xbdec), 1))], timeout=20.0)
            live = live_events(p)
            mine_ev = mine(s0, rooms, mapno, floor, scan_before, status, rs if '--use-live-state' in sys.argv else None)
            print('   rand after map: live %d mine %d' % (rs, mine.after_map))
            ok = same(live, mine_ev)
            if not ok:
                bad += 1
            print('%2d s0=%d rooms=%d %s  live %d events, mine %d' % (t, s0, rooms, 'OK' if ok else 'DIFF', len(live), len(mine_ev)))
            if not ok and '-v' in sys.argv:
                for x in live:
                    print('   live', x)
                for x in mine_ev:
                    print('   mine', x)
        p.write(gate, 0)
    print('mismatching floors:', bad, 'of', count)


main()
