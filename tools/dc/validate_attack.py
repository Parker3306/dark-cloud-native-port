"""Lock-step check of Toan's sword (attack states, hit spheres, gauge, lunge, damage dealt) against the running game.
usage: validate_attack.py [ticks] [arrange_seed] [--near SLOT] [--dist D] [--presses t,t,...] [--hold N] [--reuse]
record_attack.py plays the live game (OK button pressed at the given ticks through the patched pad getters) and saves work/attack_rec.json; the port (dcmonster --sim-player
--press) is run from the recorded start state with the recorded rand states and the two are compared tick by tick. --reuse compares against the last recording."""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.join(HERE, '..', '..')
WORK = os.path.join(ROOT, 'work')
ISO = os.path.join(ROOT, 'iso', 'Dark Cloud (USA).iso')
EXE = os.path.join(ROOT, 'native', 'build', 'core_release', 'dcmonster.exe')
NL = chr(10)


def write_inputs(rec):
    ticks = len(rec['ticks'])
    start, cam, sv = rec['start'], rec['cam'], rec['start_vars']
    state_file = os.path.join(WORK, 'player_state.txt')
    with open(state_file, 'w') as f:
        f.write('ppos %.9g %.9g %.9g' % tuple(start['pos']) + NL)
        f.write('pvy %.9g' % rec['vy'] + NL + 'pyaw %.9g' % start['yaw'] + NL)
        for k, v in cam.items():
            f.write('%s %s' % (k, ' '.join('%.9g' % x for x in (v if isinstance(v, (list, tuple)) else (v,)))) + NL)
        f.write('pmotion %s' % ' '.join('%.9g' % x for x in rec['motion_state']) + NL)
        f.write('preq %.9g %d %d' % tuple(start['req']) + NL)
        f.write('pcombat %d %d %d %d %d %d' % (start['mode'], start['action'], start['wish'], start['invuln'], start['busy'], start['requested']) + NL)
        for a_, v_ in rec.get('statics', {}).items():
            f.write('pstatic %d %d' % (int(a_, 16), v_) + NL)
        for i, (a, b, sp) in enumerate(rec['motion_table']):
            f.write('pspeed %d %.9g' % (i, sp) + NL)
        f.write('pattack %.9g %d %d %d %d %.9g %d %.9g %d %d %d %.9g %.9g %.9g %.9g %.9g %d %d %d' % (
            sv['gauge'], sv['cnt4578'], sv['cnt4560'], sv['flag4568'], sv['flag4570'], sv['rot4574'], sv['rel4498'], sv['charge449c'], sv['flag44dc'], sv['cd45cc'], sv['hit45ca'], sv['lunge'],
            sv['lunge_dec'], sv['lunge_vec'][0], sv['lunge_vec'][1], sv['lunge_vec'][2], sv['cd45c8'], sv['gauge_full'], sv['canact']) + NL)
    feed_file = os.path.join(WORK, 'monster_feed.txt')
    with open(feed_file, 'w') as f:
        for e in rec['ticks']:
            r, h, _ = e['rand']
            f.write('0 0 0 %d %d' % (r, h) + NL)
    init_file = os.path.join(WORK, 'monster_init.txt')
    with open(init_file, 'w') as f:
        for k in range(16):
            f.write('%d %.9g %.9g' % (k, rec['slots_start'][k]['height'], rec['slots_start'][k]['floor_y']) + NL)
    return state_file, feed_file, init_file


