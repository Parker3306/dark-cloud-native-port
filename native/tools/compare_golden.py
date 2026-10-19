"""Diff the Python and C++ digest files line by line. Usage: python -I native/tools/compare_golden.py <py.txt> <cpp.txt> [--show N]"""
import collections
import sys

a = open(sys.argv[1], encoding="utf-8").read().split("\n")
b = open(sys.argv[2], encoding="utf-8").read().split("\n")
show = int(sys.argv[sys.argv.index("--show") + 1]) if "--show" in sys.argv else 8
print(f"python {len(a)} lines, c++ {len(b)} lines")
bad = collections.Counter()
shown = collections.Counter()
sb = set(b)
sa = set(a)
only_a = [l for l in a if l not in sb]
only_b = [l for l in b if l not in sa]
for l in only_a:
    bad[l[:1]] += 1
print("lines only in python by kind:", dict(bad), "| only in c++:", len(only_b))
for l in only_a:
    k = l[:1]
    if shown[k] < show:
        shown[k] += 1
        print("PY :", l)
        # find the c++ line with the same first fields
        key = " ".join(l.split(" ")[:2])
        m = [x for x in only_b if x.startswith(key + " ") or x == key]
        print("C++:", m[0] if m else "(missing)")
sys.exit(1 if only_a or only_b else 0)
