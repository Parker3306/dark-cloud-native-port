"""Upscale the exported disc textures (work/originals, from build_originals.py) with Real-ESRGAN into
PCSX2's replacements folder.

PCSX2 matches a replacement to a texture by file name, so output names equal the input names. Real-ESRGAN
keeps the alpha range untouched, so the PS2 alpha scale (0x80 = opaque) survives.

Two models are used, chosen per texture (see choose_model):
  realesrgan-x4plus-anime  crisp, flat-colour graphics (logos, text, UI). Invents hard edges on soft art.
  realesrgan-x4plus        everything soft: glows, smoke, fire, gradients, painted surfaces.

Usage: python -I scripts/upscale_textures.py [--limit N] [--force-model NAME] [--jobs 2:2:2]
Re-runnable: textures already present in replacements are skipped.
"""
import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "work" / "originals"
OUT = ROOT / "tools" / "pcsx2" / "textures" / "SCUS-97111" / "replacements"
ENGINE = next((ROOT / "tools" / "realesrgan").rglob("realesrgan-ncnn-vulkan.exe"), None)
SHARP, SOFT = "realesrgan-x4plus-anime", "realesrgan-x4plus"

# Textures this small are palettes, gradients or lookup tables, not pictures. Upscaling them breaks
# effects such as fog ramps and colour-cycling, so leave them as the game made them.
MIN_SIDE = 16


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", head[16:24])


def edge_hardness(path):
    """Among pixels that have any edge at all, the share that are hard (sudden) edges. Crisp graphics score
    high; soft art with gradual shading scores low."""
    a = np.asarray(Image.open(path).convert("RGBA")).astype(np.float32)
    lum = (a[..., 0] * 0.3 + a[..., 1] * 0.59 + a[..., 2] * 0.11) * np.minimum(a[..., 3] / 128.0, 1.0)
    gx = np.abs(np.diff(lum, axis=1))[:-1, :]
    gy = np.abs(np.diff(lum, axis=0))[:, :-1]
    g = np.maximum(gx, gy) / 255.0
    edges = g > 0.03
    return float((g > 0.25).sum() / max(edges.sum(), 1))


def choose_model(path):
    return SHARP if edge_hardness(path) > HARD_THRESHOLD else SOFT


HARD_THRESHOLD = 0.2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0, help="stop after N textures (for test runs)")
    ap.add_argument("--src", choices=("originals", "dumps"), default="originals",
                    help="originals = work/originals (built from the disc); dumps = PCSX2's own dumps from play")
    ap.add_argument("--force-model", choices=(SHARP, SOFT))
    ap.add_argument("--jobs", default="2:2:2", help="Real-ESRGAN load:process:save threads")
    ap.add_argument("--report", action="store_true", help="only print which model each texture would get")
    args = ap.parse_args()

    if ENGINE is None or not (ENGINE.parent / "models" / f"{SOFT}.bin").exists():
        sys.exit("Real-ESRGAN with models not found under tools/realesrgan")

    OUT.mkdir(parents=True, exist_ok=True)
    src = SRC if args.src == "originals" else OUT.parent / "dumps"
    groups, small = {SHARP: [], SOFT: []}, 0
    for p in sorted(src.glob("*.png")):
        if (OUT / p.name).exists():
            continue
        size = png_size(p)
        if size is None or min(size) < MIN_SIDE:
            small += 1
            continue
        groups[args.force_model or choose_model(p)].append(p)
    if args.limit:
        left = args.limit
        for k in groups:
            groups[k] = groups[k][:left]
            left -= len(groups[k])
    print(f"{len(groups[SHARP])} -> {SHARP}, {len(groups[SOFT])} -> {SOFT}, {small} skipped (too small)")
    if args.report:
        return

    # Real-ESRGAN takes whole folders in one run, which avoids a start-up per texture.
    for model, files in groups.items():
        if not files:
            continue
        stage = ROOT / "work" / "stage" / model
        shutil.rmtree(stage, ignore_errors=True)
        stage.mkdir(parents=True)
        for p in files:
            (stage / p.name).hardlink_to(p)
        r = subprocess.run([str(ENGINE), "-i", str(stage), "-o", str(OUT), "-n", model, "-s", "4",
                            "-f", "png", "-j", args.jobs])
        shutil.rmtree(stage, ignore_errors=True)
        done = sum((OUT / p.name).exists() for p in files)
        print(f"{model}: {done}/{len(files)} done (exit {r.returncode})")


if __name__ == "__main__":
    main()
