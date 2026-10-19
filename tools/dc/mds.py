"""Dark Cloud model container (.mds): parse and write.

MDS  = 16-byte header ('MDS\\0', version, object count, table offset 0x10) + object table (count x 0x70) + meshes back to back.
Object record (0x70): +0 index, +4 record size, +8 name[0x20], +0x28 mesh offset (from MDS start, 0 = none),
                      +0x2C parent index (-1 = root), +0x30 4x4 transform (16 floats).
Mesh ("MDT") = 0x40-byte header + blocks. Header words (u32):
   [0] 'MDT\\0'  [1] 0x40  [2] total size  [3] #positions  [4] positions offset  [5] #normals  [6] normals offset
   [7] #colours (0)  [8] colours offset (-1 = none)  [9] byte size of the group list  [10] group list offset
   [11] #uv  [12] uv offset  [13] #materials  [14] materials offset  [15] unused
 positions/normals/uv/colours are arrays of 16-byte entries (4 floats).
 Group list: 16-byte header (u32 x4, +8 = group count), then per group: i32 type (3 = triangle list, 4 = triangle strip),
   i32 tuple count, i32 material index, then count tuples of 3 i32 (position, normal, uv index) [4 if colours exist].
 Material (0x60 bytes): texture name at +0x34 (0x20 bytes, NUL padded); the rest is colour/lighting data, kept verbatim.
Found by reading CreateVUdataFromMDT @0x00135AA0, SetVuData @0x00135970, LoadMDSFile @0x001262B0 and checking against the files.
"""
import struct
from dataclasses import dataclass, field

MAT_SIZE = 0x60


@dataclass
class Group:
    type: int                 # 3 = triangle list, 4 = triangle strip
    material: int
    tuples: list              # [(pos, norm, uv)] or with a 4th colour index


@dataclass
class Mesh:
    header_extra: dict        # words kept verbatim: [1], [4], [7], [8], [15]
    positions: list           # (x, y, z, w)
    normals: list
    uvs: list
    colors: list
    groups: list
    materials: list           # raw 0x60-byte records
    pads: dict = field(default_factory=dict)   # bytes following each block (alignment/gaps), kept verbatim: pos, groups, normals, colors, uv, materials
    group_head: bytes = bytes(16)   # the group list's 16-byte header (word [2] is the group count; the rest is kept verbatim)
    stride: int = 3           # ints per vertex tuple: 3 (position, normal, uv) or 4 (+ colour)

    def texture_names(self):
        return [m[0x34:0x54].split(b"\0")[0].decode("latin1") for m in self.materials]


@dataclass
class Obj:
    raw: bytes                # the 0x70-byte record
    name: str
    parent: int
    mesh: "Mesh | None"

    @property
    def matrix(self):
        return struct.unpack_from("<16f", self.raw, 0x30)


@dataclass
class Mds:
    version: int
    objects: list
    header_words: tuple = ()


def _vec4s(buf, off, n):
    return [struct.unpack_from("<4f", buf, off + 16 * i) for i in range(n)]


def parse_mesh(m, off):
    h = struct.unpack_from("<16I", m, off)
    if h[0] != 0x54444D:
        raise ValueError(f"bad MDT magic at {off:#x}")
    mt = m[off:off + h[2]]
    npos, nnorm, ncol, nuv, nmat = h[3], h[5], h[7], h[11], h[13]
    # the game decides the tuple size from the colour block OFFSET being positive (CreateVUdataFromMDT), not from the count
    has_col = struct.unpack_from("<i", mt, 0x20)[0] > 0
    stride = 4 if has_col else 3
    pos = _vec4s(mt, h[4], npos)
    norms = _vec4s(mt, h[6], nnorm)
    colors = _vec4s(mt, h[8], ncol) if has_col else []
    uvs = _vec4s(mt, h[12], nuv)
    g0 = h[10]
    ngroups = struct.unpack_from("<I", mt, g0 + 8)[0]
    p, groups = g0 + 0x10, []
    for _ in range(ngroups):
        typ, cnt, mat = struct.unpack_from("<3i", mt, p)
        p += 12
        tuples = [struct.unpack_from(f"<{stride}i", mt, p + 4 * stride * j) for j in range(cnt)]
        p += 4 * stride * cnt
        groups.append(Group(typ, mat, tuples))
    if p - g0 != h[9]:
        raise ValueError(f"group list size mismatch: {p - g0:#x} vs {h[9]:#x}")
    mats = [mt[h[14] + MAT_SIZE * i: h[14] + MAT_SIZE * (i + 1)] for i in range(nmat)]
    extra = {1: h[1], 4: h[4], 7: h[7], 8: h[8], 15: h[15]}
    mesh = Mesh(extra, pos, norms, uvs, colors, groups, mats)
    mesh.group_head = bytes(mt[g0:g0 + 16])
    mesh.stride = stride
    # blocks must be in this order; remember the bytes after each one (padding/gaps) so unmodified meshes round-trip exactly
    layout = [("pos", h[4], 16 * npos), ("groups", g0, h[9]), ("normals", h[6], 16 * nnorm)]
    if ncol > 0:
        layout.append(("colors", h[8], 16 * ncol))
    layout += [("uv", h[12], 16 * nuv), ("materials", h[14], MAT_SIZE * nmat)]
    if h[4] != 0x40:
        raise ValueError("unexpected positions offset")
    for (name, start, size), nxt in zip(layout, [b[1] for b in layout[1:]] + [len(mt)]):
        if start + size > nxt:
            raise ValueError(f"block {name} overlaps the next one")
        mesh.pads[name] = bytes(mt[start + size:nxt])
    return mesh


