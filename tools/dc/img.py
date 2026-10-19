"""Dark Cloud .img texture containers: 'IMG\\0', u32 count, 8 bytes zero, then count entries of 0x30 bytes:
name[0x20] (at 0x10 + 0x30*i) followed by a u32 offset (from the file start) of a TIM2 texture and padding.
(Layout checked on c01d01_dun.img; the TIM2 data itself is decoded by scripts/tim2.py.)
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "scripts"))
from tim2 import Tim2  # noqa: E402


def parse(data):
    if data[:4] not in (b"IMG\0", b"IM2\0"):      # IM2 = same layout; about two thirds of the game's containers use it
        raise ValueError("not an IMG container")
    count, = struct.unpack_from("<I", data, 4)
    out = []
    for i in range(count):
        base = 0x10 + 0x30 * i
        name = data[base:base + 0x20].split(b"\0")[0].decode("latin1")
        off, = struct.unpack_from("<I", data, base + 0x20)
        out.append((name, off))
    return out


def textures(data):
    """[(name, Tim2)] for every texture in the container."""
    res = []
    for name, off in parse(data):
        if data[off:off + 4] != b"TIM2":
            continue
        t = Tim2(data, off)
        if t.valid():
            t.swizzled = data[:4] == b"IM2\0"      # IM2 containers keep their 8-bit textures in GS page order
            res.append((name, t))
    return res
