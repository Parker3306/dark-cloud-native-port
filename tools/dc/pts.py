"""Georama / story-scene map parts: `scene.scn` (container 'SCN\\0') and the 'PTS\\0' blocks inside it.

See notes/georama.md for the full description.  Short version:

SCN  = 0x10-byte header {'SCN\\0', u32 count, 8 bytes zero} + a table of 64 slots of 0x30 bytes
       {name[16] (NUL terminated, junk after the NUL), u32 offset, u32 size, 0x18 bytes of junk} + the blocks.
       Blocks start at 0xC10, are back to back (no padding) and the last one ends at the end of the file.
       The game looks a block up by name (SearchPTS 0x13e4e0: strcmp over the names, offset relative to the file start).
PTS  = header + info block + up to 9 embedded MDS models:
       +0x00 'PTS\\0'   +0x04 u32 info offset (always 0xE0)
       +0x08 4 x name[16]      names of model slots 0..3 (LOD0, LOD1, LOD2, LOD3 = miniature `_m`)
       +0x48 4 x u32 offset    +0x58 4 x u32 size         offset/size of slots 0..3 (offsets relative to the PTS start)
       +0x68 name[16] u32 off u32 size   slot 5 (`_a`, movement collision polygons)
       +0x80 ...                         slot 4 (`_s`, projected shadow model)
       +0x98 ...                         slot 6 (`_k`, shade / dark overlay model)
       +0xB0 ...                         slot 8 (`_c` or `_v`, camera collision polygons)
       +0xC8 ...                         slot 7 (`_r`/`_w`/`_0`, water ripple / river surface model)
       +0xE0 info block (see PtsInfo) which also carries the array of 0xC0 byte function records,
       then the models back to back, in the order slot 0,1,2,3,5,4,6,8,7 (empty slots have size 0).
"""
import math
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
GEDIT = ROOT / "data" / "gedit"

SCN_HEADER = 0x10
SCN_SLOT = 0x30
SCN_SLOTS = 64
FUNC_SIZE = 0xC0

# model slot index -> (role, usual suffix)
SLOT_ROLES = {
    0: ("lod0", "_0"), 1: ("lod1", "_1"), 2: ("lod2", "_2"), 3: ("lod3_mini", "_m"), 4: ("shadow", "_s"),
    5: ("collision", "_a"), 6: ("shade", "_k"), 7: ("water", "_r/_w/_0"), 8: ("camera", "_c/_v"),
}
# header positions of the model entries: slot -> (kind, position)
#   kind 'tab': name at 0x08+16*i, offset at 0x48+4*i, size at 0x58+4*i;  kind 'ent': {name16, off, size} at pos
SLOT_POS = {0: ("tab", 0), 1: ("tab", 1), 2: ("tab", 2), 3: ("tab", 3),
            5: ("ent", 0x68), 4: ("ent", 0x80), 6: ("ent", 0x98), 8: ("ent", 0xB0), 7: ("ent", 0xC8)}

# function record types (EPARTS_FUNC_DATA +0x10): name found in the records, what the runtime does with it
FUNC_TYPES = {
    0: ("empty", "unused record (3 interior files)"),
    1: ("person", "villager standing point: +0x20 = villager number (PEOPLE/PEOPLE2 order), pos/rot relative to the part"),
    2: ("mapjump", "door trigger -> ED_EVENT_POINT kind 1 (enter a house); +0x20 = MAPJUMP id of mapinfo.cfg"),
    3: ("fire", "fire/lamp flame effect (EDIT_EFFECT kind 1), attached to frame +0x30"),
    4: ("smoke", "chimney smoke effect (EDIT_EFFECT kind 4)"),
    5: ("light2", "glow light, same effect kind as 13 (EDIT_EFFECT kind 3); not used on the disc"),
    6: ("obj_anime", "object animation (OBJ_ANIME_SEQ), see InitObjAnime(EPARTS_FUNC_DATA)"),
    9: ("timer", "object timer: frame +0x30 is shown only between hours +0x18..+0x1c (EDIT_OBJECT_TIMER)"),
    7: ("camera_zone", "interior only: fixed-camera zone; box min = +0x60, max = +0x70 in the local space of frame +0x30 "
                       "(inverse of its matrix * +0x80); the frame is the camera (SetCameraPos 0x19e520)"),
    10: ("door_r", "right door leaf of a mapjump (sets event dir +1)"),
    11: ("door_l", "left door leaf of a mapjump (sets event dir -1)"),
    12: ("flame", "flame effect (EDIT_EFFECT kind 2)"),
    13: ("light", "glow light (EDIT_EFFECT kind 3)"),
    14: ("point_light", "point light attached to a frame (DrawParts collects up to 4: colour +0x60, 'plight')"),
    15: ("candle", "candle flame (EDIT_EFFECT kind 6, CCandleEffect; scale +0x50/+0x54); interior only"),
    16: ("mapjcamp", "'camp' exit position of a mapjump (copied to the event's out position)"),
    17: ("item", "pick-up point -> ED_EVENT_POINT kind 2; params +0x70..+0x7c = item id, count, ?, ?"),
    18: ("event", "talk/inspect event point -> ED_EVENT_POINT kind 3"),
    19: ("ladder_bottom", "ladder bottom -> ED_EVENT_POINT kind 4 (paired with 20 of the same +0x20)"),
    20: ("ladder_top", "ladder top -> ED_EVENT_POINT kind 5"),
    21: ("sound_src", "positional ambient sound source (EdInitSoundSrc)"),
}