def run_port(rec, state_file, feed_file, init_file):
    ticks = len(rec['ticks'])
    press = ','.join(str(x) for x in rec['presses'])
    cmd = [EXE, ISO, 'd01main_a', str(rec['seed']), str(rec['rooms']), str(rec['mapno']), str(rec['floor']), str(ticks), '--arrange-seed', str(rec['aseed']), '--feed', feed_file, '--init', init_file,
           '--status', os.path.join(WORK, 'user_status.bin'), '--sim-player', state_file, '--press', press, '--hold', str(rec['hold']), '--no-floor', '2']
    if rec.get('btn'):
        cmd += ['--btn', rec['btn']]
    if rec.get('sticks'):
        cmd += ['--stick', ','.join('%d-%d:%d:%d' % tuple(x) for x in rec['sticks'])]
    if rec.get('bring'):
        cmd += ['--monster-at', '%d,%.9g,%.9g,%.9g' % tuple(rec['bring']), '--no-floor', '0']
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.split(NL)
    open(os.path.join(WORK, 'sim_out.txt'), 'w').write(NL.join(out))
    port, pvars, psets, mon = {}, {}, {}, {}
    gold, drops = {}, {}
    for line in out:
        parts = line.replace('|', ' ').split()
        if line.startswith('player '):
            port[int(parts[1])] = dict(pos=tuple(map(float, parts[2:5])), yaw=float(parts[5]), life=int(parts[6]), mode=int(parts[7]), action=int(parts[8]), wish=int(parts[9]), invuln=int(parts[10]),
                                       mtime=float(parts[12]), mcur=int(parts[13]))
        elif line.startswith('pvars '):
            pvars[int(parts[1])] = [float(x) for x in parts[2:]]
        elif line.startswith('gold '):
            gold[int(parts[1])] = tuple(int(x) for x in parts[2:5])
        elif line.startswith('drop '):
            drops.setdefault(int(parts[1]), []).append(dict(i=int(parts[2]), pos=tuple(map(float, parts[3:6])), gold=int(parts[6]), item=int(parts[7]), state=int(parts[8]), timer=float(parts[9]), block=int(parts[10])))
        elif line.startswith('set '):
            psets.setdefault(int(parts[1]), []).append(dict(pos=tuple(map(float, parts[2:5])), damage=int(parts[5]), radius=float(parts[6])))
        elif parts and parts[0].isdigit() and len(parts) >= 20:
            mon[(int(parts[0]), int(parts[1]))] = dict(state=int(parts[2]), pos=tuple(map(float, parts[3:6])), hp=int(parts[13]), pc=int(parts[19]), req=int(parts[9]))
    return port, pvars, psets, mon, gold, drops


