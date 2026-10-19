"""Work out the file name PCSX2 will use for each disc texture, and check it against real dumps.

PCSX2 (GSTextureCache.cpp / GSTextureReplacements.cpp, v2.8.2) names a texture
    <TEX0Hash>-<CLUTHash>-<bits>.png            (hex without zero padding; bits is 8 digits)
  TEX0Hash : XXH3-64 of the texture as the GS holds it. For 4/8/32-bit textures at least one GS block
             big, that is the raw swizzled 256-byte blocks in left-to-right, top-to-bottom block order.
             Smaller textures are hashed unswizzled, one byte per pixel (indices) or 4 bytes (RGBA).
  CLUTHash : XXH3-64 of the 256 (or 16) palette entries as u32 RGBA with the raw PS2 alpha (0x80 = opaque)
  bits     : PSM | TW << 6 | TH << 10      (TEXA bits are only used for 16-bit formats)

Not handled: 16/24-bit textures, non-power-of-two sizes (the game draws those through regions).

Usage: python -I scripts/pcsx2_names.py data tools/pcsx2/textures/SCUS-97111/dumps
"""
import sys
from pathlib import Path

import numpy as np
import xxhash

sys.path.insert(0, str(Path(__file__).parent))
from gs_tables import COLUMNTABLE4, COLUMNTABLE8, COLUMNTABLE32  # noqa: E402
from tim2 import scan  # noqa: E402

PSM = {32: 0x00, 8: 0x13, 4: 0x14}
_T8 = np.array(COLUMNTABLE8).reshape(-1)    # pixel (y*16+x) -> byte in block
_T4 = np.array(COLUMNTABLE4).reshape(-1)    # pixel (y*32+x) -> nibble in block
_T32 = np.array(COLUMNTABLE32).reshape(-1)  # pixel (y*8+x)  -> word in block


def _tiles(a, bh, bw):
    """(H, W) -> (H/bh * W/bw, bh*bw) pixels of each block, blocks in raster order."""
    h, w = a.shape[:2]
    return a.reshape(h // bh, bh, w // bw, bw, *a.shape[2:]).swapaxes(1, 2).reshape(-1, bh * bw, *a.shape[2:])


def gs_block_bytes(t):
    """The texture's bytes as hashed by PCSX2, or None if the format isn't supported."""
    w, h = t.width, t.height
    if t.bpp == 8:
        idx = t.indices().reshape(h, w)
        if w < 16 or h < 16:
            return idx.tobytes()
        tiles = _tiles(idx, 16, 16)
        out = np.empty_like(tiles)
        out[:, _T8] = tiles
        return out.tobytes()
    if t.bpp == 4:
        idx = t.indices().reshape(h, w)
        if w < 32 or h < 16:
            return idx.tobytes()
        tiles = _tiles(idx, 16, 32)
        nib = np.empty_like(tiles)
        nib[:, _T4] = tiles
        return (nib[:, 0::2] | (nib[:, 1::2] << 4)).astype(np.uint8).tobytes()
    if t.bpp == 32:
        raw = np.frombuffer(t.data, np.uint8, w * h * 4, t.image_off)
        if w < 8 or h < 8:
            return raw.tobytes()
        words = _tiles(raw.view("<u4").reshape(h, w), 8, 8)
        out = np.empty_like(words)
        out[:, _T32] = words
        return out.tobytes()
    return None


def pcsx2_name(t):
    """File name (without .png) PCSX2 will look for, or None if this texture type isn't supported yet."""
    w, h = t.width, t.height
    if t.bpp not in PSM or w & (w - 1) or h & (h - 1):
        return None
    data = gs_block_bytes(t)
    name = f"{xxhash.xxh3_64_intdigest(data):x}"
    if t.bpp in (4, 8):
        name += f"-{xxhash.xxh3_64_intdigest(t.clut32()):x}"
    bits = PSM[t.bpp] | (w.bit_length() - 1) << 6 | (h.bit_length() - 1) << 10
    return f"{name}-{bits:08x}"


def main(root, dumps):
    wanted = {f.stem for f in Path(dumps).rglob("*.png")}
    found = {}
    for f in sorted(Path(root).rglob("*")):
        if not f.is_file():
            continue
        for k, t in enumerate(scan(f.read_bytes())):
            name = pcsx2_name(t)
            if name in wanted:
                found.setdefault(name, []).append(f"{f.relative_to(root)}#{k}")
    for n in sorted(wanted):
        print(f"{'MATCH ' if n in found else 'no    '} {n}  {found.get(n, [''])[0]}")
    print(f"\n{len(found)}/{len(wanted)} dumped textures reproduced from the disc by name")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
