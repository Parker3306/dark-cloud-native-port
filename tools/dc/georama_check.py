"""Cross-checks of the Georama data formats (see notes/georama.md):

  * mapinfo.cfg of every map parses (tools/dc/mapinfo.py) and every file name it mentions can be resolved
    (scene.scn entries, mds.pak, img.pak, loose mds/ files, chara/*.chr),
  * every scene.scn / PTS block is structurally valid and every frame / texture a PTS refers to exists (tools/dc/pts.py),
  * gdata0.edt of e01..e05 is a legal Georama layout (all parts inside the edit areas, no overlaps, counts <= max),
  * fconv.bin is a message bank with the same ids as `<map>talk_1.mes`.

usage: georama_check.py            run everything and print a summary
"""
import collections
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import img as dcimg      # noqa: E402
import mapinfo           # noqa: E402
import pack as dcpack    # noqa: E402
import pts as dcpts      # noqa: E402

GEDIT = dcpts.GEDIT


def pack_names(path):
    if not path.exists():
        return set()
    return {e.name for e in dcpack.parse(path.read_bytes()).entries}


def texture_names(path):
    res = set()
    if path.exists():
        for e in dcpack.parse(path.read_bytes()).entries:
            if e.data[:4] in (b"IMG\0", b"IM2\0"):
                res.update(n for n, _ in dcimg.parse(e.data))
    return res


def base(n):
    return n.replace("\\", "/").split("/")[-1]


def check_mapinfo_refs(d):
    """Return (total references, list of unresolved)."""
    m = mapinfo.load(d)
    scn = dcpts.open_scn(d / "scene.scn") if (d / "scene.scn").exists() else None
    mds_pak = pack_names(d / "mds.pak")
    img_pak = pack_names(d / "img.pak")
    loose_mds = {p.name for p in (d / "mds").glob("*")} if (d / "mds").exists() else set()
    chara = {p.name for p in (d / "chara").glob("*")} if (d / "chara").exists() else set()
    pts_names = {p.name for p in scn.pts} if scn else set()
    pts_models = set()
    if scn:
        for p in scn.pts:
            for mm in p.models:
                if mm.size:
                    pts_models.add(mm.name)
    refs, bad = 0, []

    def need(kind, name, pool):
        nonlocal refs
        if not name:
            return
        refs += 1
        if base(name) not in pool and base(name).lower() not in {x.lower() for x in pool}:
            bad.append((kind, name))

    for cd, f, kind, flag in m.textures:
        need("img", f, img_pak)
    for k, v in m.skies.items():
        need("sky", v[0], mds_pak | loose_mds | pts_models)
    for k, v in m.suns.items():
        need("sun", v[0], mds_pak | loose_mds | pts_models)
    for o in m.objects:
        need(o.kind, o.names[0], pts_names | mds_pak | loose_mds | pts_models)
        for n in o.names[1:8]:
            need(o.kind + "-slot", n, pts_models | mds_pak | loose_mds)
    for k, a in m.editareas.items():
        need("editarea grid", a["grid"], mds_pak | loose_mds)
    for k, p in m.parts.items():
        if p.file:
            need(p.directive, base(p.file)[:-4] if p.file.endswith(".pts") else p.file, pts_names | mds_pak | loose_mds)
        if p.extra:
            need(p.directive + " water", p.extra, pts_models | mds_pak | loose_mds)
    for k, (f, fl, sc) in m.roads.items():
        need("road", base(f)[:-4] if f.endswith(".pts") else f, pts_names)
    for k, (f, fl, sc, wm) in m.rivers.items():
        need("river", base(f)[:-4] if f.endswith(".pts") else f, pts_names)
        need("river water", wm, pts_models | mds_pak | loose_mds)
    for p in m.people:
        need("people", p["name"] + ".chr", chara)
    for mp in m.motion_parts:
        need("motion_parts", mp["file"], chara | mds_pak)
    return refs, bad, m


def rot_footprint(grid, w0, h0, rot):
    """Footprint of a part after `rot` (CMapParts rotation index 0,1,2,-1): list of (i, j, code); mirrors
    CMapParts::GetWidth/GetHeight/GetInfoData (0x19aa20..0x19aae0)."""
    odd = rot & 1
    w, h = (h0, w0) if odd else (w0, h0)
    out = []
    for i in range(w):
        for j in range(h):
            if rot == 2:
                c = grid[w * ((h - j) - 1) + (w - i) - 1]
            elif rot == 1:
                c = grid[i * h + (h - j) - 1]
            elif rot == 0:
                c = grid[i + j * w]
            else:
                c = grid[j + h * ((w - i) - 1)]
            out.append((i, j, c))
    return w, h, out


