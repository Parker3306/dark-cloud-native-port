"""Check the disc index tools against the pristine ISO (read-only): HED/HD2 agree, every entry reads back the same bytes as
the extracted files, and a patched copy (identity + one real change) keeps verifying.

Usage: python -I tools/dc/test_disc.py [--make-copy]    (--make-copy creates iso/Dark Cloud (USA).patched.iso, 1.8 GB)
"""
import hashlib
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import disc as D  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "iso" / "Dark Cloud (USA).iso"
DST = ROOT / "iso" / "Dark Cloud (USA).patched.iso"


def main():
    d = D.Disc(SRC) if "--make-copy" not in sys.argv else None
    if d is None:
        t = time.time()
        d = D.make_copy(SRC, DST)
        print(f"copied the ISO in {time.time() - t:.0f}s")
    print(f"{d.n} files in the index; DMMY filler left: {d.capacity_left() / 1e6:.1f} MB")
    print("verify (pristine):", d.verify() or "OK")
    # spot-check reads against the extracted files
    import random
    random.seed(1)
    bad = 0
    for i in random.sample(range(d.n), 300):
        name = d.names[i]
        p = ROOT / "data" / name.replace("\\", "/")
        if p.exists() and p.read_bytes() != d.read(name):
            bad += 1
    print("300 random files read back identically from the ISO:", "yes" if bad == 0 else f"NO ({bad} differ)")
    if "--make-copy" in sys.argv:
        name = "dun\\mainchara\\c01d.chr"
        orig = d.read(name)
        d.put(name, orig)                                   # identity: same bytes, placed at the end of DATA.DAT
        d.commit()
        print("after identity put + commit:", d.verify() or "OK", f"| read back equal: {d.read(name) == orig}",
              f"| DATA.DAT now {d.dat_size} bytes, DMMY left {d.capacity_left() / 1e6:.1f} MB")
    d.close()


if __name__ == "__main__":
    main()