def _pad(mesh, name, length):
    """Padding after a block: the original bytes if they still keep 16-byte alignment, otherwise zeros."""
    need = (-length) % 16
    old = mesh.pads.get(name, b"")
    return old if len(old) % 16 == need else bytes(need)


def dump_mesh(mesh):
    ncol = len(mesh.colors)
    stride = mesh.stride
    out = bytearray(0x40)
    pos_off = 0x40
    out += b"".join(struct.pack("<4f", *v) for v in mesh.positions)
    g_off = len(out)
    gh = list(struct.unpack("<4I", mesh.group_head))
    gh[2] = len(mesh.groups)
    out += struct.pack("<4I", *gh)
    for g in mesh.groups:
        out += struct.pack("<3i", g.type, len(g.tuples), g.material)
        for t in g.tuples:
            out += struct.pack(f"<{stride}i", *t)
    g_size = len(out) - g_off
    out += _pad(mesh, "groups", len(out))
    n_off = len(out)
    out += b"".join(struct.pack("<4f", *v) for v in mesh.normals)
    out += _pad(mesh, "normals", len(out))
    e = mesh.header_extra
    c_off = e[8]                       # when there are no colours the original value (0 or -1) is kept
    if stride == 4:
        c_off = len(out)
        out += b"".join(struct.pack("<4f", *v) for v in mesh.colors)
        out += _pad(mesh, "colors", len(out))
    uv_off = len(out)
    out += b"".join(struct.pack("<4f", *v) for v in mesh.uvs)
    out += _pad(mesh, "uv", len(out))
    m_off = len(out)
    out += b"".join(mesh.materials)
    out += _pad(mesh, "materials", len(out))
    struct.pack_into("<16I", out, 0, 0x54444D, e[1], len(out), len(mesh.positions), pos_off, len(mesh.normals), n_off,
                     ncol, c_off, g_size, g_off, len(mesh.uvs), uv_off, len(mesh.materials), m_off, e[15])
    return bytes(out)


def parse(data):
    magic, ver, nobj, tab = struct.unpack_from("<4I", data, 0)
    if magic != 0x534D44 and data[:4] != b"MDS\0":
        raise ValueError("not an MDS file")
    objs = []
    for i in range(nobj):
        r = tab + i * 0x70
        raw = data[r:r + 0x70]
        name = raw[8:0x28].split(b"\0")[0].decode("latin1")
        moff, parent = struct.unpack_from("<Ii", raw, 0x28)
        objs.append(Obj(raw, name, parent, parse_mesh(data, moff) if moff else None))
    return Mds(ver, objs, struct.unpack_from("<4I", data, 0))


def dump(mds):
    n = len(mds.objects)
    head = bytearray(struct.pack("<4I", *mds.header_words))
    table = bytearray()
    body = bytearray()
    base = 0x10 + 0x70 * n
    for o in mds.objects:
        raw = bytearray(o.raw)
        if o.mesh is None:
            struct.pack_into("<I", raw, 0x28, 0)
        else:
            struct.pack_into("<I", raw, 0x28, base + len(body))
            body += dump_mesh(o.mesh)
        table += raw
    return bytes(head + table + body)

