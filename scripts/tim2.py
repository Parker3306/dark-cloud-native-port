"""TIM2 (PS2 texture) scanner/decoder for Dark Cloud's data files.

Textures sit inside .img/.chr/.pak/.pac/.mpd/.tm2 containers, so rather than parse every container we
scan for the "TIM2" magic and validate the header. A hit is only kept if its sizes add up.

Usage (survey):  python -I scripts/tim2.py data
Usage (decode):  python -I scripts/tim2.py data --png work/png
"""
import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

IMAGE_TYPES = {1: 16, 2: 24, 3: 32, 4: 4, 5: 8}


class Tim2:
    def __init__(self, data, pos):
        # File header: 'TIM2', version u8, format u8, pictures u16, 8 pad bytes. Picture header follows
        # (immediately for format 0, 128-byte aligned for format 1).
        _, ver, fmt, count = struct.unpack_from("<4sBBH", data, pos)
        self.pos, self.count = pos, count
        hdr = pos + (0x10 if fmt == 0 else 0x80)
        (self.total, self.clut_size, self.image_size, self.header_size, self.clut_colors,
         self.pict_fmt, self.mipmaps, self.clut_type, self.image_type, self.width, self.height,
         self.tex0, self.tex1, self.regs, self.texclut) = struct.unpack_from("<IIIHHBBBBHHQQII", data, hdr)
        self.hdr = hdr
        self.bpp = IMAGE_TYPES.get(self.image_type & 0x3F)
        self.image_off = hdr + self.header_size
        self.clut_off = self.image_off + self.image_size
        self.data = data
        self.swizzled = False     # True for 8-bit textures of "IM2" containers: pixels are stored in GS page order

    def valid(self):
        if self.bpp is None or not (1 <= self.width <= 1024 and 1 <= self.height <= 1024):
            return False
        base =self.width * self.height * self.bpp // 8
        return (base <= self.image_size and self.header_size >= 48
                and self.image_off + self.image_size + self.clut_size <= len(self.data)
                and self.total >= self.header_size + self.image_size + self.clut_size - 16)

    def decode(self, raw_alpha=False):
        """Return an RGBA PIL image of the base mip level.

        raw_alpha=True keeps the PS2 alpha scale (0x80 = opaque), which is what PCSX2 dumps and loads."""
        w, h, d = self.width, self.height, self.data
        n = w * h * self.bpp // 8
        raw = np.frombuffer(d, np.uint8, n, self.image_off)
        if self.bpp == 32:
            rgba = raw.reshape(h, w, 4).copy()
        elif self.bpp == 24:
            rgba = np.dstack([raw.reshape(h, w, 3), np.full((h, w), 255, np.uint8)])
        elif self.bpp == 16:
            v = raw.view("<u2").reshape(h, w).astype(np.uint32)
            rgba = np.dstack([(v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31,
                              ((v >> 10) & 31) * 255 // 31, np.where(v >> 15, 255, 0)]).astype(np.uint8)
        else:
            pal = self._palette(raw_alpha)
            idx = self.indices()
            rgba = pal[idx[: w * h]].reshape(h, w, 4)
        if self.bpp == 32 and not raw_alpha:
            rgba[..., 3] = np.minimum(rgba[..., 3].astype(np.uint16) * 2, 255).astype(np.uint8)
        return Image.fromarray(rgba, "RGBA")

    def indices(self):
        """Palette indices, one byte per pixel, row-major (what PCSX2 hashes for 4/8-bit textures)."""
        n = self.width * self.height * self.bpp // 8
        raw = np.frombuffer(self.data, np.uint8, n, self.image_off)
        if self.bpp == 8:
            if self.swizzled and self.width % 16 == 0 and self.height % 16 == 0:
                return raw[unswizzle8_map(self.width, self.height)]
            return raw
        return np.dstack([raw & 15, raw >> 4]).reshape(-1)  # low nibble is the first pixel

    def clut32(self):
        """Palette as the GS palette cache holds it: u32 per entry, R|G<<8|B<<16|A<<24, PS2 alpha (0x80 = opaque)."""
        pal = self._palette(raw_alpha=True)
        return np.ascontiguousarray(pal).tobytes()

    def _palette(self, raw_alpha=False):
        ncol = 256 if self.bpp == 8 else 16
        ctype = self.clut_type & 0x3F
        raw = np.frombuffer(self.data, np.uint8, self.clut_size, self.clut_off)
        if ctype == 3:
            pal = raw[: ncol * 4].reshape(ncol, 4).copy()
            if not raw_alpha:
                pal[:, 3] = np.minimum(pal[:, 3].astype(np.uint16) * 2, 255)
        elif ctype == 2:
            pal = np.hstack([raw[: ncol * 3].reshape(ncol, 3), np.full((ncol, 1), 255, np.uint8)])
        else:
            v = raw.view("<u2")[:ncol].astype(np.uint32)
            pal = np.stack([(v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31,
                            ((v >> 10) & 31) * 255 // 31, np.where(v >> 15, 255, 0)], 1).astype(np.uint8)
        # 8-bit CLUTs stored for the GS are "swizzled": entries 8..15 and 16..23 of every 32 swap places.
        # Bit 7 of clut_type set means the palette is already in linear order.
        if self.bpp == 8 and not (self.clut_type & 0x80):
            order = np.arange(256)
            blk = (order // 8) % 4
            order = np.where(blk == 1, order + 8, np.where(blk == 2, order - 8, order))
            pal = pal[order]
        return pal


_UNSWIZZLE_CACHE = {}


def unswizzle8_map(w, h):
    """Source index (in the swizzled buffer) of every raster pixel of a w x h PSMT8 image (w, h multiples of 16)."""
    key = (w, h)
    if key not in _UNSWIZZLE_CACHE:
        y, x = np.mgrid[0:h, 0:w]
        block = (y & ~0xF) * w + (x & ~0xF) * 2
        swap = (((y + 2) >> 2) & 1) * 4
        pos_y = (((y & ~3) >> 1) + (y & 1)) & 7
        column = pos_y * w * 2 + ((x + swap) & 7) * 4
        byte = ((y >> 1) & 1) + ((x >> 2) & 2)
        _UNSWIZZLE_CACHE[key] = (block + column + byte).reshape(-1)
    return _UNSWIZZLE_CACHE[key]


def scan(data):
    pos = 0
    while True:
        pos = data.find(b"TIM2", pos)
        if pos < 0:
            return
        try:
            t = Tim2(data, pos)
            if t.valid():
                yield t
        except (struct.error, ValueError):
            pass
        pos += 4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--png", help="write decoded PNGs here")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    root = Path(args.root)
    stats, fmts, per_ext, total = Counter(), Counter(), Counter(), 0
    for f in sorted(root.rglob("*")):
        if not f.is_file():
            continue
        data = f.read_bytes()
        for k, t in enumerate(scan(data)):
            total += 1
            fmts[(t.bpp, t.width, t.height)] += 1
            per_ext[f.suffix.lower()] += 1
            if args.png and (not args.limit or total <= args.limit):
                out = Path(args.png) / f.relative_to(root).with_suffix("")
                out.parent.mkdir(parents=True, exist_ok=True)
                t.decode().save(f"{out}_{k:02d}.png")
    print(f"{total} textures")
    print("by container:", dict(per_ext.most_common()))
    print("bpp counts:", dict(Counter(k[0] for k in fmts.elements())))
    print("most common sizes:", [(k, v) for k, v in fmts.most_common(8)])


if __name__ == "__main__":
    main()
