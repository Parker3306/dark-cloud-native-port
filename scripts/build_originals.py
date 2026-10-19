"""Export every supported disc texture as a PNG named the way PCSX2 expects (duplicates collapsed).

Output PNGs keep the PS2 alpha scale (0x80 = opaque), exactly like PCSX2's own dumps, so each file is a
drop-in "replacement" identical to the original. Upscale these, keep the file names, and PCSX2 will use them.

Usage: python -I scripts/build_originals.py data work/originals
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pcsx2_names import pcsx2_name  # noqa: E402
from tim2 import scan  # noqa: E402


def main(root, out):
    root, out = Path(root), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    seen, total, skipped = set(), 0, {}
    for f in sorted(root.rglob("*")):
        if not f.is_file():
            continue
        for t in scan(f.read_bytes()):
            total += 1
            name = pcsx2_name(t)
            if name is None:
                why = "non-power-of-two" if (t.width & (t.width - 1) or t.height & (t.height - 1)) else f"{t.bpp}-bit"
                skipped[why] = skipped.get(why, 0) + 1
                continue
            if name in seen:
                continue
            seen.add(name)
            t.decode(raw_alpha=True).save(out / f"{name}.png", compress_level=1)
    print(f"{total} textures on disc -> {len(seen)} unique PCSX2 names written to {out}")
    print("not handled yet:", skipped)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
