"""Compose PNGs side by side on a grey background (optionally with a pixel grid and rectangles) to look at them.
Usage: ui_view.py out.png [--scale N] [--grid N] [--rect x,y,w,h[,label]]... in1.png in2.png ...
(--rect applies to the FIRST image; the grid has a line every N source pixels with coordinates.)"""
import sys

from PIL import Image, ImageDraw

a = sys.argv[1:]
out = a[0]
scale, grid, rects, ins = 1, 0, [], []
i = 1
while i < len(a):
    if a[i] == "--scale":
        scale = int(a[i + 1]); i += 2
    elif a[i] == "--grid":
        grid = int(a[i + 1]); i += 2
    elif a[i] == "--rect":
        rects.append(a[i + 1].split(",")); i += 2
    else:
        ins.append(a[i]); i += 1
ims = [Image.open(p).convert("RGBA") for p in ins]
W = sum(im.width * scale + 8 for im in ims)
H = max(im.height * scale for im in ims) + 8
S = Image.new("RGBA", (W, H), (96, 96, 110, 255))
x = 4
for k, im in enumerate(ims):
    big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    S.alpha_composite(big, (x, 4))
    if k == 0:
        d = ImageDraw.Draw(S)
        if grid:
            for gx in range(0, im.width, grid):
                d.line([(x + gx * scale, 4), (x + gx * scale, 4 + im.height * scale)], fill=(255, 255, 0, 70))
                d.text((x + gx * scale + 1, 5), str(gx), fill=(255, 255, 0, 255))
            for gy in range(0, im.height, grid):
                d.line([(x, 4 + gy * scale), (x + im.width * scale, 4 + gy * scale)], fill=(255, 255, 0, 70))
                d.text((x + 1, 4 + gy * scale + 1), str(gy), fill=(255, 255, 0, 255))
        for r in rects:
            rx, ry, rw, rh = (int(v, 0) for v in r[:4])
            d.rectangle([x + rx * scale, 4 + ry * scale, x + (rx + rw) * scale - 1, 4 + (ry + rh) * scale - 1], outline=(255, 0, 0, 255))
            if len(r) > 4:
                d.text((x + rx * scale + 2, 4 + ry * scale + 2), r[4], fill=(255, 60, 60, 255))
    x += im.width * scale + 8
S.convert("RGB").save(out)
print(out, S.size)
