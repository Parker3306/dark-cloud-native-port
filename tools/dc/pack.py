"""Dark Cloud pack files (.chr/.pak/.pac/.mpd): a list of named files.

Entry = 0x50-byte header + data. Header: name[0x40], u32 data offset (from the entry start, normally 0x50), u32 data size,
u32 stride (bytes to the next entry, header + padded data), u32 extra (kept). The list ends at an entry whose name is empty.
(GetPackFile @0x0013F720 walks it exactly like this.)  Data is padded with zeros to the stride.
"""
import struct
from dataclasses import dataclass, field


@dataclass
class Entry:
    name: str
    data: bytes
    extra: int = 0
    raw_name: bytes = b""      # the 0x40 name field verbatim (may hold junk after the NUL)
    pad: bytes = b""           # padding after the data, verbatim


@dataclass
class Pack:
    entries: list = field(default_factory=list)
    tail: bytes = b""          # whatever follows the last entry (terminator + padding)

    def get(self, name):
        return next((e for e in self.entries if e.name.lower() == name.lower()), None)

    def replace(self, name, data):
        e = self.get(name)
        if e is None:
            raise KeyError(name)
        e.data = data


def parse(buf):
    p, entries = 0, []
    while p + 0x50 <= len(buf) and buf[p] != 0:
        raw_name = bytes(buf[p:p + 0x40])
        off, size, stride, extra = struct.unpack_from("<4I", buf, p + 0x40)
        if off < 0x50 or stride < off + size or p + off + size > len(buf):
            raise ValueError(f"bad pack entry at {p:#x}")
        e = Entry(raw_name.split(b"\0")[0].decode("latin1"), bytes(buf[p + off:p + off + size]), extra, raw_name,
                  bytes(buf[p + off + size:p + stride]))
        entries.append(e)
        if stride == 0:
            break
        p += stride
    return Pack(entries, bytes(buf[p:]))


def dump(pack):
    out = bytearray()
    for e in pack.entries:
        name = e.raw_name if e.raw_name and e.raw_name.split(b"\0")[0].decode("latin1") == e.name else \
            e.name.encode("latin1").ljust(0x40, b"\0")
        # keep the original padding when the size is unchanged, otherwise pad to 16 bytes
        pad = e.pad if len(e.pad) % 16 == (-len(e.data)) % 16 else bytes((-len(e.data)) % 16)
        stride = 0x50 + len(e.data) + len(pad)
        out += name + struct.pack("<4I", 0x50, len(e.data), stride, e.extra) + e.data + pad
    return bytes(out) + pack.tail
