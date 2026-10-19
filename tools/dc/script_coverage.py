"""Which external script functions do the monster scripts call, and which of them does dccore/monster_ai.cpp implement?
usage: script_coverage.py"""
import glob
import os
import re
import struct
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import stbdis

ROOT = os.path.join(HERE, '..', '..')


def scan(path, names):
    b = open(path, 'rb').read()
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
    used = set()
    for rec in sorted(records):
        pc = code + w(rec)
        nxt = [r for r in sorted(records) if r > pc]
        end = nxt[0] if nxt else len(b)
        for line in stbdis.disassemble(b, names, pc, end, code):
            m = re.search(r'\(id (\d+)\)', line)
            if m:
                used.add(int(m.group(1)))
    return used


def main():
    src = open(os.path.join(ROOT, 'native', 'dccore', 'src', 'monster_ai.cpp'), encoding='latin1').read()
    handled = set(int(x) for x in re.findall(r'case (\d+):', src[src.index('bool MonsterWorld::ext'):]))
    ignored = set()
    for m in re.finditer(r'case ([\d: case]+)\s*return ignore', src):
        ignored |= set(int(x) for x in re.findall(r'\d+', m.group(1)))
    names = stbdis.ext_names(order=('monster',))
    use = defaultdict(list)
    for path in sorted(glob.glob(os.path.join(ROOT, 'data', 'dun', 'monstor', '*.stb'))):
        try:
            ids = scan(path, names)
        except Exception as e:
            print('fail', path, e)
            continue
        for i in ids:
            use[i].append(os.path.basename(path)[:-4])
    print('monster scripts:', len(glob.glob(os.path.join(ROOT, 'data', 'dun', 'monstor', '*.stb'))))
    print('ext ids used:', len(use), ' handled:', len(handled & set(use)), ' ignored (stub):', len(ignored & set(use)))
    print()
    for i in sorted(use):
        state = 'ok' if i in handled and i not in ignored else ('IGNORED' if i in ignored else 'MISSING')
        if state != 'ok':
            print('%4d %-28s %-8s used by %d scripts: %s' % (i, names.get(i, '?'), state, len(use[i]), ' '.join(use[i][:10])))


if __name__ == '__main__':
    main()
