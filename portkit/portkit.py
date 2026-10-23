#!/usr/bin/env python3
"""portkit: game-independent helpers for porting a game with a model-assisted workflow. Standard library only (numpy / Pillow are used when present, not required).

  python portkit/portkit.py check  [names | --tags a,b] [--list] [--failed] [--json] [--keep-going]   run the checks of the manifest, print a short summary, keep the logs
  python portkit/portkit.py shots  [run|compare|accept|report] [names | --tags a,b] [--no-run]       screenshot regression: capture, compare with baselines, only report changes
  python portkit/portkit.py init   [dir]                                                              start a new game: copy the kit, write a manifest template and a CLAUDE.md snippet
  python portkit/portkit.py status                                                                    the project state in ~20 lines (git, last checks / shots, open items, pickup note)
  python portkit/portkit.py find   TERMS... [--code]                                                  best snippets of the notes (and code) for a topic
  python portkit/portkit.py emu    status|start|stop|reset|ensure|load-state|save-state               the project's own emulator (found by exe path), with crash repair
  python portkit/portkit.py multi  list|run NAME [--interactive]|stop|logs                            several program instances together (co-op host + guests), logs checked
  python portkit/portkit.py replay list|info|record|run|accept                                        replay a human's recorded input as a regression test
  python portkit/portkit.py decomp index|dossier|callers|callees|search|list|dump                     work with a decompiler dump without reading it
  python portkit/portkit.py live   list|run NAME                                                      compare the port with the running original over random trials
  python portkit/portkit.py bisect CHECK --good REV                                                   find the commit that broke a check (in a throw-away worktree)
  python portkit/portkit.py assets iso|inventory|sheet                                                disc listing / extraction, folder inventory, texture contact sheets
  python portkit/portkit.py logs   FILE [--errors]                                                    short version of a long log (noise dropped, repeats counted)

The manifest is portkit.toml in the game's repository (searched upward from the current directory, or --manifest FILE). Read portkit/PORTKIT.md for the format.
"""
import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def find_manifest(explicit):
    if explicit:
        return os.path.abspath(explicit)
    d = os.getcwd()
    while True:
        p = os.path.join(d, 'portkit.toml')
        if os.path.isfile(p):
            return p
        up = os.path.dirname(d)
        if up == d:
            return None
        d = up


def load_manifest(path):
    import tomllib
    with open(path, 'rb') as f:
        data = tomllib.load(f)
    data['_path'] = path
    data['_root'] = os.path.normpath(os.path.join(os.path.dirname(path), data.get('project', {}).get('root', '.')))
    return data


