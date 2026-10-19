"""Lock-step check of the player getting hurt (BtCheckDamageProc, hit reactions, smoothly draining life, knock-back, motion) against the running game.
usage: validate_hurt.py [ticks] [arrange_seed] [--near SLOT] [--dist D]
The player stands still (no input); monsters come and attack. The port simulates the player too (dcmonster --sim-player) from the state recorded after the monsters were
arranged, with the recorded rand state fed in at every Step entry; life, reaction mode, motion and position are compared tick by tick."""
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from validate_monster import *   # noqa: F401,F403
import validate_monster as vm

NOW_CAMERA = 0x002A3498
PLAYER_CHAR = 0x01EA1D20
MODE, ACTION, WISH, INVULN, BUSY, REQUESTED = 0x01DC4490, 0x01DC4494, 0x01DC448C, 0x01DC44A0, 0x01DC44A4, 0x01DC4484
BLOW, VY = 0x01DC2570, 0x01DC2554


def cam_state(p):
    cam = p.read(NOW_CAMERA)
    return dict(cpos=vec(p, cam + 0x260), cref=vec(p, cam + 0x270), cnext_pos=vec(p, cam + 0x280), cnext_ref=vec(p, cam + 0x290), cspeed=f32(p, cam + 0x2a8), cangle_h=f32(p, cam + 0x2ac),
                cfollow=vec(p, cam + 0x2c0), cdist=f32(p, cam + 0x2d0), cheight=f32(p, cam + 0x2d4), cangle=f32(p, cam + 0x2d8), cangle_cur=f32(p, cam + 0x2dc))


def player_state(p, us, fr):
    ch = p.read_bytes(us, 8)[4]
    return dict(
        pos=vec(p, fr + 0x220), yaw=f32(p, fr + 0x234), life=struct.unpack('<h', p.read_bytes(us + 0x12 + ch * 2, 2))[0],
        life_step=struct.unpack('<h', p.read_bytes(us + 0x8a68 + ch * 2, 2))[0], life_target=struct.unpack('<h', p.read_bytes(us + 0x8a5c + ch * 2, 2))[0],
        mode=i32(p, MODE), action=i32(p, ACTION), wish=i32(p, WISH), invuln=i32(p, INVULN), busy=i32(p, BUSY), requested=i32(p, REQUESTED),
        mtime=f32(p, PLAYER_CHAR + 0x2f0), mcur=i32(p, PLAYER_CHAR + 0x304), mprev=i32(p, PLAYER_CHAR + 0x308),
        req=(f32(p, PLAYER_CHAR + 0xc60), p.read(PLAYER_CHAR + 0xc64), i32(p, PLAYER_CHAR + 0xc68)), blow=vec(p, BLOW))