class PtsError(Exception):
    pass


def _cstr(b):
    return b.split(b"\0")[0].decode("cp932", errors="replace")


@dataclass
class ScnEntry:
    index: int
    name: str
    offset: int
    size: int
    raw_name: bytes
    junk: bytes


@dataclass
class ModelSlot:
    slot: int
    role: str
    name: str
    offset: int          # relative to the PTS block
    size: int
    magic: bytes = b""
    header_pos: int = 0  # where the entry lives inside the PTS header

    @property
    def present(self):
        return self.size > 0


@dataclass
class FuncRecord:
    """EPARTS_FUNC_DATA, 0xC0 bytes. Offsets are relative to the record."""
    index: int
    raw: bytes
    label: str           # +0x00 name[16] (editor label, e.g. 'mapj00', 'light1')
    type: int            # +0x10
    f14: int             # +0x14 editor-side index (unused by the town code)
    t_start: float       # +0x18 hours (clock hours 0..24; the game converts with ConvertTime)
    t_end: float         # +0x1c
    id: int              # +0x20 (mapjump id / villager number / ladder pair id / ...)
    flag: int            # +0x24 (map flag id: record is hidden once the flag is set; also event parameter)
    enabled: int         # +0x28 (1 in all but 5 records)
    target: str          # +0x30 name[16]: frame (object) of the part's models the record is bound to
    pos: tuple           # +0x40 vec4 xyz (+ w = 1)
    rot: tuple           # +0x50 vec4 euler angles in radians (x, y, z)
    scale: tuple         # +0x60 vec4
    params: tuple        # +0x70 vec4 (type specific, floats; ints are stored as floats and converted with fptosi)
    extra: tuple         # +0x80..+0xBF: editor scratch (matrix and position at tool time, or stale memory)

    @property
    def type_name(self):
        return FUNC_TYPES.get(self.type, ("type%d" % self.type, ""))[0]


@dataclass
class PtsInfo:
    header_size: int       # +0x00 always 0x78
    size_field: int        # +0x04 (NOT the real size: = end of func records minus 0..13 bytes)
    width: int             # +0x08 footprint width in edit-grid cells (before rotation)
    height: int            # +0x0C footprint height
    max_count: int         # +0x10 max number of copies the player may place (PARTS_INFO 4th argument)
    f14: int               # +0x14 0/1 (unknown)
    f18: int               # +0x18 always 0
    f1c: int               # +0x1C 0/1/2 (unknown)
    f20: int               # +0x20 always 0
    offset: tuple          # +0x24 vec3 local position of the models inside the part
    rotation: tuple        # +0x30 vec3 local rotation
    grid_off: int          # +0x3C
    element_ids: list      # +0x40..+0x54 six ints (additional item/element ids, -1 = none)
    element_str_off: list  # +0x58..+0x6C six offsets
    func_off: int          # +0x70
    func_count: int        # +0x74
    grid: bytes            # width*height bytes: 0 = free, 1..9 = raises the ground by n alt units, 0x80 'x' = occupied, 0x81 'd' = water
    elements: list         # six strings: space separated frame names switched on when the element is installed
    funcs: list            # FuncRecord


@dataclass
class Pts:
    name: str
    block: bytes
    info_offset: int
    models: list
    info: PtsInfo

    def model(self, slot):
        return self.models[slot]

    def model_bytes(self, slot):
        m = self.models[slot]
        return self.block[m.offset:m.offset + m.size]

    @property
    def funcs(self):
        return self.info.funcs


@dataclass
class Scn:
    path: str
    data: bytes
    count: int
    entries: list
    parts: dict = field(default_factory=dict)   # name -> Pts (first entry of that name, like SearchPTS)
    pts: list = field(default_factory=list)     # every entry, in table order


