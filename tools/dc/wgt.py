"""Parse Dark Cloud skin-weight files (.wgt).

Layout, from CreateAnimeDataEX @0x00149090 and MotionProc2 @0x00148860:
  a chain of records, each = 0x20-byte header + count * 0x20-byte entries
    header (8 x u32): [0] target frame (the skinned mesh object), [1] bone frame, [2] type (200 = terminator/skip),
                      [3] unknown (kept), [4] entry count, [5] "more records follow" flag, [6][7] junk saved pointers (kept)
    entry (0x20 bytes): u32 vertex index, ... , f32 weight at +0x10 (the rest: unknown, kept verbatim)
Records are applied in order; each blends the listed vertices toward the bone's transform by its weight.
"""
import struct
from dataclasses import dataclass, field


@dataclass
class Entry:
    vertex: int
    weight: float
    raw: bytes


@dataclass
class Record:
    target: int
    bone: int
    type: int
    unk3: int
    more: int
    tail: bytes
    entries: list = field(default_factory=list)


def parse(data):
    recs, pos = [], 0
    while pos + 0x20 <= len(data):
        h = struct.unpack_from("<8I", data, pos)
        target, bone, typ, unk3, count, more = h[:6]
        rec = Record(target, bone, typ, unk3, more, data[pos + 24:pos + 32])
        pos += 0x20
        for _ in range(count):
            raw = data[pos:pos + 0x20]
            rec.entries.append(Entry(struct.unpack_from("<I", raw, 0)[0], struct.unpack_from("<f", raw, 0x10)[0], raw))
            pos += 0x20
        recs.append(rec)
        if not more:
            break
    return recs, pos


def write(recs):
    """Serialise. Field [5] is the byte distance to the next record (0x20 + 0x20 * entries), 0 on the last one."""
    out = bytearray()
    for i, r in enumerate(recs):
        last = i == len(recs) - 1
        more = 0 if last else 0x20 + 0x20 * len(r.entries)
        out += struct.pack("<6I", r.target, r.bone, r.type, r.unk3, len(r.entries), more) + r.tail
        for e in r.entries:
            out += e.raw
    return bytes(out)


def build(template, vertex_weights, min_percent=0.5):
    """New weight file with the same records (bones, order) as `template`, entries rebuilt from
    vertex_weights = {vertex: {bone_object_index: percent}}. Bones that are not in the template are ignored."""
    by_bone = {}
    for v, ws in vertex_weights.items():
        for bone, pct in ws.items():
            if pct >= min_percent:
                by_bone.setdefault(bone, []).append((v, pct))
    recs = []
    for r in template:
        entries = []
        for v, pct in sorted(by_bone.get(r.bone, [])):
            raw = bytearray(32)
            struct.pack_into("<I", raw, 0, v)
            struct.pack_into("<f", raw, 0x10, pct)
            entries.append(Entry(v, pct, bytes(raw)))
        recs.append(Record(r.target, r.bone, r.type, r.unk3, 0, r.tail, entries))
    return recs


def vertex_weights(recs):
    out = {}
    for r in recs:
        for e in r.entries:
            out.setdefault(e.vertex, {})[r.bone] = e.weight
    return out


if __name__ == "__main__":
    import sys
    from pathlib import Path
    root = Path(__file__).resolve().parent.parent.parent
    sys.path.insert(0, str(root / "scripts"))
    from pack_list import entries
    pack = (root / "data/dun/mainchara/c01d.chr").read_bytes()
    f = next(pack[o:o + s] for n, o, s in entries(pack) if n == "c01d.wgt")
    recs, used = parse(f)
    print(f"c01d.wgt: {len(f)} bytes, {len(recs)} records, parsed {used} bytes, round-trip identical: {write(recs) == f[:used]}")
    targets = sorted({r.target for r in recs})
    print("target frames:", targets)
    print("records with entries:", sum(1 for r in recs if r.entries), " total entries:", sum(len(r.entries) for r in recs))
    for r in recs[:14] + [r for r in recs if r.entries][:14]:
        ws = [e.weight for e in r.entries]
        vs = [e.vertex for e in r.entries]
        print(f"  target={r.target} bone={r.bone:2} type={r.type:3} n={len(r.entries):3}  "
              f"verts {min(vs) if vs else '-'}..{max(vs) if vs else '-'}  w {min(ws) if ws else 0:.2f}..{max(ws) if ws else 0:.2f}")
    print("trailing bytes after chain:", len(f) - used)
    e0 = next(r for r in recs if r.entries).entries[0]
    print("first entry raw:", e0.raw.hex(" "))
    print("  as floats:", [round(x, 3) for x in struct.unpack("<8f", e0.raw)])
