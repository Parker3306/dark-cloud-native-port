"""List the entries of a Dark Cloud pack (.chr/.pak/.pac/.mpd style).

Each entry: name[64], u32 data offset (from entry start), u32 size, u32 stride to the next entry (see
GetPackFile @0x0013F720); the list ends at an entry with an empty name.

Usage: python -I scripts/pack_list.py data/dun/mainchara/c01d.chr
"""
import struct
import sys


def entries(data):
    pos = 0
    while pos + 0x4C <= len(data) and data[pos] != 0:
        name = data[pos:pos + 0x40].split(b"\0")[0].decode("latin1")
        off, size, stride = struct.unpack_from("<III", data, pos + 0x40)
        yield name, pos + off, size
        if stride == 0:
            break
        pos += stride


if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    print(f"{len(d)} bytes")
    for name, off, size in entries(d):
        print(f"  {name:32} data@0x{off:07X}  {size:9} bytes")
