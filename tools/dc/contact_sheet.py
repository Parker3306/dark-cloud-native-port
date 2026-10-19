"""Side-by-side comparison image: original model render vs AI model render (front and three-quarter), labelled.

Usage: python -I tools/dc/contact_sheet.py <out.png> <left_title> <left_front> <left_34> <right_title> <right_front> <right_34>
"""
import sys

from PIL import Image, ImageDraw

out, lt, lf, l34, rt, rf, r34 = sys.argv[1:8]
W = 640
bg = (58, 58, 64)


def load(path):
    im = Image.open(path).convert("RGBA")
    base = Image.new("RGBA", im.size, bg + (255,))
    base.alpha_composite(im)
    return base.convert("RGB").resize((W, W), Image.LANCZOS)


sheet = Image.new("RGB", (W * 4 + 30, W + 40), (30, 30, 34))
d = ImageDraw.Draw(sheet)
for i, (title, p) in enumerate(((lt, lf), (lt, l34), (rt, rf), (rt, r34))):
    sheet.paste(load(p), (i * (W + 10), 40))
    d.text((i * (W + 10) + 10, 12), title, fill=(235, 235, 235))
sheet.save(out)
print("saved", out, sheet.size)
