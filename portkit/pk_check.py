"""portkit check: run the checks of portkit.toml, keep full logs on disk, print one line per check and a short excerpt for the failures only."""
import json
import os
import re
import shlex
import subprocess
import sys
import time

DEFAULT_TIMEOUT = 600


def _shell_prefix(spec):
    """How a string command is run: a list / string from the manifest, else cmd on Windows and bash elsewhere."""
    if isinstance(spec, list):
        return spec
    if isinstance(spec, str) and spec:
        return shlex.split(spec, posix=False)
    if os.name == 'nt':
        return ['cmd', '/c']
    return ['bash', '-c']


def _kill_tree(proc):
    try:
        if os.name == 'nt':
            subprocess.run(['taskkill', '/T', '/F', '/PID', str(proc.pid)], capture_output=True)
        else:
            proc.kill()
    except Exception:
        pass


def run_command(cmd, cwd, env, timeout, log_path, shell_spec, append=False):
    """Runs one command with stdout + stderr going to a FILE (a pipe would hang when the command leaves a child process behind that keeps the handle, e.g. an emulator).
    Returns (exit code or None on timeout, seconds)."""
    start = time.time()
    with open(log_path, 'ab' if append else 'wb') as log:
        argv = cmd if isinstance(cmd, list) else _shell_prefix(shell_spec) + [cmd]
        try:
            proc = subprocess.Popen(argv, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        except OSError as e:
            log.write(('portkit: cannot start %r: %s\n' % (argv, e)).encode())
            return -1, time.time() - start
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            _kill_tree(proc)
            try:
                proc.wait(timeout=10)
            except Exception:
                pass
            log.write(('\nportkit: TIMEOUT after %ss\n' % timeout).encode())
            return None, time.time() - start
    return code, time.time() - start


def _read(path):
    try:
        with open(path, 'rb') as f:
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def _search_any(patterns, text, flags=re.M):
    for p in patterns:
        m = re.search(p, text, flags)
        if m:
            return m
    return None


def _as_list(v):
    if v is None:
        return []
    return [v] if isinstance(v, str) else list(v)


def judge(check, code, text):
    """PASS / FAIL / TIMEOUT from the exit code and the log: a fail_regex hit fails, every pass_regex must hit, count_min lines of count_regex must exist."""
    if code is None:
        return 'TIMEOUT', 'timed out'
    if code not in _as_list(check.get('expect_exit', [0])):
        return 'FAIL', 'exit code %s' % code
    m = _search_any(_as_list(check.get('fail_regex')), text)
    if m:
        return 'FAIL', 'matched fail_regex: %s' % m.group(0).strip()[:70]
    for p in _as_list(check.get('pass_regex')):
        if not re.search(p, text, re.M):
            return 'FAIL', 'missing pass_regex: %s' % p
    cmin = check.get('count_min')
    if cmin is not None and check.get('count_regex'):
        n = len(re.findall(check['count_regex'], text, re.M))
        if n < int(cmin):
            return 'FAIL', 'only %d lines match %s (need %d)' % (n, check['count_regex'], int(cmin))
    return 'PASS', ''


def summarize(check, text):
    parts = []
    if check.get('count_regex'):
        parts.append('%d %s' % (len(re.findall(check['count_regex'], text, re.M)), check.get('count_label', 'ok')))
    if check.get('summary_regex'):
        last = None
        for m in re.finditer(check['summary_regex'], text, re.M):
            last = m
        if last:
            parts.append((last.group(1) if last.groups() else last.group(0)).strip()[:90])
    return ' | '.join(parts)


def excerpt(check, text, n):
    lines = text.splitlines()
    at = None
    pats = _as_list(check.get('fail_regex')) + [r'\bFAIL\b', r'Traceback', r'\bERROR\b', r'\berror:']
    for p in pats:
        for i, line in enumerate(lines):
            if re.search(p, line):
                at = i
                break
        if at is not None:
            break
    if at is None:
        chunk = lines[-n:]
    else:
        chunk = lines[max(0, at - 3):at + n]
    return [l[:200] for l in chunk]


def select(checks, args, last):
    sel = checks
    if args.names:
        wanted = set(args.names)
        unknown = wanted - {c['name'] for c in checks}
        if unknown:
            print('portkit: unknown check(s): %s' % ', '.join(sorted(unknown)), file=sys.stderr)
        sel = [c for c in sel if c['name'] in wanted]
    if args.tags:
        tags = set(t.strip() for t in args.tags.split(','))
        sel = [c for c in sel if tags & set(_as_list(c.get('tags')))]
    if args.failed:
        bad = {n for n, r in last.items() if r.get('status') not in ('PASS', 'SKIP')}
        sel = [c for c in sel if c['name'] in bad]
    return sel


def run(manifest, args):
    root = manifest['_root']
    project = manifest.get('project', {})
    defaults = manifest.get('defaults', {})
    checks = manifest.get('check', [])
    state_dir = os.path.join(root, '.portkit')
    os.makedirs(state_dir, exist_ok=True)
    last_path = os.path.join(state_dir, 'last.json')
    try:
        last = json.load(open(last_path, encoding='utf-8'))
    except (OSError, ValueError):
        last = {}
    sel = select(checks, args, last)
    if args.list:
        for c in sel:
            print('%-22s %-10s %s' % (c['name'], ','.join(_as_list(c.get('tags'))), c.get('desc', c.get('cmd', ''))[:90]))
        return 0
    if not sel:
        print('portkit check: nothing to run')
        return 0
    stamp = time.strftime('%Y%m%d-%H%M%S')
    run_dir = os.path.join(state_dir, 'runs', stamp)
    os.makedirs(run_dir, exist_ok=True)
    base_env = dict(os.environ)
    base_env.update({k: str(v) for k, v in project.get('env', {}).items()})
    groups = {g['name']: g for g in manifest.get('group', [])}
    group_left = {}
    for c in sel:
        if c.get('group'):
            group_left[c['group']] = group_left.get(c['group'], 0) + 1
    group_ready = set()
    results = []
    print('portkit check: %d check(s), logs in %s' % (len(sel), os.path.relpath(run_dir, root)))
    sys.stdout.flush()
    for c in sel:
        name = c['name']
        shell_spec = c.get('shell', defaults.get('shell', project.get('shell')))
        cwd = os.path.normpath(os.path.join(root, c.get('cwd', defaults.get('cwd', '.'))))
        env = dict(base_env)
        env.update({k: str(v) for k, v in c.get('env', {}).items()})
        timeout = c.get('timeout', defaults.get('timeout', DEFAULT_TIMEOUT))
        log_path = os.path.join(run_dir, name + '.log')
        missing = [p for p in _as_list(c.get('requires')) if not os.path.exists(os.path.join(root, p))]
        if missing:
            res = dict(name=name, status='SKIP', secs=0.0, note='missing: ' + ', '.join(missing), summary='', attempts=0)
            results.append(res)
            print(' SKIP  %-22s %s' % (name, res['note']))
            sys.stdout.flush()
            continue
        g = groups.get(c.get('group', ''))
        if g and g['name'] not in group_ready and g.get('setup'):
            run_command(g['setup'], cwd, env, g.get('timeout', 300), os.path.join(run_dir, '_setup_%s.log' % g['name']), g.get('shell', shell_spec))
            group_ready.add(g['name'])
        attempts = 0
        max_attempts = 1 + int(c.get('retries', defaults.get('retries', 0)))
        total = 0.0
        while True:
            attempts += 1
            if c.get('prep'):
                run_command(c['prep'], cwd, env, c.get('prep_timeout', 300), log_path, shell_spec, append=attempts > 1)
            code, secs = run_command(c['cmd'], cwd, env, timeout, log_path, shell_spec, append=attempts > 1 or bool(c.get('prep')))
            total += secs
            text = _read(log_path)
            status, note = judge(c, code, text)
            if status == 'PASS' or attempts >= max_attempts:
                break
            retry_re = c.get('retry_regex')
            if retry_re and not re.search(retry_re, text, re.M) and status != 'TIMEOUT':
                break
            if c.get('before_retry'):
                run_command(c['before_retry'], cwd, env, c.get('before_retry_timeout', 300), os.path.join(run_dir, name + '.retry.log'), shell_spec, append=True)
        summary = summarize(c, text)
        prev = last.get(name, {}).get('status')
        flag = ''
        if status == 'PASS' and prev not in (None, 'PASS', 'SKIP'):
            flag = ' (fixed)'
        elif status != 'PASS' and prev in ('PASS', None):
            flag = ' (NEW)' if prev == 'PASS' else ''
        ftext = text
        if status != 'PASS':   # (noise of the engine dropped, repeated lines counted: the excerpt is what a reader needs)
            import pk_logs
            ftext = '\n'.join(pk_logs.render(pk_logs.reduce(text, manifest.get('logfilter', {}).get('drop', []), keep_numbers=True)[0]))
        res = dict(name=name, status=status, secs=round(total, 1), note=note, summary=summary, attempts=attempts, log=os.path.relpath(log_path, root), excerpt=excerpt(c, ftext, args.excerpt) if status != 'PASS' else [])
        results.append(res)
        print(' %-5s %-22s %6.1fs  %s%s%s' % (status, name, total, summary or note, '  [%d attempts]' % attempts if attempts > 1 else '', flag))
        sys.stdout.flush()
        if c.get('group'):
            group_left[c['group']] -= 1
            if group_left[c['group']] == 0 and g and g.get('teardown'):
                run_command(g['teardown'], cwd, env, g.get('timeout', 300), os.path.join(run_dir, '_teardown_%s.log' % g['name']), g.get('shell', shell_spec))
        if status != 'PASS' and args.stop_on_fail:
            break
    for g in groups.values():   # (a group whose checks did not all run still gets its teardown)
        if g['name'] in group_ready and group_left.get(g['name'], 0) > 0 and g.get('teardown'):
            run_command(g['teardown'], root, base_env, g.get('timeout', 300), os.path.join(run_dir, '_teardown_%s.log' % g['name']), g.get('shell'))
    for r in results:
        last[r['name']] = dict(status=r['status'], secs=r['secs'], when=stamp)
    json.dump(last, open(last_path, 'w', encoding='utf-8'), indent=1)
    with open(os.path.join(state_dir, 'last_run.json'), 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=1)
    bad = [r for r in results if r['status'] not in ('PASS', 'SKIP')]
    counts = {}
    for r in results:
        counts[r['status']] = counts.get(r['status'], 0) + 1
    print('summary: ' + ', '.join('%d %s' % (v, k.lower()) for k, v in sorted(counts.items())))
    for r in bad:
        print('--- %s: %s (full log: %s)' % (r['name'], r['note'], r['log']))
        for line in r['excerpt']:
            print('    ' + line)
    if getattr(args, 'json', False):
        print(json.dumps(results, indent=1))
    return 1 if bad else 0
