"""Side-by-side contact sheet: original (nearest-neighbour 4x) next to each upscaled version.

Usage: python -I scripts/compare.py out.png original.png upscaled1.png [upscaled2.png ...]
"""
import sys

from PIL import Image


def flat(path, size=None, resample=Image.NEAREST):
    im = Image.open(path).convert("RGBA")
    im.putalpha(255)  # ignore alpha so every panel is directly comparable
    return im.resize(size, resample) if size else im


def main(out, original, *upscaled):
    ups = [flat(p) for p in upscaled]
    size = ups[0].size
    panels = [flat(original, size)] + ups
    gap = 10
    sheet = Image.new("RGB", (sum(p.width for p in panels) + gap * (len(panels) - 1), size[1]), (30, 30, 30))
    x = 0
    for p in panels:
        sheet.paste(p.convert("RGB"), (x, 0))
        x += p.width + gap
    sheet.save(out)


if __name__ == "__main__":
    main(*sys.argv[1:])
