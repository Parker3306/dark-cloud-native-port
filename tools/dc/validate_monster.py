"""Lock-step comparison of the C++ monster AI (dcmonster.exe) with the running game.
usage: validate_monster.py [ticks] [arrange_seed] [--near SLOT] [--dist D] [--rotmode M]
The live game (dungeon floor, hooks installable) is paused at the end of a frame (hook GATE). The game's own BtArrengeMonstor is called after srand(arrange_seed)
so every monster is fresh (scripts not started); the player is moved next to one monster; then the game is released one frame at a time while the state of
all 16 monster slots, the player position and the rand() state are recorded. dcmonster.exe is run on the same floor with the recorded player positions and rand
states fed in, and the two trajectories are compared slot by slot."""
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave, PHASES

ISO = os.path.join(HERE, '..', '..', 'iso', 'Dark Cloud (USA).iso')
EXE = os.path.join(HERE, '..', '..', 'native', 'build', 'core_release', 'dcmonster.exe')
SRAND, BT_ARRENGE = 0x001046E8, 0x01DB92E0
NOW_MONSTOR, NOW_DNG_MAP, SELECT_MAP, USER_STATUS, BT_URA, IMPURE = 0x002A34D0, 0x002A34B8, 0x002A3594, 0x002A3468, 0x002A34B4, 0x0024FDEC
CHARA_FRAME, PLAYER_VEC = 0x002A3500, 0x01EA1D30
HOLD_A, HOLD_B, ALL_DRAW, EVENT_MODE = 0x002A3564, 0x002A34DC, 0x002A3560, 0x002A3608
WORK = os.path.join(HERE, '..', '..', 'work')
STEP_CALL, STEP_FUNC = 0x01DB749C, 0x001DD540        # the `jal CMonstorUnit::Step` in motionDrive, and Step itself
PROBE, PROBE_OUT = 0x01F62000, 0x01F62F00            # a tiny stub that records rand()'s state and the hold argument when Step is entered


def install_probe(p):
    """Replaces the `jal Step` of motionDrive by a wrapper that records rand()'s state and the hold argument on entry, calls Step, and records rand()'s state again on exit."""
    from mips import assemble
    code = assemble('''
        addiu sp, sp, -0x20
        sd    ra, 0(sp)
        lui   t0, 0x0025
        lw    t0, -0x0214(t0)
        lw    t1, 0x58(t0)
        li    t2, %#x
        sw    t1, 0(t2)
        sw    a1, 4(t2)
        lw    t3, 8(t2)
        addiu t3, t3, 1
        sw    t3, 8(t2)
        jal   %#x
        nop
        lui   t0, 0x0025
        lw    t0, -0x0214(t0)
        lw    t1, 0x58(t0)
        li    t2, %#x
        sw    t1, 12(t2)
        ld    ra, 0(sp)
        addiu sp, sp, 0x20
        jr    ra
        nop
    ''' % (PROBE_OUT, STEP_FUNC, PROBE_OUT), PROBE)
    p.write_bytes(PROBE_OUT, bytes(16))
    p.write_bytes(PROBE, code)
    p.write(STEP_CALL, 0x0C000000 | (PROBE >> 2))


def f32(p, a):
    return struct.unpack('<f', struct.pack('<I', p.read(a)))[0]


def i32(p, a):
    return struct.unpack('<i', struct.pack('<I', p.read(a)))[0]


def vec(p, a):
    return struct.unpack('<3f', p.read_bytes(a, 12))


def slot_state(p, unit, k):
    U = unit + 0x1e3d0 + k * 400
    C = unit + 0x1fcd0 + k * 0x3510
    return dict(
        state=i32(p, U), hp=i32(p, U + 0x24), pos=vec(p, C + 0x10), yaw=f32(p, C + 0x64), speed=f32(p, U + 0x80), rot_speed=f32(p, U + 0x84),
        req=(f32(p, C + 0xc60), p.read(C + 0xc64), i32(p, C + 0xc68)), mtime=f32(p, C + 0x2f0), mcur=i32(p, C + 0x304), flag=p.read(unit + 0x50 + 4 * k), hitstop=i32(p, U + 8), pause=p.read(unit + 0x98), muteki=i32(p, U + 0x98), height=f32(p, U + 0xb8), floor_y=f32(p, U + 0xbc), dir=vec(p, U + 0x60), pc=(p.read(unit + 0x54dd0 + k * 0x48 + 0x30) - p.read(unit + 0x54dd0 + k * 0x48 + 0x3c)) if p.read(unit + 0x54dd0 + k * 0x48 + 0x30) else 0)


