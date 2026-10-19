"""Which external functions do the town / dungeon EVENT scripts (.stb other than dun/monstor) call?
usage: event_usage.py [--json out.json]

Event scripts run on the town CRunScript (0x1D4A430) whose ext table is the EVENT table (0x269C50, ids 0..1000) plus, inside the dungeon,
the DUNGEON table (0x26B310, ids 1010..1101) added by AddSystemEventScript.  The ids of those two tables are disjoint, but they OVERLAP the
MONSTER table (ids 10..253), so tools/dc/stbdis.py (monster table first) mislabels event scripts: use this module's names instead.
"""
import glob
import json
import os
import re
import struct
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import stbdis
from stbdis import disassemble

ROOT = os.path.join(HERE, '..', '..')
DATA = os.path.join(ROOT, 'data')


_orig_ext_names = stbdis.ext_names


def event_names():
    return _orig_ext_names(order=('event', 'dungeon'))


def functions_of(b):
    """Yield (function id or None, record offset, start pc, end pc) for every function body of a .stb (internal functions have id None)."""
    w = lambda o: struct.unpack_from('<I', b, o)[0]
    code, table, count = w(8), w(0xC), w(0x10)
    funcs = [(w(table + 8 * i), w(table + 8 * i + 4)) for i in range(count)]
    recs = funcs + [(-1, w(4))]
    records = {rec for _, rec in recs}
    changed = True
    while changed:
        changed = False
        for rec in sorted(records):
            pc = code + w(rec)
            nxt = [r for r in sorted(records) if r > pc]
            end = nxt[0] if nxt else len(b)
            o = pc
            while o < end and o + 12 <= len(b):
                op, a_, bb = struct.unpack_from('<3I', b, o)
                if op == 0x13 and code + bb not in records and code + bb < len(b):
                    records.add(code + bb)
                    changed = True
                o += 12
            if changed:
                break
    names_of = {rec: fid for fid, rec in recs}
    out = []
    for rec in sorted(records):
        pc = code + w(rec)
        nxt = [r for r in sorted(records) if r > pc]
        end = nxt[0] if nxt else len(b)
        out.append((names_of.get(rec), rec, pc, end))
    return code, out


def scan(path, names):
    """-> {function id (or 'internal@rec'): [ext ids in code order]}"""
    b = open(path, 'rb').read()
    code, fl = functions_of(b)
    res = {}
    for fid, rec, pc, end in fl:
        calls = []
        for line in disassemble(b, names, pc, end, code):
            m = re.search(r'\(id (-?\d+|None)\)', line)
            if m and m.group(1) != 'None':
                calls.append(int(m.group(1)))
            elif '; ' in line and '(id None)' in line:
                calls.append(None)
        res['%s' % (fid if fid is not None else 'int@%#x' % rec)] = calls
    return res


def event_files():
    out = []
    for p in sorted(glob.glob(os.path.join(DATA, 'gedit', '**', '*.stb'), recursive=True)):
        out.append(p)
    out += sorted(glob.glob(os.path.join(DATA, 'dun', 'script', '*', '*.stb')))
    for p in ('test.stb', 'test4.stb'):
        if os.path.exists(os.path.join(DATA, p)):
            out.append(os.path.join(DATA, p))
    return out


def main():
    if '--dis' in sys.argv:
        # python event_usage.py --dis gedit/e01/event.stb [function id]   (stbdis with the EVENT/DUNGEON ext names instead of the monster ones)
        i = sys.argv.index('--dis')
        stbdis.ext_names = lambda order=None: event_names()
        sys.argv = [sys.argv[0]] + sys.argv[i + 1:]
        stbdis.main()
        return
    names = event_names()
    use = defaultdict(lambda: defaultdict(int))
    per_file = {}
    for p in event_files():
        rel = os.path.relpath(p, DATA).replace('\\', '/')
        try:
            r = scan(p, names)
        except Exception as e:
            print('fail', rel, e)
            continue
        per_file[rel] = r
        for fid, calls in r.items():
            for c in calls:
                use[c][rel] += 1
    if '--json' in sys.argv:
        json.dump({'files': per_file}, open(sys.argv[sys.argv.index('--json') + 1], 'w'))
    print('files:', len(per_file), ' distinct ext ids:', len(use))
    for i in sorted(k for k in use if k is not None):
        fs = use[i]
        print('%5d %-28s %5d calls in %3d files' % (i, names.get(i, '???'), sum(fs.values()), len(fs)))


if __name__ == '__main__':
    main()
