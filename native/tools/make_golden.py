"""Reference digests of everything dccore reads, computed with the existing Python tools (tools/dc, scripts/tim2.py).

The C++ program `dcgolden` prints the same lines from the same ISO; native/tools/compare_golden.py diffs the two. Digests are CRC32s of
canonical byte strings, so the golden file contains no game data and is safe to keep in the repository.

Usage: python -I native/tools/make_golden.py <iso> <out.txt> [--limit-files N]
Line formats (all fields space separated, no spaces inside names because names are written with '|' separators):
  F <index> <size> <crc>                          every DATA.DAT entry, read by index
  P <file> <n_entries>|ERR                        pack files, then  E <file>|<entry> <size> <crc>
  M <file>|<entry> <n_objects> <crc>|ERR          models, then      m <file>|<entry>|<object index> npos nnorm nuv ncol ngroups stride crcPos crcNrm crcUv crcCol crcGroups crcMat
  I <file>|<entry> <n_textures>|ERR               image containers, then  T <file>|<entry>|<texture> <w> <h> <bpp> <crc of RGBA>|ERR
  W <file>|<entry> <n_records> <n_entries> <crc>|ERR   skin weights
"""
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "dc"))
sys.path.insert(0, str(ROOT / "scripts"))
import disc as D  # noqa: E402
import img as I  # noqa: E402
import mds as M  # noqa: E402
import pack as P  # noqa: E402
import wgt as W  # noqa: E402
from tim2 import Tim2  # noqa: E402

PACK_EXT = (".chr", ".pak", ".pac", ".mpd")


def crc(b, prev=0):
    return zlib.crc32(b, prev) & 0xFFFFFFFF


def f32(rows):
    return np.asarray(rows, dtype=np.float32).tobytes() if len(rows) else b""


def mds_lines(label, data):
    try:
        m = M.parse(data)
    except Exception:  # noqa: BLE001
        return [f"M {label} ERR"]
    c = 0
    for o in m.objects:
        c = crc(o.name.encode("latin1") + b"\0" + struct.pack("<i", o.parent) + np.asarray(o.matrix, dtype=np.float32).tobytes(), c)
    out = [f"M {label} {len(m.objects)} {c:08x}"]
    for i, o in enumerate(m.objects):
        if not o.mesh:
            continue
        mesh = o.mesh
        cg = 0
        for g in mesh.groups:
            cg = crc(struct.pack("<3i", g.type, len(g.tuples), g.material), cg)
            cg = crc(np.asarray(g.tuples, dtype=np.int32).tobytes() if g.tuples else b"", cg)
        out.append(f"m {label}|{i} {len(mesh.positions)} {len(mesh.normals)} {len(mesh.uvs)} {len(mesh.colors)} {len(mesh.groups)} {mesh.stride} "
                   f"{crc(f32(mesh.positions)):08x} {crc(f32(mesh.normals)):08x} {crc(f32(mesh.uvs)):08x} {crc(f32(mesh.colors)):08x} "
                   f"{cg:08x} {crc(b''.join(mesh.materials)):08x}")
    return out


def img_lines(label, data):
    try:
        tex = I.textures(data)
    except Exception:  # noqa: BLE001
        return [f"I {label} ERR"]
    out = [f"I {label} {len(tex)}"]
    for name, t in tex:
        try:
            im = t.decode(raw_alpha=False)
            arr = np.asarray(im, dtype=np.uint8)
            out.append(f"T {label}|{name} {t.width} {t.height} {t.bpp} {crc(arr.tobytes()):08x}")
        except Exception:  # noqa: BLE001
            out.append(f"T {label}|{name} {t.width} {t.height} {t.bpp} ERR")
    return out


def wgt_lines(label, data):
    try:
        recs, _ = W.parse(data)
    except Exception:  # noqa: BLE001
        return [f"W {label} ERR"]
    c = 0
    for r in recs:
        c = crc(struct.pack("<5I", r.target, r.bone, r.type, r.unk3, r.more), c)
        for e in r.entries:
            c = crc(struct.pack("<If", e.vertex, e.weight), c)
    return [f"W {label} {len(recs)} {sum(len(r.entries) for r in recs)} {c:08x}"]


def tm2_lines(label, data):
    out = []
    if data[:4] == b"TIM2":
        try:
            t = Tim2(data, 0)
            if t.valid():
                arr = np.asarray(t.decode(raw_alpha=False), dtype=np.uint8)
                out = [f"T {label}|0 {t.width} {t.height} {t.bpp} {crc(arr.tobytes()):08x}"]
        except Exception:  # noqa: BLE001
            out = [f"T {label}|0 ERR"]
    return out


def classify(label, name, data):
    n = name.lower()
    if n.endswith(".mds"):
        return mds_lines(label, data)
    if n.endswith(".img"):
        return img_lines(label, data)
    if n.endswith(".wgt"):
        return wgt_lines(label, data)
    if n.endswith(".tm2"):
        return tm2_lines(label, data)
    return []


def main(iso, out_path, limit=0):
    d = D.Disc(iso, readonly=True)
    lines = []
    for i, name in enumerate(d.names):
        if limit and i >= limit:
            break
        e = d.hed[i * 80 + 64:i * 80 + 72]
        off, size = struct.unpack("<II", e)
        d.f.seek(d.files["DATA.DAT;1"]["lba"] * 2048 + off)
        data = d.f.read(size)
        lines.append(f"F {i} {size} {crc(data):08x}")
        lower = name.lower()
        if lower.endswith(PACK_EXT):
            try:
                pk = P.parse(data)
            except Exception:  # noqa: BLE001
                lines.append(f"P {name} ERR")
                continue
            lines.append(f"P {name} {len(pk.entries)}")
            for en in pk.entries:
                label = f"{name}|{en.name}"
                lines.append(f"E {label} {len(en.data)} {crc(en.data):08x}")
                lines += classify(label, en.name, en.data)
        else:
            lines += classify(name, name, data)
        if i % 500 == 0:
            print(i, "/", len(d.names), flush=True)
    Path(out_path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("wrote", out_path, len(lines), "lines")


if __name__ == "__main__":
    lim = int(sys.argv[sys.argv.index("--limit-files") + 1]) if "--limit-files" in sys.argv else 0
    main(sys.argv[1], sys.argv[2], lim)
