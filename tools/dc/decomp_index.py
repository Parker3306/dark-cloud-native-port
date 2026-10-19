"""Index helpers for work/decomp/all_dun.c (the raw Ghidra decompile of the main ELF + the dungeon overlay).

  decomp_index.py build                     -> work/funcs.json (line, name, address, line count of every function) -- run once
  decomp_index.py find <regex>              -> functions whose name matches (line, name, address, size in lines)
  decomp_index.py callers <text> [...]      -> which functions contain the text (call sites)
  decomp_index.py strings [regex]           -> functions that pass string literals (file names, texture names) from the data segment (0x290000-0x2a2000, overlay 0x1dabd00..)
  decomp_index.py show <name|addr> [a b]    -> print the function body (declarations stripped), optionally lines a..b of it
Note: the overlay (DUN.BIN @0x1DABD00) is labelled badly in the decompile (MoveChara contains labels such as DrawMess / MainDraw / CSeireiKing::Draw
that are really parts of the same code); function sizes in lines of those are not meaningful.
"""
import bisect
import json
import re
import sys
from pathlib import Path

R = Path(__file__).resolve().parent.parent.parent
SRC = R / "work" / "decomp" / "all_dun.c"
IDX = R / "work" / "funcs.json"


def build():
    lines = SRC.read_text(encoding="latin1").split("\n")
    heads = [(i + 1, m) for i, l in enumerate(lines) for m in [re.match(r"// ==== (?:FAILED )?(\S+) @ (\w+)", l)] if m]
    rows = []
    for k, (ln, m) in enumerate(heads):
        nxt = heads[k + 1][0] if k + 1 < len(heads) else len(lines)
        rows.append((ln, m.group(1), m.group(2), nxt - ln))
    IDX.write_text(json.dumps(rows))
    print(len(rows), "functions")


def rows():
    if not IDX.exists():
        build()
    return json.loads(IDX.read_text())


def find(rx):
    for r in rows():
        if re.search(rx, r[1], re.I):
            print(r[0], r[1], r[2], r[3])


def callers(texts):
    lines = SRC.read_text(encoding="latin1").split("\n")
    rs = rows()
    starts = [r[0] for r in rs]
    for t in texts:
        out = {}
        for i, l in enumerate(lines):
            if t in l and "// ====" not in l:
                r = rs[bisect.bisect_right(starts, i + 1) - 1]
                out.setdefault((r[1], r[2]), []).append(i + 1)
        print(t, {k: v[:3] for k, v in out.items()})


def strings(rx=None):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import mem
    lines = SRC.read_text(encoding="latin1").split("\n")
    rs = rows()
    starts = [r[0] for r in rs]
    res = {}
    for i, l in enumerate(lines):
        for m in re.finditer(r"0x(2[89a-f][0-9a-f]{4}|1d[a-f][0-9a-f]{5})\b", l):
            a = int(m.group(1), 16)
            try:
                s = mem.cstr(a, 40)
            except Exception:
                continue
            if len(s) >= 3 and re.fullmatch(r"[\x20-\x7e]+", s) and re.search(r"[a-zA-Z]", s):
                r = rs[bisect.bisect_right(starts, i + 1) - 1]
                res.setdefault((r[1], r[2]), set()).add((hex(a), s))
    for k, v in sorted(res.items(), key=lambda kv: kv[0][1]):
        if rx is None or re.search(rx, k[0] + str(v), re.I):
            print(k[0], k[1], sorted(v))


def show(key, a=None, b=None):
    lines = SRC.read_text(encoding="latin1").split("\n")
    for r in rows():
        if r[1] == key or r[1].split("__")[0] == key or r[2].lstrip("0") == key.lower().replace("0x", "").lstrip("0"):
            body = lines[r[0] - 1:r[0] - 1 + r[3]]
            decl = re.compile(r"^  (undefined\d?|int|float|long|uint|short|char|byte|ulong|double|bool|ushort|void \*|\w+ \*)\s+\**\w+(\[\w*\])?;$")
            body = [l for l in body if not decl.match(l) and l.strip() and not l.startswith("/* WARNING")]
            if a is not None:
                body = body[int(a):int(b)]
            print("\n".join(l[:170] for l in body))
            return
    print("not found")


if __name__ == "__main__":
    c = sys.argv[1]
    if c == "build":
        build()
    elif c == "find":
        find(sys.argv[2])
    elif c == "callers":
        callers(sys.argv[2:])
    elif c == "strings":
        strings(sys.argv[2] if len(sys.argv) > 2 else None)
    elif c == "show":
        show(sys.argv[2], *(sys.argv[3:5] if len(sys.argv) > 4 else []))