def main():
    args = sys.argv[1:]
    opts = {}
    for key in ('--near', '--dist'):
        if key in args:
            i = args.index(key)
            opts[key] = args[i + 1]
            del args[i:i + 2]
    ticks = int(args[0]) if args else 300
    aseed = int(args[1], 0) if len(args) > 1 else 12345
    dist = float(opts.get('--dist', 60))
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
        # let the game place the player on the floor, then take the starting state after the first tick so that both sides start from a settled frame
        status_file = os.path.join(WORK, 'user_status.bin')
        us_bytes = b''.join(p.read_bytes(us + off, 0x400) for off in range(0, 0x9000, 0x400))
        open(status_file, 'wb').write(us_bytes)
        start = player_state(p, us, fr)
        cam = cam_state(p)
        table = p.read(PLAYER_CHAR + 0x344)
        speeds = [f32(p, table + i * 0x10 + 8) for i in range(41)]
        motion_state = [f32(p, PLAYER_CHAR + 0x2f0), f32(p, PLAYER_CHAR + 0x2f4), f32(p, PLAYER_CHAR + 0x2f8), i32(p, PLAYER_CHAR + 0x2fc), i32(p, PLAYER_CHAR + 0x300),
                        i32(p, PLAYER_CHAR + 0x304), i32(p, PLAYER_CHAR + 0x308), i32(p, PLAYER_CHAR + 0x30c)]
        vy = f32(p, VY)
        slots_start = [slot_state(p, unit, k) for k in range(16)]
        rs = lambda: p.read(p.read(IMPURE) + 0x58)
        feed = [rs()]
        step_info, states, pstates, pools, dcols = [], [], [], [], []
        pool_base = p.read(0x002A35E0)
        last_count = p.read(PROBE_OUT + 8)
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
            step_info.append((r_step, hold_arg, r_exit) if count != last_count else (feed[-1], 1, feed[-1]))
            last_count = count
            states.append([slot_state(p, unit, k) for k in range(16)])
            pstates.append(player_state(p, us, fr))
            used = []
            flags = p.read_bytes(pool_base + 0x3c00, 96 * 4)
            for i in range(96):
                if struct.unpack_from('<i', flags, i * 4)[0]:
                    b = p.read_bytes(pool_base + i * 0xa0, 0xa0)
                    used.append((i, struct.unpack_from('<3f', b, 0), struct.unpack_from('<f', b, 0x3c)[0], struct.unpack_from('<i', b, 0x34)[0], struct.unpack_from('<i', b, 0x48)[0], struct.unpack_from('<i', b, 0x58)[0], struct.unpack_from('<i', b, 0x60)[0]))
            pools.append(used)
            cols = {}
            for k in range(16):
                if states[-1][k]['state'] != 2:
                    continue
                for i in range(16):
                    if p.read(unit + 0x5a650 + k * 0x350 + i * 4):
                        cols[(k, i)] = struct.unpack('<3f', p.read_bytes(unit + 0x5a350 + k * 0x350 + i * 0x10, 12))
            dcols.append(cols)
            feed.append(rs())
        p.write(gate, 0)
    state_file = os.path.join(WORK, 'player_state.txt')
    with open(state_file, 'w') as f:
        f.write('ppos %.9g %.9g %.9g\n' % start['pos'])
        f.write('pvy %.9g\npyaw %.9g\n' % (vy, start['yaw']))
        for k, v in cam.items():
            f.write('%s %s\n' % (k, ' '.join('%.9g' % x for x in (v if isinstance(v, tuple) else (v,)))))
        f.write('pmotion %s\n' % ' '.join('%.9g' % x for x in motion_state))
        f.write('preq %.9g %d %d\n' % start['req'])
        f.write('pcombat %d %d %d %d %d %d\n' % (start['mode'], start['action'], start['wish'], start['invuln'], start['busy'], start['requested']))
        for i, sp in enumerate(speeds):
            f.write('pspeed %d %.9g\n' % (i, sp))
    feed_file = os.path.join(WORK, 'monster_feed.txt')
    with open(feed_file, 'w') as f:
        for t in range(ticks):
            r, h, _ = step_info[t]
            f.write('0 0 0 %d %d\n' % (r, h))
    init_file = os.path.join(WORK, 'monster_init.txt')
    with open(init_file, 'w') as f:
        for k in range(16):
            f.write('%d %.9g %.9g\n' % (k, slots_start[k]['height'], slots_start[k]['floor_y']))
    cmd = [EXE, ISO, 'd01main_a', str(seed), str(rooms), str(mapno), str(floor), str(ticks), '--arrange-seed', str(aseed), '--feed', feed_file, '--init', init_file, '--status', status_file,
           '--sim-player', state_file]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.split(chr(10))
    open(os.path.join(WORK, 'sim_out.txt'), 'w').write(chr(10).join(out))
    mine = {}
    for line in out:
        parts = line.replace('|', ' ').split()
        if line.startswith('player '):
            mine[int(parts[1])] = dict(pos=tuple(map(float, parts[2:5])), yaw=float(parts[5]), life=int(parts[6]), mode=int(parts[7]), action=int(parts[8]), wish=int(parts[9]), invuln=int(parts[10]),
                                       busy=int(parts[11]), mtime=float(parts[12]), mcur=int(parts[13]), req=int(parts[14]), step=int(parts[15]), target=int(parts[16]), blow=(float(parts[17]), float(parts[18])))
    mon = {}
    mcol = {}
    for line in out:
        parts = line.replace('|', ' ').split()
        if line.startswith('dcol '):
            mcol[(int(parts[1]), int(parts[2]), int(parts[3]))] = tuple(map(float, parts[4:7]))
        elif parts and parts[0].isdigit() and len(parts) >= 20:
            mon[(int(parts[0]), int(parts[1]))] = dict(state=int(parts[2]), pos=tuple(map(float, parts[3:6])), req=int(parts[9]), hp=int(parts[13]), pc=int(parts[19]))
    for k in range(16):
        for t in range(ticks):
            m = mon.get((t, k))
            if m is None or states[t][k]['state'] != 2:
                continue
            g = states[t][k]
            d = max(abs(a - b) for a, b in zip(g['pos'], m['pos']))
            if d > 0.05 or g['req'][2] != m['req'] or g['pc'] != m['pc']:
                print('monster slot %d first deviates at tick %d: pos diff %.3f req %d/%d pc %#x/%#x' % (k, t, d, g['req'][2], m['req'], g['pc'], m['pc']))
                for tt in range(max(0, t - 6), t + 2):
                    gg, mm = states[tt][k], mon.get((tt, k))
                    print('     tick %d game pos %s speed %.4f | port pos %s' % (tt, tuple(round(x, 4) for x in gg['pos']), gg['speed'], tuple(round(x, 4) for x in mm['pos']) if mm else None))
                break
    worst_col = (0.0, None)
    for t in range(1, ticks):
        for (k, i), gpos in dcols[t].items():
            m = mcol.get((t, k, i))
            if m is None:
                continue
            d = max(abs(a - b) for a, b in zip(gpos, m))
            if d > worst_col[0]:
                worst_col = (d, (t, k, i, gpos, m))
    for k in range(16):
        row = []
        for w in range(0, ticks, 100):
            dm, ds = 0.0, 0.0
            for t in range(w, min(ticks, w + 100)):
                m = mon.get((t, k))
                if m is not None and states[t][k]['state'] == 2:
                    dm = max(dm, max(abs(a - b) for a, b in zip(states[t][k]['pos'], m['pos'])))
                for (kk, i), gpos in dcols[t].items():
                    if kk == k and mcol.get((t, k, i)) is not None:
                        ds = max(ds, max(abs(a - b) for a, b in zip(gpos, mcol[(t, k, i)])))
            row.append('%.3f/%.3f' % (dm, ds))
        if any(x != '0.000/0.000' for x in row):
            print('slot %d monster/sphere error per 100 ticks: %s' % (k, ' '.join(row)))
    for k in range(16):
        for t in range(1, ticks):
            m = mon.get((t, k))
            if m is None or states[t][k]['state'] != 2:
                continue
            d = max(abs(a - b) for a, b in zip(states[t][k]['pos'], m['pos']))
            if d > 0.00005:
                print('slot %d: error first exceeds 5e-5 at tick %d (%.4f)' % (k, t, d))
                for tt in range(max(1, t - 3), t + 2):
                    gg, mm = states[tt][k], mon.get((tt, k))
                    print('     tick %d game pos %s speed %.5f req %s | port pos %s' % (tt, tuple(round(x, 5) for x in gg['pos']), gg['speed'], gg['req'][2], tuple(round(x, 5) for x in mm['pos']) if mm else None))
                break
    print('attack sphere positions: worst difference %.4f at %s' % (worst_col[0], worst_col[1] and (worst_col[1][0], worst_col[1][1], worst_col[1][2])))
    print('floor seed %d, target monster slot %d, player start %s life %d mode %d' % (seed, target, tuple(round(x, 1) for x in start['pos']), start['life'], start['mode']))
    first_bad = None
    worst = 0.0
    for t in range(ticks):
        g, m = pstates[t], mine.get(t)
        if m is None:
            continue
        d = max(abs(a - b) for a, b in zip(g['pos'], m['pos']))
        worst = max(worst, d)
        diffs = []
        if g['life'] != m['life']: diffs.append('life %d/%d' % (g['life'], m['life']))
        if g['mode'] != m['mode']: diffs.append('mode %d/%d' % (g['mode'], m['mode']))
        if g['wish'] != m['wish']: diffs.append('wish %d/%d' % (g['wish'], m['wish']))
        if g['invuln'] != m['invuln']: diffs.append('invuln %d/%d' % (g['invuln'], m['invuln']))
        if abs(g['mtime'] - m['mtime']) > 0.05: diffs.append('mtime %.2f/%.2f' % (g['mtime'], m['mtime']))
        if g['mcur'] != m['mcur']: diffs.append('mcur %d/%d' % (g['mcur'], m['mcur']))
        if d > 0.05: diffs.append('pos %.3f' % d)
        if diffs and first_bad is None:
            first_bad = t
            print('first deviation at tick %d: %s' % (t, ', '.join(diffs)))
    hurt_ticks = [t for t in range(1, ticks) if pstates[t]['life'] < pstates[t - 1]['life'] or pstates[t]['mode'] != pstates[t - 1]['mode']]
    print('player: worst position difference %.4f; mode/life changes at ticks %s; first deviation: %s' % (worst, hurt_ticks[:20], first_bad))
    if first_bad is not None:
        for t in range(max(0, first_bad - 2), min(ticks, first_bad + 3)):
            print('   tick %d game pool: %s' % (t, [(i, tuple(round(x, 2) for x in pos), round(r, 1), dm, tg, ow, sb) for i, pos, r, dm, tg, ow, sb in pools[t]]))
            g, m = pstates[t], mine.get(t)
            print('   tick %d game: pos %s life %d (step %d tgt %d) mode %d act %d wish %d inv %d mtime %.2f cur %d req %s blow %s' % (t, tuple(round(x, 3) for x in g['pos']), g['life'], g['life_step'], g['life_target'], g['mode'], g['action'], g['wish'], g['invuln'], g['mtime'], g['mcur'], g['req'], tuple(round(x, 3) for x in g['blow'])))
            if m:
                print('   tick %d port: pos %s life %d (step %d tgt %d) mode %d act %d wish %d inv %d mtime %.2f cur %d req %d blow %s' % (t, tuple(round(x, 3) for x in m['pos']), m['life'], m['step'], m['target'], m['mode'], m['action'], m['wish'], m['invuln'], m['mtime'], m['mcur'], m['req'], m['blow']))


if __name__ == '__main__':
    main()