def main():
    args = sys.argv[1:]
    reuse = '--reuse' in args
    if reuse:
        args.remove('--reuse')
    if not reuse:
        sys.argv = [sys.argv[0]] + args
        import record_attack
        record_attack.main()
    rec = json.load(open(os.path.join(WORK, 'attack_rec.json')))
    ticks = len(rec['ticks'])
    files = write_inputs(rec)
    port, pvars, psets, mon, pgold, pdrops = run_port(rec, *files)
    gsets = {e['t']: [s for s in e['sets'] if 0x241690 <= s['ra'] <= 0x245000 or 0x001AC180 <= s['ra'] < 0x001ACC00 or 0x001ABD10 <= s['ra'] < 0x001ABF20 or 0x001AE750 <= s['ra'] < 0x001AE980 or 0x001AEFA0 <= s['ra'] < 0x001AF1E0] for e in rec['ticks']}
    issues = 0
    first = {}

    def note(kind, t, text):
        nonlocal issues
        issues += 1
        if kind not in first:
            first[kind] = t
            print('first %s deviation at tick %d: %s' % (kind, t, text))

    worst_set = 0.0
    for e in rec['ticks']:
        t = e['t']
        g, m = e['player'], port.get(t)
        if m is None:
            continue
        for key in ('mode', 'action', 'wish', 'mcur'):
            if g[key] != m[key]:
                note(key, t, '%s game %s port %s' % (key, g[key], m[key]))
        if abs(g['mtime'] - m['mtime']) > 0.05:
            note('mtime', t, 'game %.2f port %.2f' % (g['mtime'], m['mtime']))
        d = max(abs(g['pos'][0] - m['pos'][0]), abs(g['pos'][2] - m['pos'][2]))
        if d > 0.02:
            note('pos', t, 'game %s port %s' % (tuple(round(x, 3) for x in g['pos']), tuple(round(x, 3) for x in m['pos'])))
        v, pv = e['vars'], pvars.get(t)
        if pv:
            pairs = [('gauge', v['gauge'], pv[0]), ('combo_window', v['cnt4578'], pv[1]), ('idle', v['cnt4560'], pv[2]), ('in_swing', v['flag4568'], pv[3]), ('chain', v['flag4570'], pv[4]),
                     ('released', v['rel4498'], pv[6]), ('cooldown', v['cd45cc'], pv[8]), ('lunge', v['lunge'], pv[10])]
            for name, a, b in pairs:
                if abs(a - b) > 0.01:
                    note(name, t, 'game %s port %s' % (a, b))
        gs, ps = gsets.get(t, []), psets.get(t, [])
        if len(gs) != len(ps):
            note('sets', t, 'game %d port %d' % (len(gs), len(ps)))
        else:
            for a, b in zip(gs, ps):
                dd = max(abs(x - y) for x, y in zip(a['pos'][:3], b['pos']))
                worst_set = max(worst_set, dd)
                if dd > 0.05 or a['damage'] != b['damage'] or abs(a['radius'] - b['radius']) > 1e-3:
                    note('set', t, 'game %s port %s' % (a, b))
    for e in rec['ticks']:
        t = e['t']
        pg = pgold.get(t)
        if pg and (pg[0] != e['gold'] or pg[1] != e['wexp'] or pg[2] != e['bag0']):
            note('gold/exp/bag', t, 'game gold %d exp %d bag0 %d port %s' % (e['gold'], e['wexp'], e['bag0'], pg))
        gd = sorted(e['drops'], key=lambda x: x['i'])
        pd = sorted(pdrops.get(t, []), key=lambda x: x['i'])
        if len(gd) != len(pd) or any((a['i'], a['gold'], a['item'], a['state'], a['block']) != (b['i'], b['gold'], b['item'], b['state'], b['block']) or max(abs(x - y) for x, y in zip(a['pos'], b['pos'])) > 0.02 or abs(a['timer'] - b['timer']) > 1e-4 for a, b in zip(gd, pd)):
            note('drops', t, 'game %s port %s' % (gd, pd))
    tgt = rec['target']
    for e in rec['ticks']:
        t = e['t']
        gm_, pm = e['monsters'][tgt], mon.get((t, tgt))
        if pm is None or gm_['state'] != 2:
            continue
        if gm_['hp'] != pm['hp']:
            note('target_hp', t, 'game %d port %d' % (gm_['hp'], pm['hp']))
        if max(abs(a - b) for a, b in zip(gm_['pos'], pm['pos'])) > 0.05:
            note('target_pos', t, 'game %s port %s' % (tuple(round(x, 3) for x in gm_['pos']), tuple(round(x, 3) for x in pm['pos'])))
    print('ticks %d, sword spheres: worst position difference %.4f; %d deviations' % (ticks, worst_set, issues))
    if issues:
        for kind, t in sorted(first.items(), key=lambda x: x[1])[:1]:
            for tt in range(max(0, t - 2), min(ticks, t + 3)):
                e = rec['ticks'][tt]
                print('  tick %d game: mode %d act %d wish %d mt %.2f cur %d | gauge %.1f lunge %.3f chain %d inswing %d | sets %s' % (
                    tt, e['player']['mode'], e['player']['action'], e['player']['wish'], e['player']['mtime'], e['player']['mcur'], e['vars']['gauge'], e['vars']['lunge'], e['vars']['flag4570'],
                    e['vars']['flag4568'], [(round(s['pos'][0], 2), s['damage']) for s in gsets[tt]]))
                m = port.get(tt)
                if m:
                    pv = pvars.get(tt, [0] * 20)
                    print('  tick %d port: mode %d act %d wish %d mt %.2f cur %d | gauge %.1f lunge %.3f chain %d inswing %d | sets %s' % (
                        tt, m['mode'], m['action'], m['wish'], m['mtime'], m['mcur'], pv[0], pv[10], pv[4], pv[3], [(round(s['pos'][0], 2), s['damage']) for s in psets.get(tt, [])]))


if __name__ == '__main__':
    main()