def main():
    args = [a for a in sys.argv[1:]]
    opts = {}
    for key in ('--near', '--dist', '--rotmode'):
        if key in args:
            i = args.index(key)
            opts[key] = args[i + 1]
            del args[i:i + 2]
    ticks = int(args[0]) if args else 200
    aseed = int(args[1], 0) if len(args) > 1 else 12345
    dist = float(opts.get('--dist', 120))
    with Pine() as p:
        p.load_state(1)           # the saved test floor; a state load restores RAM, so the hooks are installed again afterwards
        time.sleep(6)
        cave = Cave(p)
        cave.uninstall()
        time.sleep(0.3)
        cave.install()
        time.sleep(1)
        gate = PHASES['update']['mailbox'] + 8
        hb_addr = PHASES['update']['mailbox'] + 4
        p.write(gate + 4, 0)
        p.write(gate, 1)
        h0 = p.read(hb_addr)
        for _ in range(200):
            time.sleep(0.01)
            if p.read(hb_addr) != h0:
                break
        time.sleep(0.1)
        dng = p.read(NOW_DNG_MAP)
        seed, rooms, mapno = p.read(dng + 0x468), p.read(dng + 0xb650), p.read(SELECT_MAP)
        unit = p.read(NOW_MONSTOR)
        us = p.read(USER_STATUS)
        floor = p.read_bytes(us, 4)[2]
        ura = p.read(BT_URA)
        print('floor seed %d rooms %d map %d floor %d ura %d' % (seed, rooms, mapno, floor, ura))
        install_probe(p)
        cave.call_batch([dict(func=SRAND, a=(aseed,))])
        cave.call_batch([dict(func=BT_ARRENGE)], timeout=20.0)
        slots0 = [slot_state(p, unit, k) for k in range(16)]
        live = [k for k in range(16) if slots0[k]['state'] != -1]
        print('live monsters after arrange:', [(k, tuple(round(x, 2) for x in slots0[k]['pos'])) for k in live])
        target = int(opts['--near']) if '--near' in opts else live[0]
        tp = slots0[target]['pos']
        fr = p.read(CHARA_FRAME)
        for addr in (fr + 0x220, PLAYER_VEC):
            p.write_bytes(addr, struct.pack('<3f', tp[0] + dist, tp[1], tp[2]))
        rs = lambda: p.read(p.read(IMPURE) + 0x58)
        hold = lambda: (p.read(HOLD_A) | p.read(HOLD_B)) if (p.read(ALL_DRAW) != 0 and p.read(EVENT_MODE) == 0) else 1
        feed = [(*vec(p, PLAYER_VEC), rs(), hold())]
        step_info = []
        states = []
        last_count = 0
        for t in range(ticks):
            hb = p.read(hb_addr)
            p.write(gate + 4, 1)
            for _ in range(400):
                if p.read(hb_addr) != hb:
                    break
                time.sleep(0.002)
            else:
                raise RuntimeError('the game did not advance a frame')
            r_step, hold_arg, count, r_exit = p.read(PROBE_OUT), p.read(PROBE_OUT + 4), p.read(PROBE_OUT + 8), p.read(PROBE_OUT + 12)
            step_info.append((r_step, hold_arg, r_exit) if count != last_count else (feed[-1][3], 1, feed[-1][3]))
            last_count = count
            states.append([slot_state(p, unit, k) for k in range(16)])
            feed.append((*vec(p, PLAYER_VEC), rs(), hold()))
        p.write(gate, 0)
    os.makedirs(WORK, exist_ok=True)
    feed_file = os.path.join(WORK, 'monster_feed.txt')
    with open(feed_file, 'w') as f:
        for t in range(ticks):
            px, py, pz = feed[t][:3]
            r, h, _ = step_info[t]
            f.write('%.9g %.9g %.9g %d %d\n' % (px, py, pz, r, h))   # player position and rand() state/hold argument as seen when Step was entered
    json.dump(dict(slots0=slots0, states=states, feed=feed, step_info=step_info), open(os.path.join(WORK, 'monster_live.json'), 'w'))
    init_file = os.path.join(WORK, 'monster_init.txt')
    with open(init_file, 'w') as f:
        for k in range(16):
            f.write('%d %.9g %.9g\n' % (k, slots0[k]['height'], slots0[k]['floor_y']))
    cmd = [EXE, ISO, 'd01main_a', str(seed), str(rooms), str(mapno), str(floor), str(ticks), '--arrange-seed', str(aseed), '--feed', feed_file, '--init', init_file]
    if '--rotmode' in opts:
        cmd += ['--rotmode', opts['--rotmode']]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.split(chr(10))
    mine0, mine, sim_draws = {}, {}, {}
    for line in out:
        if line.startswith('draws '):
            sim_draws[int(line.split()[1])] = int(line.split()[2])
    for line in out:
        parts = line.replace('|', ' ').split()
        if line.startswith('slot '):
            mine0[int(parts[1])] = tuple(map(float, parts[-3:]))
        elif parts and parts[0].isdigit() and len(parts) >= 20:
            mine[(int(parts[0]), int(parts[1]))] = dict(state=int(parts[2]), pos=tuple(map(float, parts[3:6])), yaw=float(parts[6]), speed=float(parts[7]), rot_speed=float(parts[8]), req=int(parts[9]),
                                                      mtime=float(parts[10]), mcur=int(parts[11]), flag=int(parts[12]), hp=int(parts[13]), height=float(parts[14]), floor_y=float(parts[15]), dir=tuple(map(float, parts[16:19])), pc=int(parts[19]))
    bad0 = [k for k in live if k not in mine0 or max(abs(a - b) for a, b in zip(mine0[k], slots0[k]['pos'])) > 0.01]
    print('arrangement: %d of %d positions match' % (len(live) - len(bad0), len(live)), ('mismatch slots %s' % bad0) if bad0 else '')
    # rand() numbers taken inside Step: the game's (from the states at entry and exit) against the port's
    def lcg_steps(a, b, limit=500):
        x = a
        for n in range(limit):
            if x == b:
                return n
            x = (x * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
        return None
    bad_draws = [(t, lcg_steps(step_info[t][0], step_info[t][2]), sim_draws.get(t)) for t in range(ticks) if lcg_steps(step_info[t][0], step_info[t][2]) != sim_draws.get(t)]
    print('rand draws per tick differ in %d of %d ticks%s' % (len(bad_draws), ticks, (', first: tick %d game %s port %s' % bad_draws[0]) if bad_draws else ''))
    awake_slots = [k for k in range(16) if any(states[t][k]['state'] == 2 for t in range(ticks))]
    for t, g, m in bad_draws[:30]:
        print('   tick %d: game %s draws, port %s;' % (t, g, m), ' '.join('slot%d req %d mtime %.1f/%d' % (k, states[t][k]['req'][2], states[t][k]['mtime'], states[t][k]['mcur']) for k in awake_slots))
    # slots whose awake/asleep state differs between the game and the port
    for k in range(16):
        for t in range(ticks):
            m = mine.get((t, k))
            if m is not None and m['state'] != states[t][k]['state']:
                print('slot %d: state differs at tick %d (game %d, port %d)' % (k, t, states[t][k]['state'], m['state']))
                break
    for k in range(16):
        for t in range(ticks):
            m = mine.get((t, k))
            if m is not None and states[t][k]['state'] == 2 and m['pc'] != states[t][k]['pc']:
                print('slot %d: script position differs at tick %d (game %#x, port %#x); previous ticks game %s' % (k, t, states[t][k]['pc'], m['pc'], [hex(states[u][k]['pc']) for u in range(max(0, t - 3), t)]))
                break
    # trajectory comparison per slot that was awake in the game
    report = []
    for k in range(16):
        awake = [t for t in range(ticks) if states[t][k]['state'] == 2]
        if not awake:
            continue
        first_bad = None
        worst = 0.0
        for t in awake:
            g, m = states[t][k], mine.get((t, k))
            if m is None:
                first_bad = first_bad or (t, 'missing')
                continue
            d = max(abs(a - b) for a, b in zip(g['pos'], m['pos']))
            dy = abs(g['yaw'] - m['yaw'])
            worst = max(worst, d)
            if first_bad is None and (d > float(os.environ.get('DC_EPS', '0.02')) or dy > 0.01 or g['req'][2] != m['req'] or abs(g['mtime'] - m['mtime']) > 0.05):
                first_bad = (t, 'pos %.4f yaw %.4f req %s/%s mtime %.2f/%.2f' % (d, dy, g['req'][2], m['req'], g['mtime'], m['mtime']))
        report.append((k, len(awake), awake[0], worst, first_bad))
    for k, n, t0, worst, fb in report:
        print('slot %d: awake %d ticks from %d, worst position difference %.4f, first deviation: %s' % (k, n, t0, worst, fb))
        if fb:
            t = fb[0]
            if isinstance(t, int) and t < ticks:
                lo = max(0, t - 2)
                for tt in range(lo, min(ticks, t + 2)):
                    g = states[tt][k]
                    print('   tick %d game: pos %s yaw %.4f speed %.4f rotspd %.4f req %s motion %.2f/%d flag %d hitstop %d pause %d muteki %d height %.3f floor %.3f dir %s | rand %d' % (tt, tuple(round(x, 4) for x in g['pos']), g['yaw'], g['speed'], g['rot_speed'], g['req'], g['mtime'], g['mcur'], g['flag'], g['hitstop'], g['pause'], g['muteki'], g['height'], g['floor_y'], tuple(round(x, 4) for x in g['dir']), feed[tt + 1][3]))
                    m = mine.get((tt, k))
                    if m:
                        print('   tick %d port: pos %s yaw %.4f speed %.4f rotspd %.4f req %s motion %.2f/%d flag %d height %.3f floor %.3f dir %s' % (tt, m['pos'], m['yaw'], m['speed'], m['rot_speed'], m['req'], m['mtime'], m['mcur'], m['flag'], m['height'], m['floor_y'], tuple(round(x, 4) for x in m['dir'])))


if __name__ == '__main__':
    main()
