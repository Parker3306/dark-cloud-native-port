"""Whole-floor live check of BtCleatRandomMap: the real game builds a floor (main + back layout, both chest lists, the main floor's 15 monsters), then the back floor's 8 monsters the way
gameTask 0xa0 places them; the C++ port (dcback.exe) does the same from the same random generator state and the two are compared: cells, events / chests / atra / traps of both floors,
monster positions and hp of both floors.
usage: validate_back_all.py [count] [--rooms N] [-v]    Needs PCSX2 running in a dungeon (tools/dc/reset_game.sh); overwrites the live floor (reload the state afterwards)."""
import os, random, struct, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave, PHASES

ROOT = os.path.join(HERE, '..', '..')
EXE = os.path.join(ROOT, 'native', 'build', 'core_release', 'dcback.exe')
ISO = os.path.join(ROOT, 'iso', 'Dark Cloud (USA).iso')
MAIN, URA = 0x01DC4BE0, 0x01DD56F0
SRAND, CLEAR_RANDOM_MAP, LOAD_MONSTOR, ARRENGE = 0x001046E8, 0x01DB8790, 0x01DB9330, 0x01DB92E0
CLEAN_VIEW, ARRANGEMENT_POS, RSET_MIMIC = 0x001DF9F0, 0x001D7FC0, 0x001C8E50
SCAN = 0x01D569A0
NOW_MONSTOR, SELECT_MAP, USER_STATUS = 0x002A34D0, 0x002A3594, 0x002A3468
IMPURE = 0x0024FDEC
NOW_DNG_MAP, BT_URA = 0x002A34B8, 0x002A34B4
STEMS = ['d01main_a', 'd02main_a', 'd03main_a', 'd04main_a', 'd05main_a', 'd06main_a', 'd07main_a']


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]


def live_cells(p, base):
    words = p.read_bytes(base + 0x9c50, 6400)
    return [(s32(struct.unpack_from('<I', words, i * 16)[0]), s32(struct.unpack_from('<I', words, i * 16 + 4)[0])) for i in range(400)]


def live_events(p, base):
    out = []
    for i in range(0x30):
        e = p.read_bytes(base + 0x8d50 + 0x50 * i, 0x50)
        typ = struct.unpack('<i', e[8:12])[0]
        if typ != -1:
            out.append(('event', i, typ, struct.unpack('<i', e[0x28:0x2c])[0], struct.unpack('<3f', e[0x10:0x1c])))
    for i in range(0x18):
        c = p.read_bytes(base + 0xb660 + 0x40 * i, 0x40)
        if struct.unpack('<i', c[0:4])[0]:
            item, one, small, st, q = struct.unpack('<5i', c[0x20:0x34])
            out.append(('chest', i, item, small, q, struct.unpack('<3f', c[0x10:0x1c])))
    na = p.read(base + 0xbd80)
    for i in range(na):
        a = p.read_bytes(base + 0xbc80 + 0x20 * i, 0x20)
        out.append(('atra', i, struct.unpack('<i', a[16:20])[0], struct.unpack('<3f', a[0:12])))
    for i in range(3):
        t = p.read_bytes(base + 0x10ab0 + 0x20 * i, 0x20)
        if struct.unpack('<i', t[16:20])[0]:
            out.append(('trap', i, struct.unpack('<i', t[20:24])[0], struct.unpack('<3f', t[0:12])))
    return out


def live_monsters(p):
    unit = p.read(NOW_MONSTOR)
    out = []
    for k in range(16):
        U = unit + 0x1e3d0 + k * 400
        C = unit + 0x1fcd0 + k * 0x3510
        state = s32(p.read(U))
        if state == -1:
            continue
        out.append((k, s32(p.read(U + 0x24)), struct.unpack('<3f', p.read_bytes(C + 0x10, 12))))
    return out


