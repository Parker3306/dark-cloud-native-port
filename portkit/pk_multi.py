"""portkit multi: run several instances of a program together (a co-op host and guests, a server and clients), each with its own arguments and window place, collect their logs and
check them. The same scenario serves as a regression check (`multi run name`, exits 0 / 1) and as an interactive session (`multi run name --interactive`: windows stay open for a human).

  multi list | run NAME [--interactive] | stop [NAME] | logs NAME [ROLE] [--tail N]

Manifest:
[multi_layout]                 # optional: screen size for the {x} {y} {w} {h} placeholders (instances side by side, left to right)
screen = [1920, 1080]
top = 40
[[multi]]
name = "coop_boss"
desc = "..."
timeout = 180                  # seconds until the instances are killed (non-interactive); default 180
start_gap = 4                  # seconds between the starts (a host must be up before its guests); default 3
done_regex = "LEFT|FINISHED"   # optional: stop as soon as every instance's log matched it
expect = [ {role = "host", regex = "COOP peers 1"}, {role = "guest", regex = "STARTED", count = 1} ]
forbid = ["SCRIPT ERROR", "Traceback"]
[[multi.instance]]
role = "host"
cmd = "\\"$GODOT\\" --path game --position {x},{y} --resolution {w}x{h} res://play.tscn -- --coop host"
env = { KEY = "value" }
[[multi.instance]]
role = "guest"
cmd = "..."
Placeholders in cmd: {role} {index} {n} {x} {y} {w} {h} {log} {dir}.
"""
import json
import os
import re
import subprocess
import sys
import time

import pk_check


def _scenario(manifest, name):
    for s in manifest.get('multi', []):
        if s['name'] == name:
            return s
    raise SystemExit('portkit multi: no scenario %r (python portkit/portkit.py multi list)' % name)


def _geometry(manifest, i, n):
    lay = manifest.get('multi_layout', {})
    sw, sh = lay.get('screen', [1920, 1080])
    top = lay.get('top', 40)
    w = sw // max(n, 1)
    h = min(sh - top - 40, int(w * 9 / 16) + 40)
    return i * w, top, w, h


def _kill(pid):
    if os.name == 'nt':
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(pid)], capture_output=True)
    else:
        try:
            os.kill(pid, 9)
        except OSError:
            pass


def _alive(pid):
    if os.name == 'nt':
        r = subprocess.run(['tasklist', '/FI', 'PID eq %d' % pid, '/NH'], capture_output=True, text=True)
        return str(pid) in r.stdout
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def _dir(manifest, name):
    d = os.path.join(manifest['_root'], '.portkit', 'multi', name)
    os.makedirs(d, exist_ok=True)
    return d


def stop(manifest, name=None):
    base = os.path.join(manifest['_root'], '.portkit', 'multi')
    names = [name] if name else (os.listdir(base) if os.path.isdir(base) else [])
    n = 0
    for nm in names:
        f = os.path.join(base, nm, 'pids.json')
        if os.path.isfile(f):
            for pid in json.load(open(f)):
                if _alive(pid):
                    _kill(pid)
                    n += 1
            os.remove(f)
    print('stopped %d instance(s)' % n)


def run_scenario(manifest, name, interactive=False):
    s = _scenario(manifest, name)
    root = manifest['_root']
    stop(manifest, name)
    d = _dir(manifest, name)
    project = manifest.get('project', {})
    shell = s.get('shell', manifest.get('defaults', {}).get('shell', project.get('shell')))
    base_env = dict(os.environ)
    base_env.update({k: str(v) for k, v in project.get('env', {}).items()})
    insts = s.get('instance', [])
    n = len(insts)
    procs = []
    logs = {}
    print('portkit multi: %s (%d instances%s)' % (name, n, ', interactive' if interactive else ''))
    for i, inst in enumerate(insts):
        x, y, w, h = _geometry(manifest, i, n)
        role = inst.get('role', 'p%d' % i)
        log = os.path.join(d, role + '.log')
        logs[role] = log
        cmd = inst['cmd']
        for k, v in dict(role=role, index=i, n=n, x=x, y=y, w=w, h=h, log=log.replace(os.sep, '/'), dir=d.replace(os.sep, '/')).items():
            cmd = cmd.replace('{' + k + '}', str(v))
        env = dict(base_env)
        env.update({k: str(v) for k, v in inst.get('env', {}).items()})
        argv = pk_check._shell_prefix(shell) + [cmd]
        cwd = os.path.normpath(os.path.join(root, inst.get('cwd', s.get('cwd', '.'))))
        f = open(log, 'wb')
        procs.append((role, subprocess.Popen(argv, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL), f))
        print('  started %-8s pid %d  log %s' % (role, procs[-1][1].pid, os.path.relpath(log, root)))
        sys.stdout.flush()
        if i < n - 1:
            time.sleep(float(s.get('start_gap', 3)))
    json.dump([p.pid for _, p, _ in procs], open(os.path.join(d, 'pids.json'), 'w'))
    if interactive:
        print('windows stay open; stop them with: python portkit/portkit.py multi stop %s' % name)
        return 0
    timeout = float(s.get('timeout', 180))
    deadline = time.time() + timeout
    done_re = s.get('done_regex')
    while time.time() < deadline:
        if all(p.poll() is not None for _, p, _ in procs):
            break
        if done_re and all(re.search(done_re, pk_check._read(logs[r]), re.M) for r, _, _ in procs):
            break
        time.sleep(1.0)
    timed_out = [r for r, p, _ in procs if p.poll() is None]
    for role, p, f in procs:
        if p.poll() is None:
            _kill(p.pid)
        f.close()
    stop(manifest, name)
    texts = {r: pk_check._read(l) for r, l in logs.items()}
    failures = []
    for e in s.get('expect', []):
        roles = [e['role']] if e.get('role') else list(texts)
        for r in roles:
            cnt = len(re.findall(e['regex'], texts.get(r, ''), re.M))
            if cnt < int(e.get('count', 1)):
                failures.append('%s: expected /%s/ x%d, found %d' % (r, e['regex'], int(e.get('count', 1)), cnt))
    for pat in s.get('forbid', []):
        for r, t in texts.items():
            m = re.search(pat, t, re.M)
            if m:
                failures.append('%s: forbidden /%s/ found: %s' % (r, pat, m.group(0).strip()[:80]))
    if timed_out and not done_re and not s.get('kill_ok', False):
        failures.append('still running after %ss: %s (set done_regex or kill_ok = true when that is intended)' % (int(timeout), ', '.join(timed_out)))
    for r, t in texts.items():
        print('  %-8s %d lines' % (r, t.count('\n')))
    if failures:
        print('FAIL: ' + name)
        for f in failures:
            print('  - ' + f)
        print('logs: ' + os.path.relpath(d, root))
        return 1
    print('ok: ' + name)
    return 0


def run(manifest, args):
    if args.action == 'list':
        for s in manifest.get('multi', []):
            print('%-20s %d instances  %s' % (s['name'], len(s.get('instance', [])), s.get('desc', '')[:80]))
        return 0
    if args.action == 'stop':
        stop(manifest, args.name)
        return 0
    if args.action == 'logs':
        d = _dir(manifest, args.name)
        for f in sorted(os.listdir(d)):
            if f.endswith('.log') and (not args.role or f == args.role + '.log'):
                print('--- ' + f)
                for line in pk_check._read(os.path.join(d, f)).splitlines()[-args.tail:]:
                    print('  ' + line[:200])
        return 0
    if not args.name:
        raise SystemExit('portkit multi run NAME')
    return run_scenario(manifest, args.name, args.interactive)
