"""portkit shots: capture screenshots with commands from the manifest, compare them with the accepted baselines, and report only what changed (with one before | after | diff image
for each change, so that a model or a person looks at a picture only when something moved)."""
import json
import os
import shutil
import sys

import pk_check
import pk_png


def _paths(manifest):
    root = manifest['_root']
    cfg = manifest.get('shots', {})
    base = os.path.join(root, '.portkit', 'shots')
    d = lambda key, default: os.path.normpath(os.path.join(root, cfg.get(key, default)))
    return dict(cur=d('dir', os.path.join('.portkit', 'shots', 'current')), base=d('baseline', os.path.join('.portkit', 'shots', 'baseline')),
                report=os.path.join(base, 'report'), logs=os.path.join(base, 'logs'))


def _fmt(s, **kw):
    for k, v in kw.items():
        s = s.replace('{' + k + '}', str(v).replace(os.sep, '/'))   # (forward slashes: a bash command must not see backslashes)
    return s


def _select(shots, args):
    sel = shots
    if args.names:
        sel = [s for s in sel if s['name'] in set(args.names)]
    if args.tags:
        tags = set(t.strip() for t in args.tags.split(','))
        sel = [s for s in sel if tags & set(pk_check._as_list(s.get('tags')))]
    return sel


def _out_path(shot, paths, root):
    out = shot.get('out', '{dir}/{name}.png')
    return os.path.normpath(os.path.join(root, _fmt(out, dir=paths['cur'], name=shot['name'])))


def capture(manifest, shots, paths):
    root = manifest['_root']
    project = manifest.get('project', {})
    os.makedirs(paths['cur'], exist_ok=True)
    os.makedirs(paths['logs'], exist_ok=True)
    env = dict(os.environ)
    env.update({k: os.path.expandvars(str(v)) for k, v in project.get('env', {}).items()})
    failed = {}
    for s in shots:
        out = _out_path(s, paths, root)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if os.path.exists(out):
            os.remove(out)
        cmd = _fmt(s['cmd'], dir=paths['cur'], name=s['name'], out=out, root=root)
        cwd = os.path.normpath(os.path.join(root, s.get('cwd', manifest.get('defaults', {}).get('cwd', '.'))))
        log = os.path.join(paths['logs'], s['name'] + '.log')
        code, secs = pk_check.run_command(cmd, cwd, env, s.get('timeout', 180), log, s.get('shell', manifest.get('defaults', {}).get('shell', project.get('shell'))))
        ok = os.path.exists(out)
        print(' %-5s %-22s %5.1fs' % ('shot' if ok else 'FAIL', s['name'], secs) + ('' if ok else '  (no image written, see %s)' % os.path.relpath(log, root)))
        sys.stdout.flush()
        if not ok:
            failed[s['name']] = log
    return failed


def compare_all(manifest, shots, paths, capture_failed):
    root = manifest['_root']
    cfg = manifest.get('shots', {})
    os.makedirs(paths['report'], exist_ok=True)
    results = []
    for s in shots:
        name = s['name']
        cur = _out_path(s, paths, root)
        base = os.path.join(paths['base'], name + '.png')
        thr = s.get('threshold', cfg.get('threshold', 8))
        limit = s.get('max_changed_pct', cfg.get('max_changed_pct', 0.3))
        r = dict(name=name)
        if name in capture_failed or not os.path.exists(cur):
            r.update(status='MISSING', note='no capture')
        elif not os.path.exists(base):
            r.update(status='NEW', note='no baseline yet (run: shots accept --new)')
        else:
            a, b = pk_png.read_png(base), pk_png.read_png(cur)
            c = pk_png.compare(a, b, thr, [tuple(m) for m in s.get('masks', [])])
            if c.get('size_mismatch'):
                r.update(status='SIZE', note='size %sx%s -> %sx%s' % (c['a'][0], c['a'][1], c['b'][0], c['b'][1]))
            elif c['pct'] > limit:
                png = os.path.join(paths['report'], name + '.png')
                pk_png.side_by_side(a, b, c, png)
                r.update(status='CHANGED', pct=round(c['pct'], 2), mean=round(c['mean'], 2), bbox=c.get('bbox'), image=os.path.relpath(png, root),
                         note='%.2f%% of the pixels differ (limit %.2f%%), mean diff %.1f, region %s' % (c['pct'], limit, c['mean'], c.get('bbox')))
            else:
                r.update(status='SAME', pct=round(c['pct'], 3), note='%.3f%% differ' % c['pct'])
        results.append(r)
    return results


def accept(manifest, shots, paths, args):
    root = manifest['_root']
    os.makedirs(paths['base'], exist_ok=True)
    n = 0
    for s in shots:
        cur = _out_path(s, paths, root)
        base = os.path.join(paths['base'], s['name'] + '.png')
        if not os.path.exists(cur):
            continue
        if args.new and os.path.exists(base):
            continue
        if not (args.names or args.new or args.all_shots):
            continue
        shutil.copyfile(cur, base)
        n += 1
        print(' accepted', s['name'])
    if not n:
        print('nothing accepted (name the shots, or use --new / --all)')
    return 0


def run(manifest, args):
    paths = _paths(manifest)
    shots = _select(manifest.get('shot', []), args)
    if args.action == 'list':
        for s in shots:
            print('%-22s %-10s %s' % (s['name'], ','.join(pk_check._as_list(s.get('tags'))), s.get('desc', '')[:80]))
        return 0
    if not shots:
        print('portkit shots: no shots selected')
        return 0
    if args.action == 'accept':
        return accept(manifest, shots, paths, args)
    failed = {}
    if args.action in ('all', 'run') and not args.no_run:
        print('portkit shots: capturing %d shot(s)' % len(shots))
        failed = capture(manifest, shots, paths)
        if args.action == 'run':
            return 1 if failed else 0
    results = compare_all(manifest, shots, paths, failed)
    bad = 0
    counts = {}
    for r in results:
        counts[r['status']] = counts.get(r['status'], 0) + 1
        if r['status'] in ('CHANGED', 'MISSING', 'SIZE'):
            bad += 1
        if r['status'] != 'SAME':
            print(' %-8s %-22s %s%s' % (r['status'], r['name'], r['note'], ('  -> ' + r['image']) if r.get('image') else ''))
    print('shots: ' + ', '.join('%d %s' % (v, k.lower()) for k, v in sorted(counts.items())))
    try:   # (for `portkit status`)
        import time as _time
        os.makedirs(os.path.join(manifest['_root'], '.portkit', 'shots'), exist_ok=True)
        json.dump(dict(time=_time.time(), results=results), open(os.path.join(manifest['_root'], '.portkit', 'shots', 'last.json'), 'w'), default=str)
    except OSError:
        pass
    if getattr(args, 'json', False):
        print(json.dumps(results, indent=1, default=str))
    return 1 if bad else 0
