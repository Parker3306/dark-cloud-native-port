"""Dump the 2D menu/HUD textures of the game to PNG files plus a contact sheet per container.

Usage: python -I tools/dc/ui_dump.py [--out work/ui] <file or dir under data/> ...
Every .img/.pak/.pac/.chr is walked: packs entry by entry (recursively), IMG/IM2 containers texture by texture
(IM2 = swizzled 8-bit).  Output: <out>/<container>/<entry path>/<name>.png, <out>/<container>/_sheet.png and
<out>/index.txt (name, size, bpp, clut colours).
"""
import argparse
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent.parent / "scripts"))
import img as imgmod  # noqa: E402
import pack as packmod  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

ROOT = HERE.parent.parent / "data"


def walk(data, label, found):
    """Collect (label, name, Tim2) for all textures reachable from `data`."""
    magic = data[:4]
    if magic in (b"IMG\0", b"IM2\0"):
        for name, t in imgmod.textures(data):
            found.append((label, name, t))
        return
    if magic == b"TIM2":
        t = imgmod.Tim2(data, 0)
        if t.valid():
            found.append((label, "tim2", t))
        return
    try:
        p = packmod.parse(data)
    except Exception:
        return
    for e in p.entries:
        walk(e.data, label + "/" + e.name, found)


def sheet(items, path, cols=4, cell=300):
    if not items:
        return
    rows = (len(items) + cols - 1) // cols
    S = Image.new("RGB", (cols * cell, rows * (cell + 16)), (40, 40, 48))
    d = ImageDraw.Draw(S)
    for i, (lab, im) in enumerate(items):
        x, y = (i % cols) * cell, (i // cols) * (cell + 16)
        bg = Image.new("RGBA", im.size, (90, 90, 100, 255))
        bg.alpha_composite(im)
        sc = min((cell - 4) / im.width, (cell - 4) / im.height, 1.0)
        th = bg.convert("RGB").resize((max(1, int(im.width * sc)), max(1, int(im.height * sc))), Image.NEAREST)
        S.paste(th, (x + 2, y + 16))
        d.text((x + 3, y + 2), "%s %dx%d" % (lab[:34], im.width, im.height), fill=(255, 255, 160))
    S.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="work/ui")
    ap.add_argument("paths", nargs="+")
    a = ap.parse_args()
    out = Path(a.out)
    idx = []
    for arg in a.paths:
        base = Path(arg)
        if not base.is_absolute():
            base = ROOT / arg
        files = [base] if base.is_file() else sorted(p for p in base.rglob("*") if p.is_file() and p.suffix.lower() in (".img", ".pak", ".pac", ".chr", ".tm2"))
        for f in files:
            rel = f.relative_to(ROOT).as_posix() if ROOT in f.parents else f.name
            data = f.read_bytes()
            found = []
            walk(data, "", found)
            ims = []
            for lab, name, t in found:
                try:
                    im = t.decode()
                except Exception as ex:
                    print("decode fail", rel, lab, name, ex)
                    continue
                d = out / rel.replace("/", "__") / lab.strip("/")
                d.mkdir(parents=True, exist_ok=True)
                im.save(d / (name + ".png"))
                ims.append(((lab.strip("/") + "/" if lab else "") + name, im))
                idx.append("%s\t%s\t%s\t%dx%d\t%dbpp\tclut%d" % (rel, lab, name, t.width, t.height, t.bpp, t.clut_colors))
            if ims:
                sheet(ims, out / rel.replace("/", "__") / "_sheet.png")
            print(rel, len(ims), "textures")
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "index.txt", "a") as fh:
        fh.write("\n".join(idx) + "\n")


if __name__ == "__main__":
    main()
