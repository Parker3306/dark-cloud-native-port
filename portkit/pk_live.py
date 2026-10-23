"""portkit live: a framework for comparing the port with the RUNNING original game (emulator) over many random trials. You write one small Python module per subsystem; portkit does the rest:
connecting (and repairing the emulator when the connection breaks), the trial loop with reproducible seeds, the deep comparison with tolerances, a short report that names the first differences,
and the shrinking of a failing trial to a simpler one.

  live list | run NAME [--trials N] [--seed S] [--keep-going]

Manifest:
[[live]]
name = "floor_start"
module = "tools/live_floor.py"        # python file (path relative to the project root)
trials = 8
seed = 2026
connect = "pine"                      # "pine": ctx.pine is a PINE client of [emu] pine_port; "none": no emulator (the module compares two programs)
tol = 0.01                            # numbers closer than this are equal
allow_mismatch = 0                    # trials allowed to differ (a known deviation of the port) before the run fails
retries = 2                           # a trial that raises a connection error is repeated after `emu ensure`
desc = "..."

The module (all optional except trial):
    def setup(ctx): ...                         # once, after connecting
    def trial(ctx, i, rng) -> dict              # {"live": <anything json-like>, "port": <the same shape>, "params": <what identifies the trial>}
    def rerun(ctx, params) -> dict              # for shrinking: the same as trial, from params
    def shrink(ctx, params) -> iterable         # simpler params to try, simplest first
    def teardown(ctx): ...
    def make_cave(pine) -> object               # ctx.cave (a mailbox / code cave to call game functions)
`ctx` has: pine, cave, root, manifest, cfg (the [[live]] table), emu (the [emu] table), run(cmd) -> stdout of a command in the project root, log(text).
"""
import importlib.util
import json
import os
import random
import subprocess
import sys
import time

import pk_pine


def deep_diff(a, b, tol, path='', out=None, limit=12):
    """Differences between two json-like structures: (path, a, b) triples, at most `limit`."""
    if out is None:
        out = []
    if len(out) >= limit:
        return out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b), key=str):
            if k not in a or k not in b:
                out.append(('%s.%s' % (path, k), a.get(k, '<missing>'), b.get(k, '<missing>')))
            else:
                deep_diff(a[k], b[k], tol, '%s.%s' % (path, k), out, limit)
        return out
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        if len(a) != len(b):
            out.append(('%s (length)' % path, len(a), len(b)))
        for i, (x, y) in enumerate(zip(a, b)):
            deep_diff(x, y, tol, '%s[%d]' % (path, i), out, limit)
        return out
    if isinstance(a, bool) or isinstance(b, bool) or not (isinstance(a, (int, float)) and isinstance(b, (int, float))):
        if a != b:
            out.append((path, a, b))
        return out
    if abs(a - b) > tol:
        out.append((path, a, b))
    return out


class Ctx:
    def __init__(self, manifest, cfg):
        self.manifest, self.cfg = manifest, cfg
        self.root = manifest['_root']
        self.emu = manifest.get('emu', {})
        self.pine = None
        self.cave = None
        self.notes = []

    def run(self, cmd, timeout=300):
        import pk_check
        shell = pk_check._shell_prefix(self.manifest.get('project', {}).get('shell'))
        r = subprocess.run(shell + [cmd], cwd=self.root, capture_output=True, text=True, timeout=timeout)
        return r.stdout

    def log(self, text):
        self.notes.append(text)

    def connect(self):
        if self.cfg.get('connect', 'pine') == 'none':
            return
        if self.pine is not None:
            try:
                self.pine.close()
            except Exception:
                pass
        self.pine = pk_pine.Pine(int(self.emu.get('pine_port', 28011)), timeout=float(self.cfg.get('pine_timeout', 5.0)))


def _load_module(root, path):
    full = os.path.join(root, path)
    sys.path.insert(0, os.path.dirname(full))
    spec = importlib.util.spec_from_file_location('pk_live_' + os.path.splitext(os.path.basename(path))[0], full)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _repair(manifest):
    if manifest.get('emu'):
        import pk_emu
        c = pk_emu._cfg(manifest)
        print('  (connection problem: repairing the emulator)')
        pk_emu.reset(c, manifest['_root'])