def parse_scn_table(data):
    if data[:4] != b"SCN\0":
        raise PtsError("not an SCN container")
    count, = struct.unpack_from("<I", data, 4)
    if count > SCN_SLOTS:
        raise PtsError("count %d > %d slots" % (count, SCN_SLOTS))
    ents = []
    for i in range(count):
        p = SCN_HEADER + i * SCN_SLOT
        raw = data[p:p + 16]
        off, size = struct.unpack_from("<II", data, p + 16)
        ents.append(ScnEntry(i, _cstr(raw), off, size, raw, data[p + 24:p + 48]))
    return count, ents


def parse_pts(block, name=""):
    if block[:4] != b"PTS\0":
        raise PtsError("%s: not a PTS block" % name)
    io, = struct.unpack_from("<I", block, 4)
    models = []
    toffs = struct.unpack_from("<4I", block, 0x48)
    tsize = struct.unpack_from("<4I", block, 0x58)
    for slot in range(9):
        kind, pos = SLOT_POS[slot]
        if kind == "tab":
            nm = _cstr(block[8 + 16 * pos:8 + 16 * pos + 16])
            off, size, hp = toffs[pos], tsize[pos], 8 + 16 * pos
        else:
            nm = _cstr(block[pos:pos + 16])
            off, size = struct.unpack_from("<II", block, pos + 16)
            hp = pos
        if size and (off + size > len(block) or off < io):
            raise PtsError("%s: slot %d out of range" % (name, slot))
        models.append(ModelSlot(slot, SLOT_ROLES[slot][0], nm if size else "", off, size,
                                block[off:off + 4] if size else b"", hp))
    info = parse_info(block, io, name)
    return Pts(name, block, io, models, info)


def _vec3(b, off):
    return struct.unpack_from("<3f", b, off)


def parse_info(block, io, name=""):
    b = block[io:]
    (hsz, size_field, w, h, maxc, f14, f18, f1c, f20) = struct.unpack_from("<9I", b, 0)
    offset = _vec3(b, 0x24)
    rotation = _vec3(b, 0x30)
    grid_off, = struct.unpack_from("<I", b, 0x3C)
    ids = list(struct.unpack_from("<6i", b, 0x40))
    soffs = list(struct.unpack_from("<6I", b, 0x58))
    foff, fcount = struct.unpack_from("<II", b, 0x70)
    if hsz != 0x78 or grid_off != 0x78:
        raise PtsError("%s: unexpected info header %x/%x" % (name, hsz, grid_off))
    grid = bytes(b[grid_off:grid_off + w * h])
    if len(grid) != w * h:
        raise PtsError("%s: grid truncated" % name)
    elements = []
    for k, o in enumerate(soffs):
        if o == 0:
            elements.append("")
            continue
        if o < grid_off + w * h or o >= foff:
            raise PtsError("%s: element string %d out of range" % (name, k))
        elements.append(_cstr(b[o:o + 64]))
    if foff < grid_off + w * h:
        raise PtsError("%s: func array inside the grid" % name)
    if io + foff + fcount * FUNC_SIZE > len(block):
        raise PtsError("%s: func array out of range" % name)
    funcs = []
    for i in range(fcount):
        r = b[foff + i * FUNC_SIZE:foff + (i + 1) * FUNC_SIZE]
        t, f14r = struct.unpack_from("<iI", r, 0x10)
        ts, te = struct.unpack_from("<2f", r, 0x18)
        fid, fflag, fen = struct.unpack_from("<3i", r, 0x20)
        funcs.append(FuncRecord(
            i, bytes(r), _cstr(r[0:16]), t, f14r, ts, te, fid, fflag, fen, _cstr(r[0x30:0x40]),
            struct.unpack_from("<3f", r, 0x40), struct.unpack_from("<3f", r, 0x50),
            struct.unpack_from("<3f", r, 0x60), struct.unpack_from("<4f", r, 0x70), tuple(r[0x80:0xC0])))
    return PtsInfo(hsz, size_field, w, h, maxc, f14, f18, f1c, f20, offset, rotation, grid_off, ids, soffs, foff,
                   fcount, grid, elements, funcs)


def open_scn(src):
    """src: path (str/Path) or bytes. Returns Scn with .entries (table) and .parts (name -> Pts)."""
    if isinstance(src, (bytes, bytearray)):
        data, path = bytes(src), "<bytes>"
    else:
        path = str(src)
        data = Path(src).read_bytes()
    count, ents = parse_scn_table(data)
    scn = Scn(path, data, count, ents)
    pos = SCN_HEADER + SCN_SLOTS * SCN_SLOT
    for e in ents:
        if e.offset != pos:
            raise PtsError("%s: entry %s starts at %#x, expected %#x" % (path, e.name, e.offset, pos))
        if e.offset + e.size > len(data):
            raise PtsError("%s: entry %s runs past the end" % (path, e.name))
        pos = e.offset + e.size
        pt = parse_pts(data[e.offset:e.offset + e.size], e.name)
        scn.pts.append(pt)
        scn.parts.setdefault(e.name, pt)
    if pos != len(data):
        raise PtsError("%s: %d bytes after the last entry" % (path, len(data) - pos))
    return scn


