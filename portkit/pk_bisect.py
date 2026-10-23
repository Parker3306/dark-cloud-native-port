"""portkit bisect: find the commit that broke a check, without touching your working tree. Binary search over `GOOD..BAD`, each step in a throw-away git worktree where the check of the
CURRENT manifest runs (so the check exists even for old commits).

  bisect CHECK --good REV [--bad REV] [--prep CMD] [--keep]

Manifest (optional):
[bisect]
link = ["iso", "data", "extracted"]     # untracked directories to share with the worktree (junction / symlink to the same files)
prep = "powershell -File native/build_core.ps1"    # run in the worktree before the check at every step (build)
Exit code 0 and prints the first bad commit; 2 when GOOD does not pass or BAD does not fail.
"""
import os
import shutil
import subprocess
import sys

import pk_check


def _git(root, *args, check=True):
    r = subprocess.run(['git'] + list(args), cwd=root, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise SystemExit('git %s failed: %s' % (' '.join(args), r.stderr.strip()[:200]))
    return r.stdout.strip()


def _unlink_all(wt, cfg):
    """Remove the junctions / symlinks of the worktree FIRST (os.rmdir / os.unlink only remove the link, never the files behind it)."""
    for d in cfg.get('link', []):
        p = os.path.join(wt, d)
        try:
            if os.path.islink(p):
                os.unlink(p)
            elif os.path.lexists(p) and os.name == 'nt' and (os.stat(p, follow_symlinks=False).st_file_attributes & 0x400):   # FILE_ATTRIBUTE_REPARSE_POINT: a junction
                os.rmdir(p)
        except OSError:
            pass


def _link(src, dst):
    if os.path.lexists(dst):
        return
    if os.name == 'nt':
        subprocess.run(['cmd', '/c', 'mklink', '/J', dst, src], capture_output=True)
    else:
        os.symlink(src, dst)


class Args:
    def __init__(self, name):
        self.names, self.tags, self.failed, self.list, self.json, self.stop_on_fail, self.excerpt = [name], None, False, False, False, False, 5


def _try(manifest, wt, rev, cfg, check_name, quiet=True):
    _git(wt, 'checkout', '--detach', '--quiet', rev)
    m = dict(manifest)
    m['_root'] = wt
    prep = cfg.get('prep')
    if prep:
        log = os.path.join(wt, '.portkit', 'bisect_prep.log')
        os.makedirs(os.path.dirname(log), exist_ok=True)
        code, _ = pk_check.run_command(prep, wt, dict(os.environ), cfg.get('prep_timeout', 1800), log, manifest.get('project', {}).get('shell'))
        if code != 0:
            return None   # does not build: the commit cannot be judged
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = pk_check.run(m, Args(check_name))
    return rc == 0


def run(manifest, args):
    root = manifest['_root']
    cfg = manifest.get('bisect', {})
    if args.check not in {c['name'] for c in manifest.get('check', [])}:
        raise SystemExit('portkit bisect: unknown check %r' % args.check)
    good = _git(root, 'rev-parse', args.good)
    bad = _git(root, 'rev-parse', args.bad)
    commits = _git(root, 'rev-list', '--reverse', '%s..%s' % (good, bad)).split()
    if not commits:
        raise SystemExit('portkit bisect: nothing between %s and %s' % (args.good, args.bad))
    wt = os.path.join(root, '.portkit', 'bisect', 'wt')
    if os.path.exists(wt):
        _unlink_all(wt, cfg)
        subprocess.run(['git', 'worktree', 'remove', '--force', wt], cwd=root, capture_output=True)
        shutil.rmtree(wt, ignore_errors=True)
    os.makedirs(os.path.dirname(wt), exist_ok=True)
    _git(root, 'worktree', 'add', '--detach', '--quiet', wt, good)
    for d in cfg.get('link', []):
        if os.path.exists(os.path.join(root, d)):
            _link(os.path.join(root, d), os.path.join(wt, d))
    print('portkit bisect: %d commit(s) between GOOD %s and BAD %s, check %s' % (len(commits), good[:8], bad[:8], args.check))
    try:
        g = _try(manifest, wt, good, cfg, args.check)
        if g is not True:
            print('GOOD revision does not pass the check (%s): pick an older one' % ('does not build' if g is None else 'fails'))
            return 2
        b = _try(manifest, wt, bad, cfg, args.check)
        if b is not False:
            print('BAD revision does not fail the check (%s)' % ('does not build' if b is None else 'passes'))
            return 2
        lo, hi = 0, len(commits) - 1   # commits[hi] is bad; find the first bad
        steps = 0
        while lo < hi:
            mid = (lo + hi) // 2
            steps += 1
            r = _try(manifest, wt, commits[mid], cfg, args.check)
            print('  step %d: %s %s' % (steps, commits[mid][:8], {True: 'passes', False: 'FAILS', None: 'does not build (treated as bad)'}[r]))
            sys.stdout.flush()
            if r is True:
                lo = mid + 1
            else:
                hi = mid
        first = commits[lo]
        print('first bad commit: %s' % _git(root, 'log', '-1', '--format=%h %s (%an, %cs)', first))
        print(_git(root, 'show', '--stat', '--format=', first)[:1500])
        return 0
    finally:
        if not args.keep:
            _unlink_all(wt, cfg)
            subprocess.run(['git', 'worktree', 'remove', '--force', wt], cwd=root, capture_output=True)
            shutil.rmtree(wt, ignore_errors=True)
