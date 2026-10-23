"""portkit decomp: work with a decompiler dump without reading it. The dump is one big text file in which every function starts with a line `// ==== NAME @ ADDRESS` (what the Ghidra
scripts of this project write; adapt `header_regex` for another format). An index (functions, calls, strings, data references) is built once; then

  decomp index [--force]                  (re)build the index (done automatically when the dump changed)
  decomp dossier NAME [--lines N|--full]  one page: signature, size, callers, callees, strings, data references, constants, the first N lines of code (default 60)
  decomp callers NAME | callees NAME      who calls it / what it calls (with counts and addresses)
  decomp search REGEX [--max N]           functions whose code matches, with the matching lines
  decomp list [PATTERN]                   function names (filtered)
  decomp dump                             run the dump command of the manifest (Ghidra headless)

Manifest:
[decomp]
source = "work/decomp/all_dun.c"
symbols = ["notes/symbols.csv"]          # optional: lines `0xADDR,size,TYPE,segment,name` for names / sizes
header_regex = '^// ==== (\\S+) @ ([0-9a-fA-F]+)'    # optional
dump_cmd = "bash scripts/ghidra/run_all.sh"           # optional
"""
import json
import os
import re
import subprocess
import sys

DEFAULT_HEADER = r'^// ==== (\S+) @ ([0-9a-fA-F]+)'
IDENT = re.compile(r'\b([A-Za-z_][A-Za-z0-9_<>:~]*)\s*\(')
KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'do', 'else', 'case'}


def _paths(manifest):
    cfg = manifest.get('decomp', {})
    root = manifest['_root']
    if not cfg.get('source'):
        raise SystemExit('portkit decomp: [decomp] source is not set in portkit.toml')
    src = os.path.normpath(os.path.join(root, cfg['source']))
    idx = os.path.join(root, '.portkit', 'decomp', 'index.json')
    return cfg, root, src, idx


def build_index(cfg, root, src, idx_path):
    header = re.compile(cfg.get('header_regex', DEFAULT_HEADER))
    lines = open(src, encoding='utf-8', errors='replace').read().split('\n')
    funcs = []
    for i, line in enumerate(lines):
        m = header.match(line)
        if m:
            if funcs:
                funcs[-1]['end'] = i
            funcs.append(dict(name=m.group(1), addr=m.group(2).lower().lstrip('0') or '0', start=i, end=len(lines)))
    names = {f['name'] for f in funcs}
    short = {}
    for f in funcs:
        short.setdefault(f['name'].split('__')[0], []).append(f['name'])
    callers = {}
    for f in funcs:
        body = lines[f['start'] + 1:f['end']]
        text = '\n'.join(body)
        calls = {}
        for m in IDENT.finditer(text):
            n = m.group(1)
            if n in KEYWORDS or n == f['name']:
                continue
            calls[n] = calls.get(n, 0) + 1
        resolved = {}
        for n, c in calls.items():
            if n in names:
                resolved[n] = c
            elif n in short and len(short[n]) == 1:
                resolved[short[n][0]] = resolved.get(short[n][0], 0) + c
            else:
                resolved.setdefault('?' + n, 0)
                resolved['?' + n] += c
        f['calls'] = resolved
        f['strings'] = sorted(set(re.findall(r'"([^"\n]{2,60})"', text)))[:40]
        f['data'] = sorted(set(re.findall(r'\b(?:DAT|PTR|UNK|LAB)_[0-9a-fA-F]{6,8}\b|\b_[0-9]{2,5}\b', text)))[:60]
        f['consts'] = sorted(set(re.findall(r'\b0x[0-9a-fA-F]{3,8}\b', text)))[:40]
        f['lines'] = f['end'] - f['start']
        for n, c in resolved.items():
            if not n.startswith('?'):
                callers.setdefault(n, {})[f['name']] = c
    for f in funcs:
        f['callers'] = callers.get(f['name'], {})
    sym = {}
    for p in cfg.get('symbols', []):
        fp = os.path.join(root, p)
        if os.path.isfile(fp):
            for line in open(fp, encoding='utf-8', errors='replace'):
                parts = line.strip().split(',')
                if len(parts) >= 5 and parts[0].startswith('0x'):
                    sym[parts[0][2:].lower().lstrip('0') or '0'] = dict(size=parts[1], type=parts[2], seg=parts[3], name=parts[4])
    os.makedirs(os.path.dirname(idx_path), exist_ok=True)
    data = dict(source=src, mtime=os.path.getmtime(src), funcs=funcs, symbols=sym)
    json.dump(data, open(idx_path, 'w', encoding='utf-8'))
    return data


def load_index(manifest, force=False):
    cfg, root, src, idx = _paths(manifest)
    if not force and os.path.isfile(idx):
        try:
            data = json.load(open(idx, encoding='utf-8'))
            if data.get('source') == src and abs(data.get('mtime', 0) - os.path.getmtime(src)) < 1:
                return data, src
        except (OSError, ValueError):
            pass
    print('portkit decomp: building the index of %s ...' % os.path.relpath(src, root), file=sys.stderr)
    return build_index(cfg, root, src, idx), src


