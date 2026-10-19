"""Crop the same region from several screenshots, enlarge it, and stack the crops for comparison.

Usage: python -I scripts/crop_compare.py out.png x0 y0 x1 y1 zoom shot1.png shot2.png ...
"""
import sys

from PIL import Image


def main(out, x0, y0, x1, y1, zoom, *shots):
    box = tuple(int(v) for v in (x0, y0, x1, y1))
    z = int(zoom)
    crops = [Image.open(s).convert("RGB").crop(box) for s in shots]
    crops = [c.resize((c.width * z, c.height * z), Image.NEAREST) for c in crops]
    sheet = Image.new("RGB", (crops[0].width, sum(c.height for c in crops) + 8 * (len(crops) - 1)), (255, 0, 255))
    y = 0
    for c in crops:
        sheet.paste(c, (0, y))
        y += c.height + 8
    sheet.save(out)


if __name__ == "__main__":
    main(*sys.argv[1:])
