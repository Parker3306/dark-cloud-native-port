"""Lock-step check of the monsters' shot effects (dccore/shots.cpp) against the running game: swaps the floor's monsters for the given ones (through the game's
BtLoadMonstor), records the hit spheres that Step__12CSHOT_EFFECT puts into the pool every tick, then replays the same player/rand feed through the port and compares.
usage: validate_shot.py <monster id> [ticks] [arrange_seed] [--dist D] [--near SLOT] [--reuse]"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import validate_attack as va

ROOT = va.ROOT
WORK = va.WORK
SHOT_LO, SHOT_HI = 0x001AC180, 0x001ACC00   # Step__12CSHOT_EFFECT
BOMB_LO, BOMB_HI = 0x001D5940, 0x001D5B30   # SetBombEffect (shots of type 100 end in an explosion)


def main():
    args = sys.argv[1:]
    reuse = '--reuse' in args
    if reuse:
        args.remove('--reuse')
    monster = int(args[0])
    ticks = args[1] if len(args) > 1 else '400'
    aseed = args[2] if len(args) > 2 else '99'
    extra = [a for a in args[3:]]
    rec_path = os.path.join(WORK, 'attack_rec.json')
    if not reuse:
        cmd = [sys.executable, os.path.join(HERE, 'record_attack.py'), ticks, aseed, '--monsters', str(monster), '--presses', '99999'] + extra
        if '--dist' not in extra:
            cmd += ['--dist', '60']
        print(' '.join(cmd))
        subprocess.run(cmd, check=True)
    rec = json.load(open(rec_path))
    state_file, feed_file, init_file = va.write_inputs(rec)
    n = len(rec['ticks'])
    cmd = [va.EXE, va.ISO, 'd01main_a', str(rec['seed']), str(rec['rooms']), str(rec['mapno']), str(rec['floor']), str(n), '--arrange-seed', str(rec['aseed']), '--feed', feed_file, '--init', init_file,
           '--status', os.path.join(WORK, 'user_status.bin'), '--sim-player', state_file, '--press', '99999', '--hold', str(rec['hold']), '--no-floor', '2', '--monsters', str(monster)]
    if rec.get('bring'):
        cmd += ['--monster-at', '%d,%.9g,%.9g,%.9g' % tuple(rec['bring']), '--no-floor', '0']
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.split(va.NL)
    open(os.path.join(WORK, 'sim_out.txt'), 'w').write(va.NL.join(out))
    mine = {}
    for line in out:
        parts = line.split()
        if parts and parts[0] == 'shotset':
            mine.setdefault(int(parts[1]), []).append(tuple(float(x) for x in parts[2:5]) + (int(parts[5]), float(parts[6]), int(parts[7]), int(parts[8]), int(parts[9])))
    game = {}
    for t, e in enumerate(rec['ticks']):
        for s in e['sets']:
            if SHOT_LO <= s['ra'] < SHOT_HI or BOMB_LO <= s['ra'] < BOMB_HI:
                game.setdefault(t, []).append(tuple(s['pos'][:3]) + (s['damage'], s['radius'], s['target'], s['kind'], s['life']))
    total_g = sum(len(v) for v in game.values())
    total_m = sum(len(v) for v in mine.values())
    print('shot hit spheres: game %d, port %d (monster %d, %d ticks)' % (total_g, total_m, monster, n))
    bad = 0
    worst = 0.0
    first = None
    for t in range(n):
        g, m = game.get(t, []), mine.get(t, [])
        if len(g) != len(m):
            bad += 1
            first = first or (t, len(g), len(m))
            continue
        for a, b in zip(sorted(g), sorted(m)):
            d = max(abs(x - y) for x, y in zip(a[:3], b[:3]))
            worst = max(worst, d)
            if a[3] != b[3] or abs(a[4] - b[4]) > 1e-3 or a[5:] != b[5:] or d > 0.02:
                bad += 1
                first = first or (t, a, b)
                break
    print('ticks that differ: %d, worst position difference %.4f, first: %s' % (bad, worst, first))


if __name__ == '__main__':
    main()