def find_func(data, query):
    q = query.lower().lstrip('0x') if re.fullmatch(r'(0x)?[0-9a-fA-F]{5,8}', query) else None
    if q is not None:
        hit = [f for f in data['funcs'] if f['addr'] == q.lstrip('0')]
        if hit:
            return hit
    exact = [f for f in data['funcs'] if f['name'] == query]
    if exact:
        return exact
    ex2 = [f for f in data['funcs'] if f['name'].split('__')[0] == query]
    if ex2:
        return ex2
    return [f for f in data['funcs'] if query.lower() in f['name'].lower()]


def _pick(data, query):
    hits = find_func(data, query)
    if not hits:
        print('no function matches %r (decomp list PATTERN)' % query)
        return None
    if len(hits) > 1:
        print('%d functions match %r: %s' % (len(hits), query, ', '.join('%s@%s' % (h['name'], h['addr']) for h in hits[:12])) + (' ...' if len(hits) > 12 else ''))
        print('using the first; give the full name or address to choose')
    return hits[0]


def _fmt_calls(d, data, limit=14):
    items = sorted(d.items(), key=lambda kv: -kv[1])
    addr = {f['name']: f['addr'] for f in data['funcs']}
    out = []
    for n, c in items[:limit]:
        label = n[1:] + '?' if n.startswith('?') else '%s@%s' % (n, addr.get(n, '?'))
        out.append('%s%s' % (label, ' x%d' % c if c > 1 else ''))
    more = len(items) - limit
    return ', '.join(out) + (' ... +%d' % more if more > 0 else '')


def run(manifest, args):
    cfg, root, src, idx = _paths(manifest)
    if args.action == 'dump':
        if not cfg.get('dump_cmd'):
            raise SystemExit('portkit decomp: [decomp] dump_cmd is not set')
        import pk_check
        shell = pk_check._shell_prefix(manifest.get('project', {}).get('shell'))
        return subprocess.call(shell + [cfg['dump_cmd']], cwd=root)
    data, src = load_index(manifest, force=args.action == 'index' and args.force)
    if args.action == 'index':
        print('index: %d functions, %d symbols' % (len(data['funcs']), len(data['symbols'])))
        return 0
    if args.action == 'list':
        pat = re.compile(args.query or '.', re.I)
        names = [f['name'] + '@' + f['addr'] for f in data['funcs'] if pat.search(f['name'])]
        print('\n'.join(names[:args.max]))
        print('(%d of %d)' % (min(len(names), args.max), len(names)))
        return 0
    if args.action == 'search':
        pat = re.compile(args.query)
        lines = open(src, encoding='utf-8', errors='replace').read().split('\n')
        shown = 0
        for f in data['funcs']:
            hits = [i for i in range(f['start'] + 1, f['end']) if pat.search(lines[i])]
            if not hits:
                continue
            print('%s@%s (%d line(s)):' % (f['name'], f['addr'], len(hits)))
            for i in hits[:3]:
                print('   %d: %s' % (i + 1, lines[i].strip()[:150]))
            shown += 1
            if shown >= args.max:
                print('... (more; --max N)')
                break
        if not shown:
            print('no function matches')
        return 0
    f = _pick(data, args.query)
    if not f:
        return 1
    if args.action == 'callers':
        print('%s@%s is called by: %s' % (f['name'], f['addr'], _fmt_calls(f['callers'], data, 60) or 'nobody (in this dump)'))
        return 0
    if args.action == 'callees':
        print('%s@%s calls: %s' % (f['name'], f['addr'], _fmt_calls(f['calls'], data, 80) or 'nothing'))
        return 0
    sym = data['symbols'].get(f['addr'])
    print('== %s @ 0x%s   %d lines of decompiled code%s' % (f['name'], f['addr'], f['lines'], ('   [symbol table: %s bytes, %s, %s]' % (sym['size'], sym['type'], sym['seg'])) if sym else ''))
    print('callers : ' + (_fmt_calls(f['callers'], data) or '-'))
    print('callees : ' + (_fmt_calls(f['calls'], data) or '-'))
    if f['strings']:
        print('strings : ' + ' | '.join('"%s"' % s for s in f['strings'][:12]))
    if f['data']:
        print('data    : ' + ', '.join(f['data'][:16]) + (' ...' if len(f['data']) > 16 else ''))
    if f['consts']:
        print('consts  : ' + ', '.join(f['consts'][:14]))
    lines = open(src, encoding='utf-8', errors='replace').read().split('\n')
    body = lines[f['start'] + 1:f['end']]
    while body and not body[0].strip():
        body.pop(0)
    n = len(body) if args.full else args.lines
    print('-' * 60)
    print('\n'.join(body[:n]).rstrip())
    if len(body) > n:
        print('... %d more line(s) (decomp dossier %s --full | --lines N; source line %d)' % (len(body) - n, f['name'], f['start'] + 1))
    return 0