def run_port(state, rooms, mapno, floor, scan, status, arrange_back):
    args = [EXE, ISO, STEMS[mapno], str(state), str(rooms), str(mapno), str(floor), '--scan', ','.join(str(v) for v in scan), '--status', status, '--arrange-back', str(arrange_back)]
    lines = subprocess.run(args, capture_output=True, text=True).stdout.split('\n')
    res = {'main': {'cells': [], 'ev': [], 'mon': [], 'rand': {}}, 'back': {'cells': [], 'ev': [], 'mon': [], 'rand': {}}}
    for ln in lines:
        w = ln.split()
        if len(w) < 2 or w[0] not in res:
            continue
        r = res[w[0]]
        pos = lambda i: tuple(float(v) for v in w[i:i + 3])
        if w[1] == 'cells':
            r['cells'] = [tuple(int(x) for x in c.split(':')) for c in w[2:]]
        elif w[1] == 'event':
            r['ev'].append(('event', int(w[2]), int(w[4]), int(w[6]), pos(8)))
        elif w[1] == 'chest':
            r['ev'].append(('chest', int(w[2]), int(w[4]), int(w[6]), int(w[8]), pos(10)))
        elif w[1] == 'atra':
            r['ev'].append(('atra', int(w[2]), int(w[4]), pos(6)))
        elif w[1] == 'trap':
            r['ev'].append(('trap', int(w[2]), int(w[4]), pos(6)))
        elif w[1] == 'mon':
            r['mon'].append((int(w[2]), int(w[6]), pos(8)))
        elif w[1].startswith('rand_'):
            r['rand'][w[1]] = int(w[2])
        elif w[1] == 'seed':
            r['rand']['seed'] = int(w[2])
    return res


def close(a, b):
    return all(abs(u - v) < 0.01 for u, v in zip(a, b))


def same_ev(a, b):
    return len(a) == len(b) and all(x[:-1] == y[:-1] and close(x[-1], y[-1]) for x, y in zip(a, b))


def same_mon(a, b):
    return len(a) == len(b) and all(x[0] == y[0] and x[1] == y[1] and close(x[2], y[2]) for x, y in zip(a, b))


