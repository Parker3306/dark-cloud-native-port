"""portkit replay: a human plays a scene ONCE, portkit stores the controller input tick by tick, and from then on the same input is replayed headless as a regression test (the end state is
judged by log patterns, or by a screenshot compared with a baseline). Replaces writing a bot for every fight, walk or menu sequence.

  replay list | info NAME | record NAME | run [NAMES] [--tags a,b] | accept NAME [NAMES...]

How it works: `record` runs the case's `record_cmd` (an interactive scene) with the environment variable named in [replays] env_var set to the recording file; the GAME writes one line per tick
(`lx ly rx ry buttons menu`, integers, see `portkit/godot/pad_file.gd` and `dc_pad.h` for the Godot / C++ side). `run` substitutes {input} (the recording path) into `cmd`.

Manifest:
[replays]
dir = ".portkit/replays"          # recordings (a few KB each; they hold no game data and may be committed)
env_var = "PK_RECORD_PAD"         # the variable the game reads to know where to write
[[replay]]
name = "d02_boss_start"
desc = "walk to the boss marker, let the scene run, fight a few seconds"
record_cmd = "\\"$GODOT\\" --path game res://play.tscn -- --dungeon 1 --floor 16 --seed 7"      # interactive; closing the game ends the recording
cmd = "\\"$GODOT\\" --path game res://play.tscn -- --dungeon 1 --floor 16 --seed 7 --pad-file {input} --out {dir}/{name} --shots 1500"
out = "{dir}/{name}_t1500.png"    # optional: a screenshot case (compared with its baseline like `shots`); without `out` the case is a check:
pass_regex = ["BOSS DOWN"]        #   pass_regex / fail_regex / count_regex ... as in [[check]]
timeout = 300
tags = ["boss"]
"""
import os
import subprocess
import sys

import pk_check
import pk_shots


def _cfg(manifest):
    c = dict(manifest.get('replays', {}))
    c.setdefault('dir', os.path.join('.portkit', 'replays'))
    c.setdefault('env_var', 'PK_RECORD_PAD')
    c['dir_full'] = os.path.normpath(os.path.join(manifest['_root'], c['dir']))
    return c


def _case(manifest, name):
    for c in manifest.get('replay', []):
        if c['name'] == name:
            return c
    raise SystemExit('portkit replay: no case %r (python portkit/portkit.py replay list)' % name)


def _input_path(cfg, name):
    return os.path.join(cfg['dir_full'], name + '.pad')


def read_pad(path):
    ticks = []
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if line and not line.startswith('#'):
            ticks.append([int(v) for v in line.split()])
    return ticks


def info(path):
    ticks = read_pad(path)
    names = {0x1: 'L2', 0x2: 'R2', 0x4: 'L1', 0x8: 'R1', 0x10: 'Triangle', 0x20: 'Circle', 0x40: 'Cross', 0x80: 'Square', 0x100: 'Select', 0x200: 'L3', 0x400: 'R3', 0x800: 'Start',
             0x1000: 'Up', 0x2000: 'Right', 0x4000: 'Down', 0x8000: 'Left'}
    counts = {}
    moving = 0
    for t in ticks:
        b = t[4] | (t[5] if len(t) > 5 else 0)
        for bit, n in names.items():
            if b & bit:
                counts[n] = counts.get(n, 0) + 1
        if abs(t[0] - 128) > 40 or abs(t[1] - 128) > 40:
            moving += 1
    secs = len(ticks) / 60.0
    print('%d ticks (%.1f s at 60 Hz); stick moved in %d ticks; buttons (ticks held): %s' % (len(ticks), secs, moving, ', '.join('%s %d' % kv for kv in sorted(counts.items(), key=lambda kv: -kv[1])) or 'none'))


class _A:
    def __init__(self, **kw):
        self.names, self.tags, self.failed, self.list, self.json = kw.get('names', []), None, False, False, False
        self.stop_on_fail, self.excerpt, self.action, self.no_run, self.new, self.all_shots = False, 20, 'all', False, False, False
        for k, v in kw.items():
            setattr(self, k, v)


def run(manifest, args):
    cfg = _cfg(manifest)
    cases = manifest.get('replay', [])
    act = args.action
    if act == 'list':
        for c in cases:
            have = os.path.isfile(_input_path(cfg, c['name']))
            print('%-24s %-10s %s  %s' % (c['name'], 'recorded' if have else 'NO RECORDING', 'screenshot' if c.get('out') else 'check', c.get('desc', '')[:70]))
        return 0
    if act == 'info':
        p = _input_path(cfg, args.names[0])
        if not os.path.isfile(p):
            raise SystemExit('no recording for %s' % args.names[0])
        info(p)
        return 0
    if act == 'record':
        c = _case(manifest, args.names[0])
        os.makedirs(cfg['dir_full'], exist_ok=True)
        path = _input_path(cfg, c['name'])
        if os.path.exists(path):
            os.replace(path, path + '.bak')
        env = dict(os.environ)
        env.update({k: os.path.expandvars(str(v)) for k, v in manifest.get('project', {}).get('env', {}).items()})
        env[cfg['env_var']] = path
        shell = pk_check._shell_prefix(c.get('shell', manifest.get('project', {}).get('shell')))
        cmd = c['record_cmd']
        print('recording %s: play the scene, then close the game (input goes to %s)' % (c['name'], os.path.relpath(path, manifest['_root'])))
        subprocess.call(shell + [cmd], cwd=manifest['_root'], env=env)
        if not os.path.isfile(path):
            print('no recording was written: does the game read %s? (portkit/PORTKIT.md, section replay)' % cfg['env_var'])
            return 1
        info(path)
        return 0
    if act in ('run', 'accept'):
        wanted = set(args.names)
        sel = [c for c in cases if (not wanted or c['name'] in wanted) and (not getattr(args, 'tags', None) or set(args.tags.split(',')) & set(pk_check._as_list(c.get('tags'))))]
        checks, shots, skipped = [], [], []
        for c in sel:
            path = _input_path(cfg, c['name'])
            if not os.path.isfile(path):
                skipped.append(c['name'])
                continue
            d = dict(c)
            d['cmd'] = c['cmd'].replace('{input}', path.replace(os.sep, '/'))
            if c.get('out'):
                d['name'] = 'replay_' + c['name']
                shots.append(d)
            else:
                d['name'] = 'replay:' + c['name']
                checks.append(d)
        for n in skipped:
            print(' SKIP  %-22s no recording (python portkit/portkit.py replay record %s)' % (n, n))
        m = dict(manifest)
        rc = 0
        if act == 'accept':
            m['shot'] = shots
            return pk_shots.run(m, _A(action='accept', names=[], new=False, all_shots=True, tags=None))
        if checks:
            m['check'] = checks
            rc |= pk_check.run(m, _A(names=[]))
        if shots:
            m['shot'] = shots
            rc |= pk_shots.run(m, _A(action='all', names=[], tags=None))
        return rc
    return 2
