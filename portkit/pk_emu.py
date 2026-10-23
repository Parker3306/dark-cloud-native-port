"""portkit emu: start / stop / reset / health-check the project's own PCSX2 (found by its executable PATH, never by process name: other projects run their own emulators).

  emu status                 processes of this project's emulator, PINE answer, game, state (running / paused / shut down)
  emu start [--iso FILE]     start it (minimized) and wait until PINE answers
  emu stop                   stop only this project's emulator
  emu reset                  stop, start, load the save state, wait until the game runs (the fix for a crashed or hung emulator)
  emu ensure                 do nothing when the emulator is healthy, else reset (use it as `prep` / `before_retry` of live checks)
  emu load-state [N] | save-state [N]

Manifest section [emu]: exe, iso, args, cwd, minimized, pine_port, state_slot, boot_timeout, settle_seconds, ready_cmd (a command that exits 0 when the game is usable), drop_path (PATH entries to remove
for the emulator process only: e.g. a DLL that makes it fail to start).
"""
import os
import subprocess
import sys
import time

import pk_pine


def _cfg(manifest):
    c = dict(manifest.get('emu', {}))
    root = manifest['_root']
    if not c.get('exe'):
        raise SystemExit('portkit emu: [emu] exe is not set in portkit.toml')
    c['exe_full'] = os.path.normpath(os.path.join(root, c['exe']))
    c.setdefault('pine_port', 28011)
    c.setdefault('state_slot', 1)
    c.setdefault('boot_timeout', 120)
    c.setdefault('settle_seconds', 8)
    c.setdefault('minimized', True)
    return c


def find_pids(exe_full):
    """Process ids whose executable is exactly this file."""
    want = os.path.normcase(os.path.abspath(exe_full))
    pids = []
    if os.name == 'nt':
        name = os.path.basename(exe_full)
        ps = "Get-CimInstance Win32_Process -Filter \"Name='%s'\" | ForEach-Object { \"$($_.ProcessId)|$($_.ExecutablePath)\" }" % name
        r = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True)
        for line in r.stdout.splitlines():
            if '|' in line:
                pid, path = line.split('|', 1)
                if path.strip() and os.path.normcase(os.path.abspath(path.strip())) == want:
                    pids.append(int(pid))
    else:
        r = subprocess.run(['ps', '-eo', 'pid,args'], capture_output=True, text=True)
        for line in r.stdout.splitlines()[1:]:
            pid, _, args = line.strip().partition(' ')
            if args.split(' ')[0] and os.path.normcase(os.path.abspath(args.split(' ')[0])) == want:
                pids.append(int(pid))
    return pids


def pine_status(port, timeout=2.0):
    """('ok', status int, title) | ('nogame', ...) | ('down', ...). status: 0 running, 1 paused, 2 shut down."""
    try:
        with pk_pine.Pine(port, timeout=timeout) as p:
            try:
                return 'ok', p.status(), p.title()
            except pk_pine.PineError:
                return 'nogame', None, ''
    except OSError as e:
        return 'down', None, str(e)


def stop(c):
    pids = find_pids(c['exe_full'])
    for pid in pids:
        if os.name == 'nt':
            subprocess.run(['taskkill', '/T', '/F', '/PID', str(pid)], capture_output=True)
        else:
            os.kill(pid, 9)
    if pids:
        time.sleep(2.5)
    return pids


def start(c, root, iso=None):
    iso = iso or c.get('iso')
    argv = [c['exe_full']] + list(c.get('args', []))
    if iso:
        argv.append(os.path.normpath(os.path.join(root, iso)))
    env = dict(os.environ)
    drop = c.get('drop_path', [])
    if drop:
        env['PATH'] = os.pathsep.join(p for p in env.get('PATH', '').split(os.pathsep) if p and not any(d.lower() in p.lower() for d in drop))
    cwd = os.path.normpath(os.path.join(root, c['cwd'])) if c.get('cwd') else os.path.dirname(c['exe_full'])
    kw = {}
    if os.name == 'nt':
        si = subprocess.STARTUPINFO()
        if c['minimized']:
            si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            si.wShowWindow = 7   # SW_SHOWMINNOACTIVE
        kw['startupinfo'] = si
        kw['creationflags'] = subprocess.CREATE_NEW_PROCESS_GROUP | 0x00000008   # detached: it outlives this command
    proc = subprocess.Popen(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **kw)
    deadline = time.time() + c['boot_timeout']
    while time.time() < deadline:
        if proc.poll() is not None:
            raise SystemExit('portkit emu: the emulator exited at start-up (code %s)' % proc.returncode)
        st, status, _ = pine_status(c['pine_port'], timeout=1.0)
        if st == 'ok' and status == 0:
            return proc.pid
        time.sleep(1.0)
    raise SystemExit('portkit emu: PINE did not answer within %ss' % c['boot_timeout'])


def load_state(c, slot):
    deadline = time.time() + 30
    last = None
    while time.time() < deadline:
        try:
            with pk_pine.Pine(c['pine_port'], timeout=5.0) as p:
                p.load_state(slot)
                return True
        except (OSError, pk_pine.PineError) as e:
            last = e
            time.sleep(1.0)
    print('portkit emu: load-state failed (%s)' % last)
    return False


def ready(c, root):
    if c.get('ready_cmd'):
        shell = c.get('shell') or (['cmd', '/c'] if os.name == 'nt' else ['bash', '-c'])
        r = subprocess.run(shell + [c['ready_cmd']], cwd=root, capture_output=True, text=True, timeout=120)
        return r.returncode == 0
    st, status, _ = pine_status(c['pine_port'])
    return st == 'ok' and status == 0


def reset(c, root):
    stopped = stop(c)
    pid = start(c, root)
    if c.get('state_slot') is not None and not load_state(c, int(c['state_slot'])):
        return False
    time.sleep(float(c['settle_seconds']))
    ok = ready(c, root)
    print('portkit emu: reset %s (stopped %d, started pid %s, state %s)' % ('ok' if ok else 'FAILED: game not ready', len(stopped), pid, c['state_slot']))
    return ok


def run(manifest, args):
    c = _cfg(manifest)
    root = manifest['_root']
    act = args.action
    if act == 'status':
        pids = find_pids(c['exe_full'])
        st, status, title = pine_status(c['pine_port'])
        names = {0: 'running', 1: 'paused', 2: 'shut down'}
        print('emulator %s: %d process(es) %s; PINE %s%s' % (os.path.basename(c['exe_full']), len(pids), pids or '', st, (' (%s, %s)' % (names.get(status, status), title)) if st == 'ok' else ''))
        return 0 if (pids and st == 'ok') else 1
    if act == 'start':
        if find_pids(c['exe_full']):
            print('already running (use reset to restart)')
            return 0
        print('started pid', start(c, root, args.iso))
        return 0
    if act == 'stop':
        pids = stop(c)
        print('stopped %d process(es) %s' % (len(pids), pids))
        return 0
    if act == 'reset':
        return 0 if reset(c, root) else 1
    if act == 'ensure':
        pids = find_pids(c['exe_full'])
        st, status, _ = pine_status(c['pine_port'])
        if pids and st == 'ok' and status == 0 and ready(c, root):
            print('emulator healthy')
            return 0
        return 0 if reset(c, root) else 1
    if act in ('load-state', 'save-state'):
        slot = int(args.slot if args.slot is not None else c['state_slot'])
        with pk_pine.Pine(c['pine_port'], timeout=5.0) as p:
            (p.load_state if act == 'load-state' else p.save_state)(slot)
        print(act, 'slot', slot, 'ok')
        return 0
    return 2