def check_pts(p):
    """Structural invariants of one PTS. Returns a list of problems (empty = ok)."""
    bad = []
    blk = p.block
    present = sorted((m.offset, m.size, m.slot) for m in p.models if m.size)
    info_end = p.info_offset + p.info.func_off + p.info.func_count * FUNC_SIZE
    pos = info_end
    for off, size, slot in present:
        if off != pos:
            bad.append("slot %d starts at %#x, expected %#x" % (slot, off, pos))
        pos = off + size
    if pos != len(blk):
        bad.append("models end at %#x, block is %#x" % (pos, len(blk)))
    if p.models[0].size == 0:
        bad.append("no LOD0 model")
    for m in p.models:
        if m.size:
            if m.magic != b"MDS\0":
                bad.append("slot %d magic %r" % (m.slot, m.magic))
            else:
                nobj, tab = struct.unpack_from("<2I", blk, m.offset + 8)
                if tab != 0x10 or 0x10 + nobj * 0x70 > m.size:
                    bad.append("slot %d MDS table" % m.slot)
    f = p.info
    if not (0 <= info_end - p.info_offset - f.size_field <= 32):
        bad.append("size field %d vs func end %d" % (f.size_field, info_end - p.info_offset))
    for fr in f.funcs:
        if fr.type not in FUNC_TYPES:
            bad.append("func %d unknown type %d" % (fr.index, fr.type))
    return bad


def frame_key(name):
    """StrCmp__FPcPc (0x1285f0): frame names are compared only up to the first `__` (case sensitive), so the
    record target `h01__N` matches a model object called `h01` or `h01__S` ..."""
    i = name.find("__")
    return name if i < 0 else name[:i]


def mds_objects(data):
    """Object names of an embedded MDS (container level only)."""
    nobj, tab = struct.unpack_from("<2I", data, 8)
    out = []
    for i in range(nobj):
        r = data[tab + i * 0x70: tab + (i + 1) * 0x70]
        mo, parent = struct.unpack_from("<Ii", r, 0x28)
        out.append((_cstr(r[8:0x28]), parent, mo))
    return out


def _main(argv):
    import collections
    if argv and argv[0] == "--all":
        paths = sorted(GEDIT.rglob("*.scn"))
        argv = []
    else:
        paths = [Path(a) for a in argv] if argv else sorted(GEDIT.glob("*/scene.scn"))
    nparts = nbad = 0
    types = collections.Counter()
    for pth in paths:
        try:
            scn = open_scn(pth)
        except Exception as e:                                  # noqa
            print("FAIL", pth, e)
            nbad += 1
            continue
        for p in scn.pts:
            nm = p.name
            nparts += 1
            for pr in check_pts(p):
                print("PROBLEM", pth.parent.name, nm, pr)
                nbad += 1
            for fr in p.funcs:
                types[fr.type_name] += 1
    print("%d scn files, %d parts, %d problems" % (len(paths), nparts, nbad))
    print(dict(types))
    return nbad


def dump(path, only=None, out=sys.stdout):
    scn = open_scn(path)
    print("%s: %d entries, %d bytes" % (path, scn.count, len(scn.data)), file=out)
    for e in scn.entries:
        if only and e.name != only:
            continue
        p = scn.pts[e.index]
        i = p.info
        print("[%d] %s  offset %#x size %#x" % (e.index, e.name, e.offset, e.size), file=out)
        print("    footprint %dx%d max %d grid %s elements %s%s" % (
            i.width, i.height, i.max_count, i.grid.hex(), i.element_ids,
            [x for x in i.elements if x]), file=out)
        for m in p.models:
            if m.size:
                objs = [n for n, _, _ in mds_objects(p.model_bytes(m.slot))]
                print("    slot %d %-9s %-18s +%#07x %#7x  objects %s" % (
                    m.slot, m.role, m.name, m.offset, m.size, objs[:6] + (["..."] if len(objs) > 6 else [])), file=out)
        for fr in i.funcs:
            print("    func %2d %-12s %-10r target %-12r id %d flag %d t %g-%g pos (%.1f %.1f %.1f) params %s" % (
                fr.index, fr.type_name, fr.label, fr.target, fr.id, fr.flag, fr.t_start, fr.t_end, *fr.pos,
                tuple(round(x, 2) for x in fr.params)), file=out)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--dump":
        sys.stdout.reconfigure(encoding="utf-8")
        dump(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    else:
        raise SystemExit(1 if _main(sys.argv[1:]) else 0)
