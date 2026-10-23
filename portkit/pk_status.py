"""portkit status: where does the project stand, in about 20 lines (git, the last check / shots results, open items, the pickup note, the emulator). Meant for the start of a chat."""
import json
import os
import re
import socket
import subprocess
import time


def _git(root, *args):
    try:
        r = subprocess.run(['git'] + list(args), cwd=root, capture_output=True, text=True, timeout=20)
        return r.stdout.strip() if r.returncode == 0 else None
    except Exception:
        return None


def _age(ts):
    s = max(0, time.time() - ts)
    if s < 90:
        return '%ds ago' % s
    if s < 5400:
        return '%dm ago' % (s / 60)
    if s < 172800:
        return '%dh ago' % (s / 3600)
    return '%dd ago' % (s / 86400)


def _load(path):
    try:
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def run(manifest, args):
    root = manifest['_root']
    cfg = manifest.get('status', {})
    out = []
    name = manifest.get('project', {}).get('name', os.path.basename(root))
    out.append('== %s' % name)
    branch = _git(root, 'rev-parse', '--abbrev-ref', 'HEAD')
    if branch:
        porcelain = _git(root, 'status', '--porcelain') or ''
        lines = [l for l in porcelain.splitlines() if l.strip()]
        modified = sum(1 for l in lines if not l.startswith('??'))
        untracked = len(lines) - modified
        last = _git(root, 'log', '-1', '--format=%h %s (%cr)') or '-'
        out.append('git: %s, %d modified, %d untracked; last commit: %s' % (branch, modified, untracked, last[:90]))
    state = os.path.join(root, '.portkit')
    last_run = _load(os.path.join(state, 'last_run.json'))
    last = _load(os.path.join(state, 'last.json')) or {}
    if last:
        stamps = [v.get('when', '') for v in last.values()]
        newest = max(stamps) if stamps else ''
        counts = {}
        for v in last.values():
            counts[v['status']] = counts.get(v['status'], 0) + 1
        bad = sorted(n for n, v in last.items() if v['status'] not in ('PASS', 'SKIP'))
        when = ''
        try:
            when = _age(time.mktime(time.strptime(newest, '%Y%m%d-%H%M%S')))
        except ValueError:
            pass
        out.append('checks (last run %s): %s%s' % (when, ', '.join('%d %s' % (v, k.lower()) for k, v in sorted(counts.items())), ('; FAILING: ' + ', '.join(bad)) if bad else ''))
        total = len(manifest.get('check', []))
        if total > len(last):
            out.append('  %d of %d checks have never run: python portkit/portkit.py check --list' % (total - len(last), total))
        try:   # source files changed since the last run?
            ref = time.mktime(time.strptime(newest, '%Y%m%d-%H%M%S'))
            changed = [l[3:] for l in (_git(root, 'status', '--porcelain') or '').splitlines() if os.path.exists(os.path.join(root, l[3:])) and os.path.getmtime(os.path.join(root, l[3:])) > ref]
            if changed:
                out.append('  %d file(s) changed since then (e.g. %s): run the checks again' % (len(changed), changed[0]))
        except (ValueError, OSError):
            pass
    else:
        out.append('checks: none run yet (python portkit/portkit.py check --list)')
    shots = _load(os.path.join(state, 'shots', 'last.json'))
    if shots:
        counts = {}
        for r in shots['results']:
            counts[r['status']] = counts.get(r['status'], 0) + 1
        odd = [r['name'] + '(' + r['status'].lower() + ')' for r in shots['results'] if r['status'] not in ('SAME',)]
        out.append('shots (%s): %s%s' % (_age(shots['time']), ', '.join('%d %s' % (v, k.lower()) for k, v in sorted(counts.items())), ('; ' + ', '.join(odd[:6])) if odd else ''))
    todo = cfg.get('todo')
    if todo and os.path.isfile(os.path.join(root, todo)):
        open_items, done = [], 0
        section = ''
        for line in open(os.path.join(root, todo), encoding='utf-8'):
            if line.startswith('##'):   # (sub-headings only: the single '# title' line says nothing)
                section = line.lstrip('# ').strip()
            m = re.match(r'\s*[-*]\s*\[( |x|X)\]\s*(.*)', line)
            if m:
                if m.group(1) == ' ':
                    open_items.append((section, m.group(2).strip()))
                else:
                    done += 1
        out.append('open items (%s): %d open, %d done' % (todo, len(open_items), done))
        limit = int(cfg.get('max_items', 8))
        for sec, text in open_items[:limit]:
            out.append('  - %s%s' % (('[' + sec[:18] + '] ') if sec else '', text[:140]))
        if len(open_items) > limit:
            out.append('  ... %d more in %s' % (len(open_items) - limit, todo))
    pick = cfg.get('pickup')
    if pick and os.path.isfile(os.path.join(root, pick)):
        text = [l.rstrip() for l in open(os.path.join(root, pick), encoding='utf-8') if l.strip()]
        out.append('pickup note (%s):' % pick)
        for l in text[:int(cfg.get('pickup_lines', 12))]:
            out.append('  ' + l[:160])
        if len(text) > int(cfg.get('pickup_lines', 12)):
            out.append('  ... (more in %s)' % pick)
    emu = manifest.get('emu')
    if emu and emu.get('pine_port'):
        try:
            socket.create_connection(('127.0.0.1', int(emu['pine_port'])), timeout=0.5).close()
            out.append('emulator: PINE answers on port %s' % emu['pine_port'])
        except OSError:
            out.append('emulator: not running (python portkit/portkit.py emu start)')
    print('\n'.join(out))
    return 0
