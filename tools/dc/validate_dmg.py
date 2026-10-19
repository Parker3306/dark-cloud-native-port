"""Lock-step check of the monster damage code (CheckDmg, hit reactions, poison/status, death and loot) against the running game.
usage: validate_dmg.py [ticks] [arrange_seed] [--near SLOT] [--dist D] [--hits tick:damage:flags:owner:sub:status,...]
Like validate_monster.py, but at the given ticks a hit box (as the player's weapon would put into the pool) is written into the game's NowColData right on a body sphere
of the target monster, and the same box is injected into the port. Compares life, status counters, script position and rand() use tick by tick."""
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import validate_monster as vm
from validate_monster import *   # noqa: F401,F403  (constants and helpers)

NOW_COL = 0x002A35E0


def snapshot_status(p, path):
    us = p.read(USER_STATUS)
    data = b''.join(p.read_bytes(us + off, 0x400) for off in range(0, 0x9000, 0x400))
    with open(path, 'wb') as f:
        f.write(data)
    return us


def write_entry(p, pool, idx, pos, radius, damage, flags, owner, sub, status, life=1):
    e = bytearray(0xa0)
    struct.pack_into('<4f', e, 0x00, pos[0], pos[1], pos[2], 1.0)
    struct.pack_into('<4f', e, 0x10, 0, 0, 0, 1.0)
    struct.pack_into('<4f', e, 0x20, 1.0, 0, 0, 0)
    struct.pack_into('<i', e, 0x34, damage)
    struct.pack_into('<f', e, 0x38, 0.0)
    struct.pack_into('<f', e, 0x3c, radius)
    struct.pack_into('<f', e, 0x40, 0.0)
    struct.pack_into('<i', e, 0x44, life)
    struct.pack_into('<i', e, 0x48, 2)
    struct.pack_into('<i', e, 0x4c, 2)
    struct.pack_into('<i', e, 0x50, flags)
    struct.pack_into('<i', e, 0x54, 0)
    struct.pack_into('<i', e, 0x58, owner)
    struct.pack_into('<i', e, 0x5c, -1)
    struct.pack_into('<i', e, 0x60, sub)
    struct.pack_into('<i', e, 0x64, 0)
    struct.pack_into('<i', e, 0x68, -1)
    struct.pack_into('<i', e, 0x6c, status)
    struct.pack_into('<4f', e, 0x80, 0, 0, 0, 1.0)
    p.write_bytes(pool + idx * 0xa0, bytes(e))
    p.write(pool + 0x3c00 + idx * 4, 1)


