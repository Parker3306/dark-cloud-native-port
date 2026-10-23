"""portkit find: look up a topic in the project's notes (and optionally its code) and print the few best snippets with file, line and the heading they sit under, instead of reading whole files."""
import os
import re

TEXT_EXT = {'.md', '.txt', '.csv', '.toml', '.cfg', '.ini'}
CODE_EXT = {'.py', '.cpp', '.c', '.h', '.hpp', '.gd', '.ps1', '.sh', '.java', '.rs', '.js', '.ts', '.cs'}
SKIP_DIRS = {'.git', '.portkit', 'build', 'node_modules', '__pycache__', '.godot', 'godot-cpp', 'data', 'extracted', 'iso', 'ghidra', 'llvm-mingw', 'cmake', 'ninja', 'bin'}
MAX_FILE = 3 * 1024 * 1024


def _files(root, paths, exts):
    for p in paths:
        full = os.path.join(root, p)
        if os.path.isfile(full):
            yield full
            continue
        for d, dirs, files in os.walk(full):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
            for f in files:
                if os.path.splitext(f)[1].lower() in exts:
                    fp = os.path.join(d, f)
                    try:
                        if os.path.getsize(fp) <= MAX_FILE:
                            yield fp
                    except OSError:
                        pass


def search(root, paths, exts, terms, regex, max_hits, context, per_file):
    pats = [re.compile(t if regex else re.escape(t), re.I) for t in terms]
    scored = []
    for fp in _files(root, paths, exts):
        try:
            lines = open(fp, encoding='utf-8', errors='replace').read().splitlines()
        except OSError:
            continue
        heading = ''
        heads = {}
        file_hits = []
        for i, line in enumerate(lines):
            if line.startswith('#'):
                heading = line.lstrip('# ').strip()
            heads[i] = heading
            hit = [bool(p.search(line)) for p in pats]
            if any(hit):
                file_hits.append((i, sum(hit)))
        if not file_hits:
            continue
        # a line with all terms beats lines with some; terms spread over a few neighbouring lines count too
        for i, n in file_hits:
            window = lines[max(0, i - 2):i + 3]
            window_terms = sum(1 for p in pats if any(p.search(w) for w in window))
            score = n * 3 + window_terms * 2 + (2 if line_is_heading(lines[i]) else 0) + (1 if any(p.search(heads[i]) for p in pats) else 0)
            if window_terms < len(pats) and len(pats) > 1:
                continue
            scored.append((score, fp, i, heads[i]))
    scored.sort(key=lambda t: (-t[0], t[1], t[2]))
    out, per = [], {}
    for score, fp, i, head in scored:
        if per.get(fp, 0) >= per_file:
            continue
        per[fp] = per.get(fp, 0) + 1
        out.append((fp, i, head))
        if len(out) >= max_hits:
            break
    return out


def line_is_heading(s):
    return s.startswith('#')


def run(manifest, args):
    root = manifest['_root']
    cfg = manifest.get('find', {})
    notes = cfg.get('paths', ['notes', 'README.md', 'docs'])
    code = cfg.get('code', [])
    exts = set(TEXT_EXT)
    paths = list(notes)
    if args.code:
        paths += code or ['.']
        exts |= CODE_EXT
    hits = search(root, paths, exts, args.terms, args.regex, args.max, args.context, args.per_file)
    if not hits:
        print('no match for %s in %s%s' % (' '.join(args.terms), ', '.join(paths), '' if args.code else '  (add --code to search source files)'))
        return 0
    for fp, i, head in hits:
        lines = open(fp, encoding='utf-8', errors='replace').read().splitlines()
        rel = os.path.relpath(fp, root).replace('\\', '/')
        print('%s:%d%s' % (rel, i + 1, ('  [' + head[:60] + ']') if head else ''))
        for j in range(max(0, i - args.context), min(len(lines), i + args.context + 1)):
            print('   %s%s' % ('> ' if j == i else '  ', lines[j][:args.width]))
    print('(%d hit(s); narrow with more terms, widen with --max N / --code / --context N)' % len(hits))
    return 0