def check_gdata(d):
    m = mapinfo.load(d)
    scn = dcpts.open_scn(d / "scene.scn")
    g = (d / "gdata0.edt").read_bytes()
    cnt, size, off = struct.unpack_from("<3h", g, 0)
    problems = []
    if size != len(g):
        problems.append("size field %d != file size %d" % (size, len(g)))
    if off != 8 or 8 + 16 * cnt + 16 != len(g):
        problems.append("layout")
    term = struct.unpack_from("<hh3f", g, off + 16 * cnt)
    if term[:2] != (-1, -1):
        problems.append("no terminator")
    recs = [struct.unpack_from("<hh3f", g, off + 16 * i) for i in range(cnt)]
    cells = {a: {} for a in m.editareas}
    used = collections.Counter()
    kinds = {"BLD_PARTS": 0, "GRD_PARTS": 0, "ROAD_PARTS": 1, "RIVER_PARTS": 2, "BRIDGE_PARTS": 3, "LAKE_PARTS": 4,
             "ON_RIVER_PARTS": 5}
    for n, (pn, rot, x, y, z) in enumerate(recs):
        p = m.parts.get(pn)
        if p is None:
            problems.append("record %d: part %d not defined" % (n, pn))
            continue
        # footprint: PARTS_INFO of the mapinfo (roads, rivers) or the PTS info (buildings)
        if p.info:
            w0, h0, gr, mx = p.info["width"], p.info["height"], p.info["grid"].replace("|", ""), p.info["max_count"]
            grid = []
            for ch in gr:
                grid.append(0x80 if ch == "x" else 0x81 if ch == "d" else int(ch))
            grid = bytes(grid)
        else:
            nm = base(p.file)[:-4] if p.file.endswith(".pts") else base(p.file)
            pt = scn.parts.get(nm)
            if pt is None:
                problems.append("record %d: no PTS for part %d (%s)" % (n, pn, p.file))
                continue
            w0, h0, grid, mx = pt.info.width, pt.info.height, pt.info.grid, pt.info.max_count
        used[pn] += 1
        if used[pn] > mx:
            problems.append("part %d used %d times, max %d" % (pn, used[pn], mx))
        # find the area (GetAreaCode: first area whose rectangle contains the point; the loader adds +1.0 to x and z)
        px, pz = x + 1.0, z + 1.0
        area = None
        for k in sorted(m.editareas):
            a = m.editareas[k]
            ox, oy, oz = a["origin"]
            ox -= 0.01 * a["width"]
            oz -= 0.01 * a["height"]
            if ox <= px <= ox + a["unit"] * a["width"] and oz <= pz <= oz + a["unit"] * a["height"]:
                area = (k, a, ox, oz)
                break
        if area is None:
            problems.append("record %d: part %d at (%g,%g) outside every edit area" % (n, pn, x, z))
            continue
        k, a, ox, oz = area
        cx = int((px - ox + 0.1) / a["unit"])
        cz = int((pz - oz + 0.1) / a["unit"])
        w, h, fp = rot_footprint(grid, w0, h0, rot)
        # position check: odd extents are centred on a cell, even ones on a grid line
        exp_x = ox + cx * a["unit"] + (a["unit"] / 2 if w & 1 else 0)
        exp_z = oz + cz * a["unit"] + (a["unit"] / 2 if h & 1 else 0)
        if abs(exp_x - x) > 0.2 or abs(exp_z - z) > 0.2:
            problems.append("record %d: part %d position (%g,%g) not on the grid (expected %g,%g)" %
                            (n, pn, x, z, exp_x, exp_z))
        for i, j, c in fp:
            if c == 0:
                continue
            gx, gz = cx + i - (w >> 1), cz + j - (h >> 1)
            if not (0 <= gx < a["width"] and 0 <= gz < a["height"]):
                problems.append("record %d: part %d cell (%d,%d) outside area %d" % (n, pn, gx, gz, k))
                continue
            prev = cells[k].get((gx, gz))
            if prev is not None:
                prev_kind = kinds[m.parts[prev].directive]
                if kinds[p.directive] == 1 or prev_kind != 1:
                    problems.append("record %d: part %d overlaps part %d at (%d,%d)" % (n, pn, prev, gx, gz))
            if c >= 0x80:
                cells[k][(gx, gz)] = pn
    return cnt, problems


def check_fconv(d):
    import struct as st

    def bank(p):
        b = p.read_bytes()
        sh = st.unpack("<%dH" % (len(b) // 2), b[:len(b) // 2 * 2])
        t = sh[0]
        ents = {sh[2 + 2 * i]: sh[3 + 2 * i] for i in range(t // 2)}
        return sh, t + 1, ents

    def text(bk, i):
        sh, base_, ents = bk
        s = base_ + ents[i]
        out = []
        while s < len(sh) and sh[s] != 0xFF01:
            out.append(sh[s])
            s += 1
        return tuple(out)

    f = bank(d / "fconv.bin")
    t = bank(d / (d.name + "talk_1.mes"))
    same = sum(1 for i in f[2] if i in t[2] and text(f, i) == text(t, i))
    return len(f[2]), len(t[2]), set(f[2]) == set(t[2]), same


def main():
    tot_refs = tot_bad = 0
    for d in sorted(GEDIT.iterdir()):
        if not (d / "mapinfo.cfg").exists() or d.name == "interior":
            continue
        if not (d / "scene.scn").exists():
            print("%s: no scene.scn (mapinfo only)" % d.name)
            continue
        refs, bad, m = check_mapinfo_refs(d)
        tot_refs += refs
        tot_bad += len(bad)
        if bad:
            print("%s: %d/%d unresolved: %s" % (d.name, len(bad), refs, bad[:6]))
    print("mapinfo references: %d, unresolved %d" % (tot_refs, tot_bad))
    for e in ["e01", "e02", "e03", "e04", "e05"]:
        cnt, pr = check_gdata(GEDIT / e)
        print("gdata0.edt %s: %d records, %d problems %s" % (e, cnt, len(pr), pr[:4]))
    for e in sorted(GEDIT.glob("*/fconv.bin")):
        d = e.parent
        if (d / (d.name + "talk_1.mes")).exists():
            print("fconv.bin %s: %s" % (d.name, check_fconv(d)))


if __name__ == "__main__":
    main()
