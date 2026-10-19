"""List the entries of a .pak/.pac container (recursively into nested .pak/.pac) with sizes.

usage: python -I tools/dc/pak_ls.py <path under data/ or absolute>   e.g. commenu/a_usa/savetex.pak
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pack  # noqa: E402

ROOT = os.path.join(HERE, '..', '..')


def walk(d, ind=0):
    for e in pack.parse(d).entries:
        print(' ' * ind + e.name, len(e.data))
        if ind < 4 and e.name.lower().endswith(('.pak', '.pac')):
            try:
                walk(e.data, ind + 2)
            except Exception as x:  # noqa: BLE001
                print(' ' * ind + '  (not a pack: %s)' % x)


if __name__ == '__main__':
    p = sys.argv[1]
    if not os.path.isabs(p) and not os.path.exists(p):
        p = os.path.join(ROOT, 'data', p)
    walk(open(p, 'rb').read())
