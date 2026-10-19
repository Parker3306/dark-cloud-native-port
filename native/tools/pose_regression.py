"""Regression check for the animation code: CRC of dcpose output (all joint matrices) for every playable character at three motion times.
Not a correctness test (that is native/tools/pose_diff_live.py and check_skin_live.py against the running game), just a tripwire for accidental changes.
Usage: python -I native/tools/pose_regression.py [--update]"""
import subprocess, sys, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
iso = str(ROOT / "iso" / "Dark Cloud (USA).iso")
exe = ROOT / "native" / "build" / "core_release" / "dcpose.exe"
golden = ROOT / "native" / "dccore" / "tests" / "golden" / "pose_golden.txt"
packs = ["c01d", "c04b", "c05a", "c10b", "c18a"]
times = ["10", "55.5", "300.25"]
lines = []
for c in packs:
    for t in times:
        out = subprocess.run([str(exe), iso, f"dun/mainchara/{c}.chr", t], capture_output=True, text=True).stdout
        lines.append(f"{c} {t} {zlib.crc32(out.encode()) & 0xFFFFFFFF:08x} {out.count(chr(10))}")
if "--update" in sys.argv or not golden.exists():
    golden.write_text("\n".join(lines) + "\n"); print("wrote", golden)
else:
    old = golden.read_text().split()
    new = "\n".join(lines).split()
    print("pose regression:", "OK" if old == new else "CHANGED")
    if old != new:
        for a, b in zip(golden.read_text().splitlines(), lines):
            if a != b: print(" was", a, "now", b)
        sys.exit(1)