def cmd_init(args):
    target = os.path.abspath(args.dir or '.')
    os.makedirs(target, exist_ok=True)
    dest = os.path.join(target, 'portkit')
    if os.path.abspath(dest) != HERE:
        shutil.copytree(HERE, dest, ignore=shutil.ignore_patterns('__pycache__', '.portkit'), dirs_exist_ok=True)
    manifest = os.path.join(target, 'portkit.toml')
    if not os.path.exists(manifest):
        shutil.copyfile(os.path.join(HERE, 'templates', 'portkit.toml'), manifest)
        print('wrote', manifest)
    snippet = open(os.path.join(HERE, 'templates', 'CLAUDE.snippet.md'), encoding='utf-8').read()
    claude = os.path.join(target, 'CLAUDE.md')
    old = open(claude, encoding='utf-8').read() if os.path.exists(claude) else ''
    if 'portkit/PORTKIT.md' not in old:
        with open(claude, 'a', encoding='utf-8') as f:
            f.write(('\n' if old and not old.endswith('\n') else '') + snippet)
        print('added the portkit section to', claude)
    gi = os.path.join(target, '.gitignore')
    old = open(gi, encoding='utf-8').read() if os.path.exists(gi) else ''
    if '.portkit/' not in old:
        with open(gi, 'a', encoding='utf-8') as f:
            f.write(('\n' if old and not old.endswith('\n') else '') + '# portkit: run logs, screenshot captures and baselines (game renders: never commit)\n.portkit/\n')
        print('added .portkit/ to', gi)
    print('portkit copied to', dest, '- edit portkit.toml, then: python portkit/portkit.py check --list')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog='portkit', description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--manifest', help='portkit.toml (default: searched upward from the current directory)')
    sub = ap.add_subparsers(dest='cmd', required=True)

    c = sub.add_parser('check', help='run the checks of the manifest')
    c.add_argument('names', nargs='*', help='check names (default: all)')
    c.add_argument('--tags', help='only checks with one of these tags (comma separated)')
    c.add_argument('--list', action='store_true', help='list the checks and exit')
    c.add_argument('--failed', action='store_true', help='only the checks that failed in the last run')
    c.add_argument('--json', action='store_true', help='print the result as JSON')
    c.add_argument('--keep-going', action='store_true', help='(default) kept for clarity: a failing check never stops the others')
    c.add_argument('--stop-on-fail', action='store_true', help='stop at the first failing check')
    c.add_argument('--excerpt', type=int, default=20, help='lines of log shown for each failing check (default 20)')

    s = sub.add_parser('shots', help='screenshot regression')
    s.add_argument('action', nargs='?', default='all', choices=['all', 'run', 'compare', 'accept', 'report', 'list'], help='all = run + compare (default)')
    s.add_argument('names', nargs='*')
    s.add_argument('--tags')
    s.add_argument('--no-run', action='store_true', help='compare what is already captured')
    s.add_argument('--new', action='store_true', help='accept: only shots that have no baseline yet')
    s.add_argument('--all', dest='all_shots', action='store_true', help='accept: every captured shot')
    s.add_argument('--json', action='store_true')

    i = sub.add_parser('init', help='set up portkit in a new game repository')
    i.add_argument('dir', nargs='?')

    sub.add_parser('status', help='project state in about 20 lines')

    f = sub.add_parser('find', help='best snippets of the notes (and code) for a topic')
    f.add_argument('terms', nargs='+', help='words that must all appear near each other (case-insensitive)')
    f.add_argument('--code', action='store_true', help='search the source files too ([find] code = [...] or everything)')
    f.add_argument('--regex', action='store_true', help='terms are regular expressions')
    f.add_argument('--max', type=int, default=12)
    f.add_argument('--context', type=int, default=1, help='lines around a hit')
    f.add_argument('--per-file', type=int, default=3)
    f.add_argument('--width', type=int, default=170)

    e = sub.add_parser('emu', help='the project emulator')
    e.add_argument('action', choices=['status', 'start', 'stop', 'reset', 'ensure', 'load-state', 'save-state'])
    e.add_argument('slot', nargs='?', type=int)
    e.add_argument('--iso')

    m = sub.add_parser('multi', help='several instances together')
    m.add_argument('action', choices=['list', 'run', 'stop', 'logs'])
    m.add_argument('name', nargs='?')
    m.add_argument('role', nargs='?')
    m.add_argument('--interactive', action='store_true', help='leave the windows open for a human')
    m.add_argument('--tail', type=int, default=30)

    r = sub.add_parser('replay', help='replay recorded human input')
    r.add_argument('action', choices=['list', 'info', 'record', 'run', 'accept'])
    r.add_argument('names', nargs='*')
    r.add_argument('--tags')

    d = sub.add_parser('decomp', help='decompiler dump without reading it')
    d.add_argument('action', choices=['index', 'dossier', 'callers', 'callees', 'search', 'list', 'dump'])
    d.add_argument('query', nargs='?')
    d.add_argument('--lines', type=int, default=60)
    d.add_argument('--full', action='store_true')
    d.add_argument('--force', action='store_true')
    d.add_argument('--max', type=int, default=30)

    lv = sub.add_parser('live', help='compare with the running original')
    lv.add_argument('action', choices=['list', 'run'])
    lv.add_argument('name', nargs='?')
    lv.add_argument('--trials', type=int)
    lv.add_argument('--seed', type=int)

    b = sub.add_parser('bisect', help='find the commit that broke a check')
    b.add_argument('check')
    b.add_argument('--good', required=True)
    b.add_argument('--bad', default='HEAD')
    b.add_argument('--keep', action='store_true', help='keep the worktree')

    a = sub.add_parser('assets', help='disc / folder / texture helpers')
    asub = a.add_subparsers(dest='action', required=True)
    ai = asub.add_parser('iso')
    ai.add_argument('file')
    ai.add_argument('--find')
    ai.add_argument('--extract')
    ai.add_argument('--max', type=int, default=60)
    an = asub.add_parser('inventory')
    an.add_argument('dir')
    an.add_argument('--top', type=int, default=8)
    asx = asub.add_parser('sheet')
    asx.add_argument('paths', nargs='+')
    asx.add_argument('--out', required=True)
    asx.add_argument('--max', type=int, default=80)
    asx.add_argument('--cell', type=int, default=96)
    asx.add_argument('--cols', type=int, default=12)
    asx.add_argument('--tim2', action='store_true', help='also scan every file for PS2 TIM2 textures')

    lg = sub.add_parser('logs', help='short version of a long log')
    lg.add_argument('file', help='a file, a run directory, or - for stdin')
    lg.add_argument('--errors', action='store_true', help='only the distinct errors with counts')
    lg.add_argument('--tail', type=int, default=60)
    lg.add_argument('--grep')
    lg.add_argument('--keep-numbers', action='store_true', help='do not treat lines that differ only in numbers as repeats')

    args = ap.parse_args(argv)
    if args.cmd == 'init':
        return cmd_init(args)
    path = find_manifest(args.manifest)
    if not path and args.cmd in ('logs', 'assets'):
        path = None
        manifest = {'_root': os.getcwd(), '_path': ''}
        return __import__('pk_logs' if args.cmd == 'logs' else 'pk_assets').run(manifest, args)
    if not path:
        print('portkit: no portkit.toml found (run: python portkit/portkit.py init)', file=sys.stderr)
        return 2
    manifest = load_manifest(path)
    if args.cmd == 'check':
        import pk_check
        return pk_check.run(manifest, args)
    if args.cmd == 'shots':
        import pk_shots
        return pk_shots.run(manifest, args)
    mod = {'status': 'pk_status', 'find': 'pk_find', 'emu': 'pk_emu', 'multi': 'pk_multi', 'replay': 'pk_replay', 'decomp': 'pk_decomp', 'live': 'pk_live', 'bisect': 'pk_bisect',
           'assets': 'pk_assets', 'logs': 'pk_logs'}[args.cmd]
    return __import__(mod).run(manifest, args)


if __name__ == '__main__':
    sys.exit(main())
