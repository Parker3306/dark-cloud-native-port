"""For dumps whose palette is on disc but whose pixels aren't: compare each against the disc textures that
use the same palette, and report how much of the picture still matches (whole, or only in some blocks).

Usage: python -I scripts/diff_dumps.py data tools/pcsx2/textures/SCUS-97111/dumps
"""
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import xxhash
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from pcsx2_names import gs_block_bytes  # noqa: E402
from tim2 import scan  # noqa: E402


def main(root, dumps):
    by_clut = defaultdict(list)
    tex_hashes = set()
    for f in sorted(Path(root).rglob("*")):
        if not f.is_file():
            continue
        for k, t in enumerate(scan(f.read_bytes())):
            if t.bpp == 8:
                by_clut[xxhash.xxh3_64_intdigest(t.clut32())].append((f, k, t))
            if t.bpp in (4, 8, 32) and not (t.width & (t.width - 1) or t.height & (t.height - 1)):
                tex_hashes.add(xxhash.xxh3_64_intdigest(gs_block_bytes(t)))

    for f in sorted(Path(dumps).glob("*.png")):
        parts = f.stem.split("-")
        if len(parts) != 3 or int(parts[0], 16) in tex_hashes:
            continue
        ch = int(parts[1], 16)
        if ch not in by_clut:
            continue
        dump = np.asarray(Image.open(f).convert("RGBA")).astype(int)
        print(f"\n{f.stem}  dump {dump.shape[1]}x{dump.shape[0]}")
        shown = 0
        for src, k, t in by_clut[ch]:
            px = np.asarray(t.decode(raw_alpha=True)).astype(int)
            h, w = min(px.shape[0], dump.shape[0]), min(px.shape[1], dump.shape[1])
            same = (np.abs(px[:h, :w] - dump[:h, :w]).sum(-1) == 0)
            print(f"   {str(src.relative_to(root))[:48]:48}#{k} disc {t.width}x{t.height}: "
                  f"{same.mean():6.1%} of overlapping pixels identical")
            shown += 1
            if shown == 3:
                break


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