def main():
    args = sys.argv[1:]
    opts = {}
    for key in ('--near', '--dist', '--hits'):
        if key in args:
            i = args.index(key)
            opts[key] = args[i + 1]
            del args[i:i + 2]
    ticks = int(args[0]) if args else 80
    aseed = int(args[1], 0) if len(args) > 1 else 12345
    dist = float(opts.get('--dist', 120))
    hits = []
    for spec in opts.get('--hits', '12:5:0:0:0:1').split(','):
        t, dmg, flags, owner, sub, status = (int(x, 0) for x in spec.split(':'))
        hits.append(dict(tick=t, damage=dmg, flags=flags, owner=owner, sub=sub, status=status))
    with Pine() as p:
        p.load_state(1)
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
        status_file = os.path.join(WORK, 'user_status.bin')
        snapshot_status(p, status_file)
        install_probe(p)
        cave.call_batch([dict(func=SRAND, a=(aseed,))])
        cave.call_batch([dict(func=BT_ARRENGE)], timeout=20.0)
        slots0 = [slot_state(p, unit, k) for k in range(16)]
        live = [k for k in range(16) if slots0[k]['state'] != -1]
        target = int(opts['--near']) if '--near' in opts else live[0]
        tp = slots0[target]['pos']
        fr = p.read(CHARA_FRAME)
        for addr in (fr + 0x220, PLAYER_VEC):
            p.write_bytes(addr, struct.pack('<3f', tp[0] + dist, tp[1], tp[2]))
        pool = p.read(NOW_COL)
        rs = lambda: p.read(p.read(IMPURE) + 0x58)
        feed = [(*vec(p, PLAYER_VEC), rs(), 0)]
        step_info, states, extras, colpos = [], [], [], []
        last_count = 0
        injected = []
        for t in range(ticks):
            for h in hits:
                if h['tick'] == t:
                    # a sphere right on the target's first body sphere
                    base = unit + 0x55250 + target * 0x510
                    pos = struct.unpack('<3f', p.read_bytes(base, 12))
                    if p.read(unit + 0x55450 + target * 0x510) == 0:
                        print('target has no body sphere yet at tick', t)
                    radius = 8.0
                    write_entry(p, pool, 0x5f, pos, radius, h['damage'], h['flags'], h['owner'], h['sub'], h['status'])
                    injected.append((t, pos, radius, h))
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
            extras.append([dict(hit_stop=i32(p, unit + 0x1e3d0 + k * 400 + 8), poison=i32(p, unit + 0x1e3d0 + k * 400 + 0xc), bin2=i32(p, unit + 0x1e3d0 + k * 400 + 0x10),
                                slow=i32(p, unit + 0x1e3d0 + k * 400 + 0x14), killer=i32(p, unit + 0x1e3d0 + k * 400 + 4)) for k in range(16)])
            cols = {}
            for k in range(16):
                if states[-1][k]['state'] != 2:
                    continue
                for i in range(16):
                    if p.read(unit + 0x55450 + k * 0x510 + i * 4):
                        cols[(k, 'b', i)] = struct.unpack('<3f', p.read_bytes(unit + 0x55250 + k * 0x510 + i * 0x10, 12))
                    if p.read(unit + 0x5a650 + k * 0x350 + i * 4):
                        cols[(k, 'd', i)] = struct.unpack('<3f', p.read_bytes(unit + 0x5a350 + k * 0x350 + i * 0x10, 12))
            colpos.append(cols)
            feed.append((*vec(p, PLAYER_VEC), rs(), 0))
        p.write(gate, 0)
    feed_file = os.path.join(WORK, 'monster_feed.txt')
    with open(feed_file, 'w') as f:
        for t in range(ticks):
            px, py, pz = feed[t][:3]
            r, h, _ = step_info[t]
            f.write('%.9g %.9g %.9g %d %d\n' % (px, py, pz, r, h))
    init_file = os.path.join(WORK, 'monster_init.txt')
    with open(init_file, 'w') as f:
        for k in range(16):
            f.write('%d %.9g %.9g\n' % (k, slots0[k]['height'], slots0[k]['floor_y']))
    cmd = [EXE, ISO, 'd01main_a', str(seed), str(rooms), str(mapno), str(floor), str(ticks), '--arrange-seed', str(aseed), '--feed', feed_file, '--init', init_file, '--status', status_file]
    for t, pos, radius, h in injected:
        cmd += ['--inject', '%d,%.9g,%.9g,%.9g,%.9g,%d,%d,%d,%d,2,2,%d,1' % (t, pos[0], pos[1], pos[2], radius, h['damage'], h['flags'], h['owner'], h['sub'], h['status'])]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.split(chr(10))
    mine, hurt, events, sim_draws, mycol = {}, {}, [], {}, {}
    for line in out:
        parts = line.replace('|', ' ').split()
        if line.startswith('draws '):
            sim_draws[int(parts[1])] = int(parts[2])
        if line.startswith('bcol ') or line.startswith('dcol '):
            mycol[(int(parts[1]), int(parts[2]), 'b' if line[0] == 'b' else 'd', int(parts[3]))] = tuple(map(float, parts[4:7]))
        elif line.startswith('hurt '):
            hurt[(int(parts[1]), int(parts[2]))] = tuple(int(x) for x in parts[3:7])
        elif line.startswith('ev '):
            events.append(tuple(int(x) for x in parts[1:6]))
        elif parts and parts[0].isdigit() and len(parts) >= 20:
            mine[(int(parts[0]), int(parts[1]))] = dict(state=int(parts[2]), pos=tuple(map(float, parts[3:6])), req=int(parts[9]), hp=int(parts[13]), pc=int(parts[19]))
    print('target slot %d, hits injected: %s' % (target, [(t, h['damage'], h['flags']) for t, _, _, h in injected]))
    bad = 0
    for t in range(ticks):
        for k in range(16):
            g, x, m = states[t][k], extras[t][k], mine.get((t, k))
            if m is None or (g['state'] != 2 and m['state'] != 2 and g['state'] == m['state']):
                continue
            mh = hurt.get((t, k))
            diffs = []
            if g['state'] != m['state']: diffs.append('state %d/%d' % (g['state'], m['state']))
            if g['hp'] != m['hp']: diffs.append('hp %d/%d' % (g['hp'], m['hp']))
            if g['req'][2] != m['req']: diffs.append('req %d/%d' % (g['req'][2], m['req']))
            if g['pc'] != m['pc']: diffs.append('pc %#x/%#x' % (g['pc'], m['pc']))
            if mh and (x['hit_stop'], x['poison'], x['bin2'], x['slow']) != mh: diffs.append('status %s/%s' % ((x['hit_stop'], x['poison'], x['bin2'], x['slow']), mh))
            if max(abs(a - b) for a, b in zip(g['pos'], m['pos'])) > 0.05: diffs.append('pos')
            if diffs:
                bad += 1
                if bad <= 12:
                    print('tick %d slot %d: %s' % (t, k, ', '.join(diffs)))
    print('mismatching (tick, slot) pairs:', bad)
    def lcg_steps(a, b, limit=500):
        x = a
        for n in range(limit):
            if x == b:
                return n
            x = (x * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
        return None
    bad_draws = [(t, lcg_steps(step_info[t][0], step_info[t][2]), sim_draws.get(t)) for t in range(ticks) if lcg_steps(step_info[t][0], step_info[t][2]) != sim_draws.get(t)]
    print('rand draws differ in %d of %d ticks%s' % (len(bad_draws), ticks, (', first: tick %d game %s port %s' % bad_draws[0]) if bad_draws else ''))
    for t in range(max(0, injected[0][0] - 1) if injected else 0, min(ticks, (injected[0][0] + 8) if injected else 0)):
        g = states[t][target]
        print('   tick %d game: hp %d state %d req %s mtime %.2f pc %#x %s' % (t, g['hp'], g['state'], g['req'], g['mtime'], g['pc'], extras[t][target]))
        m = mine.get((t, target))
        if m:
            print('   tick %d port: hp %d state %d req %d pc %#x %s' % (t, m['hp'], m['state'], m['req'], m['pc'], hurt.get((t, target))))
    print('events (tick kind slot value value2):', events[:12])
    # world positions of the body / attack spheres (the game computes them when it draws the monster; the port after its motion step)
    worst, worst_at, compared = {'b': 0.0, 'd': 0.0}, {}, 0
    for t in range(1, ticks):
        for (k, kind, i), gpos in colpos[t].items():
            m = mycol.get((t, k, kind, i))
            if m is None:
                continue
            compared += 1
            d = max(abs(a - b) for a, b in zip(gpos, m))
            if d > worst[kind]:
                worst[kind] = d
                worst_at[kind] = (t, k, i, gpos, m)
    print('sphere positions compared %d: worst body %.3f, worst attack %.3f' % (compared, worst['b'], worst['d']))
    for kind in worst_at:
        print('   worst %s at tick %d slot %d sphere %d: game %s port %s' % ((kind,) + worst_at[kind][:3] + (tuple(round(x, 3) for x in worst_at[kind][3]), tuple(round(x, 3) for x in worst_at[kind][4]))))


if __name__ == '__main__':
    main()
