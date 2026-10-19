"""Check the TIM2 decoder against textures PCSX2 dumped from the running game.

For each PNG in the PCSX2 dumps folder, find disc textures of the same size whose pixels match.
Usage: python -I scripts/match_dumps.py data tools/pcsx2/textures/SCUS-97111/dumps
"""
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from tim2 import scan  # noqa: E402


def main(root, dumps):
    wanted = {}
    for f in sorted(Path(dumps).rglob("*.png")):
        im = Image.open(f).convert("RGBA")
        wanted[f.name] = np.asarray(im).astype(int)
        print(f"dump {f.name}: {im.size}")
    by_size = defaultdict(list)
    for name, a in wanted.items():
        by_size[(a.shape[1], a.shape[0])].append(name)

    best = {n: (1e9, None) for n in wanted}
    for f in sorted(Path(root).rglob("*")):
        if not f.is_file():
            continue
        data = f.read_bytes()
        for k, t in enumerate(scan(data)):
            names = by_size.get((t.width, t.height))
            if not names:
                continue
            px = np.asarray(t.decode()).astype(int)
            for n in names:
                rgb_diff = np.abs(px[..., :3] - wanted[n][..., :3]).mean()
                alpha_diff = np.abs(px[..., 3] - wanted[n][..., 3]).mean()
                if rgb_diff < best[n][0]:
                    best[n] = (rgb_diff, f"{f.relative_to(root)}#{k} bpp={t.bpp} alpha_diff={alpha_diff:.1f}")
    print()
    for n, (d, where) in best.items():
        print(f"{n[:30]:30} rgb_diff={d:7.3f}  {where}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
