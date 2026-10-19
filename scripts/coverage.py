"""Explain why dumped textures are (or aren't) covered by names computed from the disc.

For every PNG in the PCSX2 dumps folder: exact name on disc, or texture data on disc with a different
palette, or palette on disc but different data, or neither (generated at runtime / not a plain TIM2).

Usage: python -I scripts/coverage.py data tools/pcsx2/textures/SCUS-97111/dumps
"""
import sys
from collections import Counter, defaultdict
from pathlib import Path

import xxhash
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from pcsx2_names import gs_block_bytes  # noqa: E402
from tim2 import scan  # noqa: E402


def main(root, dumps):
    tex_hashes, clut_hashes = set(), set()
    for f in sorted(Path(root).rglob("*")):
        if not f.is_file():
            continue
        for t in scan(f.read_bytes()):
            if t.bpp not in (4, 8, 32) or t.width & (t.width - 1) or t.height & (t.height - 1):
                continue
            tex_hashes.add(xxhash.xxh3_64_intdigest(gs_block_bytes(t)))
            if t.bpp in (4, 8):
                clut_hashes.add(xxhash.xxh3_64_intdigest(t.clut32()))

    cats, examples = Counter(), defaultdict(list)
    for f in sorted(Path(dumps).glob("*.png")):
        parts = f.stem.split("-")
        th = int(parts[0], 16)
        ch = int(parts[1], 16) if len(parts) == 3 else None
        bits = int(parts[-1], 16)
        size = Image.open(f).size
        data_ok = th in tex_hashes
        clut_ok = ch is None or ch in clut_hashes
        cat = ("exact" if data_ok and clut_ok else "data on disc, palette differs" if data_ok else
               "palette on disc, data differs" if clut_ok and ch is not None else
               "neither (runtime-generated?)")
        kind = {0: "32-bit", 0x13: "8-bit", 0x14: "4-bit"}.get(bits & 0x3F, f"psm{bits & 0x3F:x}")
        cats[(cat, kind)] += 1
        examples[(cat, kind)].append(f"{f.stem} {size[0]}x{size[1]}")
    for k, n in sorted(cats.items()):
        print(f"{n:4}  {k[1]:7} {k[0]}")
    print()
    for k, ex in sorted(examples.items()):
        if k[0] != "exact":
            print(k, ex[:3])


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