def main():
    args = [a for a in sys.argv[1:] if a.isdigit()]
    count = int(args[0]) if args else 6
    rooms = 6
    verbose = '-v' in sys.argv
    rnd = random.Random(2026)
    seeds = [int(a) for a in sys.argv[sys.argv.index('--seeds') + 1].split(',')] if '--seeds' in sys.argv else []
    if seeds:
        count = len(seeds)
    bad = 0
    with Pine() as p:
        if '--no-reload' not in sys.argv:   # (the last run left the game with half-swapped monsters: start from the saved dungeon floor, as tools/dc/reset_game.sh does)
            p.load_state(1)
            time.sleep(8)
        if '--dungeon' in sys.argv:   # start that floor with the game's own floor-start function (tools/dc/boss_live.py does the same for the boss floors), wait for free movement
            d, f = int(sys.argv[sys.argv.index('--dungeon') + 1]), int(sys.argv[sys.argv.index('--floor') + 1])
            us = p.read(USER_STATUS)
            p.write(us + 0, d, 1)
            p.write(us + 2, f - 1, 1)
            p.write(us + 3, f - 1, 1)
            p.write(SELECT_MAP, d, 4)
            p.write(0x01D5648C, 100, 4)
            p.write(0x01D56490, 0, 4)
            p.write(0x002A35EC, 0xDE, 4)
            still = 0
            for _ in range(120):
                time.sleep(1)
                still = still + 1 if p.read(0x002A35EC) == 0 else 0
                if still >= 4:
                    break
            time.sleep(1)
            print('entered dungeon %d floor %d (task %d, floor byte %d)' % (d, f, p.read(0x002A35EC), p.read_bytes(us + 2, 1)[0]))
        cave = Cave(p)
        if not cave.installed():
            cave.install()
            time.sleep(1)
        gate = PHASES['update']['mailbox'] + 8
        p.write(gate + 4, 0)
        p.write(gate, 1)
        time.sleep(0.5)
        mapno = p.read(SELECT_MAP)
        ustat = p.read(USER_STATUS)
        floor = struct.unpack('<b', p.read_bytes(ustat + 2, 1))[0]
        status = os.path.join(ROOT, 'work', 'user_status_live.bin')
        open(status, 'wb').write(b''.join(p.read_bytes(ustat + off, 0x400) for off in range(0, 0x9000, 0x400)))
        print('dungeon %d floor %d' % (mapno, floor))
        unit = p.read(NOW_MONSTOR)
        for t in range(count):
            s0 = seeds[t] if seeds else rnd.randrange(1, 2**31)
            open(status, 'wb').write(b''.join(p.read_bytes(ustat + off, 0x400) for off in range(0, 0x9000, 0x400)))   # (every floor start changes the status: atra list, event flags ...)
            if '--keep-status' in sys.argv:
                open(status.replace('.bin', '_t%d.bin' % t), 'wb').write(open(status, 'rb').read())
            scan = [s32(v) for v in struct.unpack('<256I', p.read_bytes(SCAN, 1024))]
            for c in (dict(func=CLEAN_VIEW, a=(unit, 0)), dict(func=SRAND, a=(s0,)), dict(func=CLEAR_RANDOM_MAP), dict(func=LOAD_MONSTOR, a=(0,)), dict(func=ARRENGE)):   # (one at a time: a batch is not always run in order)
                cave.call_batch([c], timeout=60.0)
            lm = live_monsters(p)
            lc_main, lc_back = live_cells(p, MAIN), live_cells(p, URA)
            le_main = live_events(p, MAIN)
            # the swap (gameTask 0xa0, main -> back): the monsters are wiped, the back row is loaded and eight are placed on the back floor
            ab = rnd.randrange(1, 2**31)
            p.write(NOW_DNG_MAP, URA, 4)   # (gameTask 0xa0: NowDngMap = the back floor while its monsters are placed: a mimic's chest goes into that map's events)
            p.write(BT_URA, 1, 4)
            for c in (dict(func=CLEAN_VIEW, a=(unit, 0)), dict(func=RSET_MIMIC, a=(URA,)), dict(func=CLEAN_VIEW, a=(unit, 1)), dict(func=LOAD_MONSTOR, a=(1,)), dict(func=SRAND, a=(ab,)), dict(func=ARRANGEMENT_POS, a=(unit, URA, 8, 0xFFFFFFFF), t=(0,))):
                cave.call_batch([c], timeout=60.0)
            p.write(NOW_DNG_MAP, MAIN, 4)
            p.write(BT_URA, 0, 4)
            le_back = live_events(p, URA)
            lb = live_monsters(p)
            if verbose and not lb:
                print('   back slots', [(s32(p.read(unit + 0x1e3d0 + k * 400)), s32(p.read(unit + 0x1e3d0 + k * 400 + 0x24))) for k in range(16)], 'unit+0x44', p.read(unit + 0x44), 'rand', p.read(p.read(IMPURE) + 0x58))
            mine = run_port(s0, rooms, mapno, floor, scan, status, ab)
            m, b = mine['main'], mine['back']
            checks = [('main cells', lc_main == m['cells']), ('back cells', lc_back == b['cells']), ('main events', same_ev(le_main, m['ev'])),
                      ('back events', same_ev(le_back, b['ev'])), ('main monsters', same_mon(lm, m['mon'])), ('back monsters', same_mon(lb, b['mon']))]
            ok = all(c[1] for c in checks)
            bad += 0 if ok else 1
            print('%2d s0=%d %s  %s  (live %d/%d monsters, %d/%d events)' % (t, s0, 'OK' if ok else 'DIFF', ' '.join('%s=%s' % (n, 'ok' if v else 'DIFF') for n, v in checks),
                                                                          len(lm), len(lb), len(le_main), len(le_back)))
            if verbose:
                open(os.path.join(ROOT, 'work', 'live_cells_t%d.txt' % t), 'w').write(' '.join('%d:%d' % c for c in lc_main) + chr(10) + ' '.join('%d:%d' % c for c in lc_back) + chr(10) + str(s0) + ' ' + ' '.join(str(v) for v in scan) + chr(10))
            if verbose and not ok:
                for name, lc, mc in (('main', lc_main, m['cells']), ('back', lc_back, b['cells'])):
                    dd = [i for i in range(400) if lc[i] != mc[i]]
                    if dd:
                        print('   %s cells differ at %d cells, first: %s' % (name, len(dd), [(i % 20, i // 20, lc[i], mc[i]) for i in dd[:8]]))
                for name, lv, mv in (('main events', le_main, m['ev']), ('back events', le_back, b['ev']), ('main mon', lm, m['mon']), ('back mon', lb, b['mon'])):
                    same = same_ev if 'events' in name else same_mon
                    if not same(lv, mv):
                        k = next((i for i in range(min(len(lv), len(mv))) if not same([lv[i]], [mv[i]])), min(len(lv), len(mv)))
                        print('   %s differ from entry %d (live %d entries, mine %d): live %s | mine %s' % (name, k, len(lv), len(mv), lv[k] if k < len(lv) else '-', mv[k] if k < len(mv) else '-'))
        p.write(gate, 0)
    print('mismatching floors:', bad, 'of', count)


main()
