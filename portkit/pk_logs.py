"""portkit logs: make a long log short. Drops known noise (manifest [logfilter] drop = [regex, ...], plus a few built-in engine-noise rules), collapses repeated lines into one line with a count,
and lists the distinct errors with counts. Also used for the excerpts that `check` prints.

  portkit logs FILE [--errors] [--tail N] [--grep REGEX] [--keep-numbers]      (FILE = '-' reads stdin; `.portkit/runs/latest` names the last run's directory)
"""
import os
import re
import sys

BUILTIN_DROP = [
    r'BUG: Unreferenced static string',        # Godot shutdown noise
    r'^\s*at: .*\(.*\.cpp:\d+\)$',             # Godot error location lines (the message above them is kept)
    r'^\s*GDScript backtrace',
    r'WARNING: .*ObjectDB instances leaked',
    r'^Godot Engine v',
    r'^Vulkan \d',
]
ERROR_RE = re.compile(r'\b(ERROR|FAIL(ED)?|Traceback|SCRIPT ERROR|Exception|fatal|panic|assert(ion)?)\b|error:', re.I)


def _signature(line):
    s = re.sub(r'0x[0-9a-fA-F]+', '0x#', line)
    s = re.sub(r'\d+(\.\d+)?', '#', s)
    return s.strip()[:160]


def reduce(text, drop=(), keep_numbers=False, grep=None):
    """Returns (lines with repeat counts, error signature counts)."""
    drops = [re.compile(p) for p in list(BUILTIN_DROP) + list(drop)]
    out = []
    errors = {}
    prev, count = None, 0
    for line in text.splitlines():
        if any(d.search(line) for d in drops):
            continue
        if grep and not re.search(grep, line):
            continue
        key = line if keep_numbers else _signature(line)
        if ERROR_RE.search(line):
            errors[_signature(line)] = errors.get(_signature(line), 0) + 1
        if key == prev:
            count += 1
            continue
        if prev is not None:
            out.append((prev_line, count))
        prev, prev_line, count = key, line, 1
    if prev is not None:
        out.append((prev_line, count))
    return out, errors


def render(lines, tail=None, width=200):
    rows = ['%s%s' % (l[:width], ('   [x%d]' % c) if c > 1 else '') for l, c in lines]
    return rows[-tail:] if tail else rows


def run(manifest, args):
    drop = manifest.get('logfilter', {}).get('drop', [])
    if args.file == '-':
        text = sys.stdin.read()
    else:
        path = args.file
        if os.path.isdir(path):   # a run directory: every log of it
            parts = []
            for f in sorted(os.listdir(path)):
                if f.endswith('.log'):
                    parts.append('=== ' + f + '\n' + open(os.path.join(path, f), encoding='utf-8', errors='replace').read())
            text = '\n'.join(parts)
        else:
            text = open(path, encoding='utf-8', errors='replace').read()
    lines, errors = reduce(text, drop, args.keep_numbers, args.grep)
    if args.errors:
        if not errors:
            print('no errors found')
        for sig, n in sorted(errors.items(), key=lambda kv: -kv[1])[:40]:
            print('%5d  %s' % (n, sig))
        return 0
    rows = render(lines, args.tail)
    print('\n'.join(rows))
    print('(%d line(s) after filtering; %d distinct error signature(s); --errors lists them)' % (len(lines), len(errors)))
    return 0
