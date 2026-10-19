"""Unpack DATA.DAT using DATA.HED.

DATA.HED is an array of 80-byte entries: name[64], u32 offset, u32 size, u32 sector index, u32 sector count.
Offsets are bytes into DATA.DAT (always a multiple of 2048).

Usage: python -I scripts/extract_data.py extracted data
"""
import struct
import sys
from pathlib import Path


def main(src, dst):
    src, dst = Path(src), Path(dst)
    hed = (src / "DATA.HED").read_bytes()
    dat = open(src / "DATA.DAT", "rb")
    seen = {}
    total = 0
    for i in range(0, len(hed), 80):
        name = hed[i:i + 64].split(b"\0")[0].decode("latin1")
        offset, size = struct.unpack_from("<II", hed, i + 64)
        if not name:
            continue
        # Names can repeat; keep every copy.
        n = seen[name] = seen.get(name, 0) + 1
        out = dst / (name if n == 1 else f"{name}.dup{n}")
        out.parent.mkdir(parents=True, exist_ok=True)
        dat.seek(offset)
        out.write_bytes(dat.read(size))
        total += size
    print(f"{sum(seen.values())} files, {total / 1e6:.0f} MB -> {dst}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