def run_live(manifest, cfg, trials=None, seed=None):
    root = manifest['_root']
    mod = _load_module(root, cfg['module'])
    ctx = Ctx(manifest, cfg)
    n = int(trials if trials is not None else cfg.get('trials', 8))
    base_seed = int(seed if seed is not None else cfg.get('seed', 2026))
    tol = float(cfg.get('tol', 0.01))
    retries = int(cfg.get('retries', 2))
    try:
        ctx.connect()
    except OSError:
        _repair(manifest)
        ctx.connect()
    if hasattr(mod, 'make_cave') and ctx.pine is not None:
        ctx.cave = mod.make_cave(ctx.pine)
    if hasattr(mod, 'setup'):
        mod.setup(ctx)
    results = []
    for i in range(n):
        res = None
        for attempt in range(retries + 1):
            try:
                res = mod.trial(ctx, i, random.Random(base_seed + i))
                break
            except (OSError, pk_pine.PineError, TimeoutError, ConnectionError) as e:
                print('  trial %d: %s: %s' % (i, type(e).__name__, str(e)[:80]))
                if attempt >= retries:
                    res = dict(error='%s: %s' % (type(e).__name__, e))
                    break
                _repair(manifest)
                ctx.connect()
                if hasattr(mod, 'make_cave'):
                    ctx.cave = mod.make_cave(ctx.pine)
                if hasattr(mod, 'setup'):
                    mod.setup(ctx)
        if 'error' in res:
            results.append(dict(i=i, ok=False, params=res.get('params'), diffs=[('error', res['error'], '')]))
            continue
        diffs = deep_diff(res['live'], res['port'], tol)
        results.append(dict(i=i, ok=not diffs, params=res.get('params'), diffs=diffs))
    bad = [r for r in results if not r['ok']]
    shrunk = None
    if bad and hasattr(mod, 'shrink') and hasattr(mod, 'rerun') and bad[0].get('params') is not None:
        best = bad[0]['params']
        improved = True
        while improved:
            improved = False
            for cand in mod.shrink(ctx, best):
                try:
                    r = mod.rerun(ctx, cand)
                except Exception:
                    continue
                if deep_diff(r['live'], r['port'], tol):
                    best, improved = cand, True
                    break
        shrunk = best
    if hasattr(mod, 'teardown'):
        mod.teardown(ctx)
    os.makedirs(os.path.join(root, '.portkit', 'live'), exist_ok=True)
    json.dump(dict(name=cfg['name'], trials=n, seed=base_seed, results=results, shrunk=shrunk), open(os.path.join(root, '.portkit', 'live', cfg['name'] + '.json'), 'w'), indent=1, default=str)
    allow = int(cfg.get('allow_mismatch', 0))
    print('live %s: %d/%d trials identical%s' % (cfg['name'], n - len(bad), n, ('; %d differ (allowed %d)' % (len(bad), allow)) if bad else ''))
    for r in bad[:5]:
        print('  trial %d params=%s' % (r['i'], json.dumps(r['params'], default=str)[:160]))
        for path, a, b in r['diffs'][:5]:
            print('     %s: live %s | port %s' % (path, str(a)[:70], str(b)[:70]))
    if shrunk is not None and shrunk != bad[0].get('params'):
        print('  smallest failing case found: %s' % json.dumps(shrunk, default=str)[:200])
    for note in ctx.notes[:10]:
        print('  note: ' + str(note)[:160])
    return 1 if len(bad) > allow else 0


def run(manifest, args):
    cases = manifest.get('live', [])
    if args.action == 'list':
        for c in cases:
            print('%-20s %-3s trials  %s' % (c['name'], c.get('trials', 8), c.get('desc', c['module'])[:80]))
        return 0
    for c in cases:
        if c['name'] == args.name:
            return run_live(manifest, c, args.trials, args.seed)
    raise SystemExit('portkit live: no case %r (python portkit/portkit.py live list)' % args.name)
