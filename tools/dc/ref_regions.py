"""Label dense surface samples of the ORIGINAL character with a body region (from its skin weights) for the UV unwrapper.

Regions come from the skeleton chains: 0 torso, 1 head, 2 left arm (+x), 3 right arm (-x), 4 left leg, 5 right leg.
Usage: python -I tools/dc/ref_regions.py <model dir> <out npz>
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import build_character as B  # noqa: E402

REGION_BONES = {
    0: {3, 30},                     # jnt1_1, jnt2_1 (pelvis / chest)
    1: {47, 50},                    # neck, head
    2: {33, 36, 37, 40},            # arm chain under chn8 (+x)
    3: {59, 62, 63, 66},            # arm chain under chn16 (-x)
    4: {18, 19, 22, 25},            # leg chain under chn5
    5: {6, 7, 10, 13},              # leg chain under chn22
}


def main(model_dir, out, auto=False, exclude=()):
    pts, w, bones = B.reference_samples(model_dir, exclude=exclude)
    region_bones = REGION_BONES
    if auto:                                                     # other characters: classify joints by position
        reg, _, _ = B.joint_regions(model_dir)
        region_bones = {r: {b for b, rr in reg.items() if rr == r} for r in range(6)}
    score = np.zeros((len(pts), 6))
    for r, bs in region_bones.items():
        for b in bs:
            if b in bones:
                score[:, r] += w[:, bones.index(b)]
    lab = score.argmax(axis=1)
    # sanity: left arm is +x
    for r in range(6):
        sel = pts[lab == r]
        print(r, len(sel), sel.mean(axis=0).round(2) if len(sel) else None)
    np.savez(out, pts=pts, lab=lab)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], auto=len(sys.argv) > 3 and sys.argv[3] == "auto", exclude=tuple(sys.argv[4].split(",")) if len(sys.argv) > 4 else ())
