"""Compares the script call streams (monster index, function id, argument slots) of the last recording (work/attack_rec.json) and of the port (dcmonster --extlog) tick by tick
and prints the first differences.   usage: diff_ext.py [ticks]"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
os.chdir(ROOT)
NL = chr(10)
r = json.load(open('work/attack_rec.json'))
ticks = int(sys.argv[1]) if len(sys.argv) > 1 else len(r['ticks'])
exe = os.path.abspath('native/build/core_release/dcmonster.exe')
iso = os.path.abspath('iso/Dark Cloud (USA).iso')
cmd = [exe, iso, 'd01main_a', str(r['seed']), str(r['rooms']), str(r['mapno']), str(r['floor']), str(ticks), '--arrange-seed', str(r['aseed']), '--feed', 'work/monster_feed.txt', '--init', 'work/monster_init.txt',
       '--status', 'work/user_status.bin', '--sim-player', 'work/player_state.txt', '--press', ','.join(str(x) for x in r['presses']), '--hold', str(r['hold']), '--no-floor', '2', '--extlog']
if r.get('bring'):
    cmd += ['--monster-at', '%d,%.9g,%.9g,%.9g' % tuple(r['bring']), '--no-floor', '0']
if r.get('monsters'):
    cmd += ['--monsters', ','.join(str(x) for x in r['monsters'])]
out = subprocess.run(cmd, capture_output=True, text=True).stdout.split(NL)
port = {}
for l in out:
    if l.startswith('xc '):
        p = l.split()
        port.setdefault(int(p[1]), []).append(p[2:])


def norm_game(c):
    sl = []
    n = c['n'] - 1
    for k in range(min(n, 9)):
        ty, v = c['slots'][k]
        sl.append('%d:%s' % (ty, '0' if ty == 3 else ('%08x' % v if ty == 1 else str(v if v < 2 ** 31 else v - 2 ** 32))))
    return [str(c['mon']), str(c['id']), str(n)] + sl


shown = 0
for t in range(ticks):
    g = [norm_game(c) for c in r['ticks'][t]['ext']]
    p = port.get(t, [])
    if g != p:
        print('tick', t, 'game', len(g), 'calls, port', len(p))
        for i in range(max(len(g), len(p))):
            a = g[i] if i < len(g) else None
            b = p[i] if i < len(p) else None
            if a != b:
                print('  first difference at call', i, NL + '    game', a, NL + '    port', b)
                print('  previous calls:', [x[:3] for x in g[max(0, i - 3):i]])
                break
        shown += 1
        if shown >= 2:
            break
if not shown:
    print('script calls identical for', ticks, 'ticks')
