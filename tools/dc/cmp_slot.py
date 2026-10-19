"""Print the first ticks where a slot's position differs between work/monster_live.json (game) and the C++ port.  usage: cmp_slot.py slot ticks [eps]"""
import json, os, subprocess, sys
slot, ticks = int(sys.argv[1]), int(sys.argv[2])
eps = float(sys.argv[3]) if len(sys.argv) > 3 else 1e-5
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
exe = os.path.abspath(os.path.join(root, 'native/build/core_release/dcmonster.exe'))
iso = os.path.abspath(os.path.join(root, 'iso/Dark Cloud (USA).iso'))
os.chdir(root)
st = json.load(open('work/monster_live.json'))['states']
out = subprocess.run([exe, iso, 'd01main_a', '8097', '6', '0', '0', str(ticks), '--arrange-seed', '12345', '--feed', 'work/monster_feed.txt', '--init', 'work/monster_init.txt'], capture_output=True, text=True).stdout.split('\n')
mine = {}
for l in out:
    p = l.replace('|', ' ').split()
    if p and p[0].isdigit() and len(p) >= 20: mine[(int(p[0]), int(p[1]))] = p
shown = 0
for t in range(min(ticks, len(st))):
    g = st[t][slot]; m = mine.get((t, slot))
    if not m: continue
    mp = [float(m[3]), float(m[4]), float(m[5])]
    if max(abs(a - b) for a, b in zip(g['pos'], mp)) > eps:
        print(t, 'G', g['pos'], g['speed'], g['dir'], g['pc'], g['req'], '| P', m[3:6], m[7], m[16:19], m[19])
        shown += 1
        if shown >= 6: break
