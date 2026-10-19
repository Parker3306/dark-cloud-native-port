"""Which external functions does an event function need, following internal calls?
usage: python tools/dc/stb_closure.py gedit/e01/event.stb 128 256 [150 ...]      (path relative to data/)
Prints the external function names (event + dungeon tables) in first-use order, with call counts."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import event_usage as eu
import stbtext


def closure(path, fids):
    b = open(os.path.join(eu.DATA, path), 'rb').read()
    names = eu.event_names()
    code, fl = eu.functions_of(b)
    byrec = {rec: (fid, pc, end) for fid, rec, pc, end in fl}
    byfid = {fid: rec for fid, rec, pc, end in fl if fid is not None}
    seen = set()
    todo = [byfid[f] for f in fids if f in byfid]
    order = []
    counts = {}
    while todo:
        rec = todo.pop(0)
        if rec in seen or rec not in byrec:
            continue
        seen.add(rec)
        fid, pc, end = byrec[rec]
        wanted = set()
        for line in stbtext.text_function(b, code, names, pc, end, wanted):
            for m in re.finditer(r'\b(_[A-Z0-9_]+)\(', line):
                n = m.group(1)
                if n not in counts:
                    order.append(n)
                counts[n] = counts.get(n, 0) + 1
        todo.extend(sorted(wanted - seen))
    return order, counts


def main():
    path = sys.argv[1]
    fids = [int(a) for a in sys.argv[2:]]
    order, counts = closure(path, fids)
    print('%d distinct external functions' % len(order))
    for n in order:
        print('  %-28s %d' % (n, counts[n]))


if __name__ == '__main__':
    main()
