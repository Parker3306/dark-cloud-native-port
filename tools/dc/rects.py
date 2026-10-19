"""Extract 2D sprite draw calls (dst/src rectangles) from a function of work/decomp/all_dun.c.

Usage: python -I tools/dc/rects.py <function name or hex addr> [--all] [--vars]
The decompiler builds CRect<i> structs in stack slots (uStack_460 = sp-0x460, fields at +0,+4,+8,+c = x,y,w,h):
the script follows the last assignment to every slot before each call and prints
  L<line>  callee(args...) with &slot arguments expanded to [x,y,w,h].
CRect order: DrawMenu2DSprite(tex, dst rect on the 640x448 screen, src rect in the texture, alpha ...).
"""
import json
import re
import sys
from pathlib import Path

R = Path(__file__).resolve().parent.parent.parent
rows = json.load(open(R / "work" / "funcs.json")) if (R / "work" / "funcs.json").exists() else None
SRC = R / "work" / "decomp" / "all_dun.c"
CALLEES = re.compile(r"(set2DSprite[\w<>]*|Myset2DSprite[\w<>]*|DrawMenu2DSprite[\w<>]*|DrawMenuNumber[\w<>]*|DrawMenuWaku[\w<>]*|MenuHelpWinDraw[\w<>]*|DrawMenuHelpWindow[\w<>]*|DrawIconParts[\w<>]*|DrawMenuColorGradation[\w<>]*|setColSprite[\w<>]*|DrawAttachNumberOrWeapon[\w<>]*)\(")
ASSIGN = re.compile(r"^\s*(\w*Stack_[0-9a-f]+)\s*=\s*(.*);\s*$")
VASSIGN = re.compile(r"^\s*((?:[ilufbcs]Var|param_|in_|unaff_|lVar|dVar|uVar|iVar|fVar|sVar|pVar|puVar|piVar|psVar|pfVar)\w*)\s*=\s*(.*);\s*$")


def slot(name):
    return int(name.rsplit("_", 1)[1], 16)


def short(e):
    e = re.sub(r"\(int\)|\(float\)|\(long\)|\(uint\)|\(undefined4\)", "", e)
    return e.strip()


def find_func(lines, key):
    for i, l in enumerate(lines):
        m = re.match(r"// ==== (?:FAILED )?(\S+) @ (\w+)", l)
        if m and (m.group(1) == key or m.group(1).split("__")[0] == key or m.group(2).lstrip("0") == key.lower().replace("0x", "").lstrip("0")):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("// ===="):
                j += 1
            return i, j
    raise SystemExit("function not found")


def main():
    key = sys.argv[1]
    show_vars = "--vars" in sys.argv
    lines = SRC.read_text(encoding="latin1").split("\n")
    if re.fullmatch(r'\d+-\d+', key):
        a, b = (int(x) for x in key.split('-'))
        a -= 1
    else:
        a, b = find_func(lines, key)
    # a call whose argument list starts on the next line: glue it
    for q in range(a + 1, b):
        if lines[q].lstrip().startswith("(") and re.search(r"\w$", lines[q - 1]):
            lines[q - 1] += lines[q].strip()
            lines[q] = ""
    slots = {}
    vars_ = {}
    out = []
    # join calls spanning lines
    i = a
    while i < b:
        l = lines[i]
        # accumulate multi-line statements
        stmt = l
        k = i
        while stmt.count("(") > stmt.count(")") and k + 1 < b:
            k += 1
            stmt += " " + lines[k].strip()
        m = ASSIGN.match(l)
        if m:
            slots[slot(m.group(1))] = short(m.group(2))
        else:
            mv = VASSIGN.match(l)
            if mv:
                vars_[mv.group(1)] = short(mv.group(2))
        mc = CALLEES.search(stmt)
        if mc and not stmt.lstrip().startswith("//"):
            name = mc.group(1)
            args = stmt[mc.end():]
            args = args[:args.rfind(")")]
            parts = [p.strip() for p in re.split(r",(?![^()]*\))", args)]
            ex = []
            for p in parts:
                mm = re.fullmatch(r"&(\w*Stack_[0-9a-f]+)", p)
                if mm:
                    s0 = slot(mm.group(1))
                    ex.append("[" + ",".join(slots.get(s0 - 4 * q, "?") for q in range(4)) + "]")
                else:
                    ex.append(vars_.get(p, p) if show_vars and re.fullmatch(r"\w+", p) and p in vars_ else p)
            out.append("L%d %s(%s)" % (i + 1, name, ", ".join(ex)))
        i = k + 1
    print("\n".join(out))


if __name__ == "__main__":
    main()
