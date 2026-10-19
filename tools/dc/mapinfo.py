"""Georama / story-scene map description files: data/gedit/<map>/mapinfo.cfg  (Shift-JIS text).

The game parses them with CScriptInterpreter (GetNextTAG / SearchCommand / GetArg, 0x15f100..) and a table of 61 directives
(TAG_PARAM table 0x2679D0, 100 bytes per directive: name pointer, argument types, ..., handler pointer is in the separate
array CommandExe 0x2691B0); the handlers `Command*__FPPv` (0x174ae0..0x1775a4) write into an EDIT_MAP_INFO (0x2c260 bytes).
This module re-implements the tokenizer exactly (including its quirks) and a semantic layer that mirrors the handlers.

Grammar (all verified against the 69 files on the disc, see `python tools/dc/mapinfo.py --check`):
  * `//` comments run to the end of the line, `/* ... */` comments may span lines (PreProcess blanks them).
  * Whitespace = space, TAB, CR, LF.  A directive is a *word* made of upper-case letters/digits/underscores starting with A-Z,
    separated from its arguments by whitespace.  Any other word (not starting with A-Z, or an unknown upper-case word) is
    silently skipped, one whitespace-delimited word at a time.  The word `test` ends the file.
  * Arguments follow in the order of the type list of the directive; each argument is a token that ends at the first
    comma or whitespace.  A comma directly before a token is eaten, so `a,b`, `a b`, `a , b`, `a,\\nb` are all the same.
    Type 0 = string: must start with `"` and contain a closing `"` (so no spaces or commas inside), type 1 = integer
    (characters 0-9 and '-' only), type 2 = float (characters 0-9 '.' '-' only, parsed with atof, so `0.0.` is read as 0.0).
    A malformed argument makes the real game spin in an infinite loop; this parser raises MapInfoError instead.
  * Trailing commas after the last argument are harmless (they end the last token).
"""
import math
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
GEDIT = ROOT / "data" / "gedit"

S, I, F = 0, 1, 2   # argument types

# name -> argument types, in table order (index = directive number used by CommandExe)
TAGS = [
    ("SCN", [S]), ("LIGHT_NO", [I]), ("AMBIENT", [F, F, F]), ("LIGHT_C", [F, F, F, F, F, F, I]),
    ("FOG", [F, F, I, I, I, F, F]), ("BG_COL", [F, F, F]), ("BG_COL2", [F, F, F]), ("DOF", [F, F, I, F, F, I, I]),
    ("CD", [S]), ("GRD_IMG", [S, I]), ("BLD_IMG", [S, I]), ("SKY_IMG", [I, S, I]), ("SUN_IMG", [S, I]),
    ("WATER_IMG", [S, I]), ("FIRE_IMG", [S, I]), ("FLER_IMG", [S, I]), ("IMG", [I, S, I]), ("SKY", [I, S, I]),
    ("SUN", [I, S, I]), ("GROUND", [S] * 8 + [F] * 6), ("BUILD", [S] * 8 + [F] * 6), ("WATER", [S] * 8 + [F] * 6),
    ("WATER_SURFACE", [S, I, I] + [F] * 13 + [I] * 6), ("WATER_SHAKE", [I, I, F, F]),
    ("EDITAREA", [I, S, I, I, F, F, F, F, F]), ("BLD_PARTS", [I, S, I, F]), ("GRD_PARTS", [I, S, I, F]),
    ("PARTS_INFO", [I, I, S, I, I, S, I, S, I, S, I, S, I, S, I, S]), ("ROAD_PARTS", [I, S, I, F]),
    ("ROAD", [I, S, I, F]), ("RIVER_PARTS", [I, S, I, F]), ("RIVER", [I, S, I, F, S]),
    ("BRIDGE_PARTS", [I, S, I, F]), ("LAKE_PARTS", [I, S, I, F, S]), ("ON_RIVER_PARTS", [I, S, I, F, S]),
    ("OBJ_ANIME", [I, I, S] + [F] * 9), ("FIRE", [S] + [F] * 6), ("FLAME", [S] + [F] * 6), ("BRIGHT", [S] + [F] * 6),
    ("OBJECT_TIMER", [S, F, F]), ("ENTRANCE", [S] + [F] * 9), ("MAPJUMP", [I, S]),
    ("PEOPLE", [S, I, I, I, I, F, F, F, F, F]), ("TIME_TABLE_NO", [I]), ("TIME_TABLE", [I] * 7), ("TIME_STOP", []),
    ("SKY_FOLLOW", [I, I, I]), ("SHADOW_LEVEL", [I, F, F, I, I]), ("EDITAREA_RECT", [F] * 6), ("BGM_NO", [I]),
    ("SOUND_SET", [I]), ("REVERBE", [S, I, S, I]), ("MOTION_PARTS", [S] + [F] * 6), ("PEOPLE2", [S, I, I, F]),
    ("SE_AMBIENT_OFF", []), ("WIND", [F, F, F, F]), ("TALK_EVENT", [I, I]), ("CHARA_AMBIENT", [I, F, F, F]),
    ("TALK_ROT", [I]), ("TALK_DIR", [I]), ("PEOPLE_LIST", [I] * 15),
]
TAG_INDEX = {n: i for i, (n, _) in enumerate(TAGS)}
assert len(TAGS) == 61

# The interior mapinfo.cfg (gedit/interior/mapinfo.cfg and the small in/<house>/*.cfg files) is read by LoadInfo (0x19fb70)
# with a different, 15 entry table at 0x26A7F0 (CommandExe handlers 0x19fc50..):
INTERIOR_TAGS = [
    ("AMBIENT", [F, F, F]), ("LIGHT_C", [F, F, F, F, F, F, I]), ("FOG", [F, F, I, I, I, F, F]), ("BG_COL", [F, F, F]),
    ("PROJECTION", [F]), ("PEOPLE", [I, F, F, F, F]), ("CD", [S]), ("OBJ_ANIME", [I, I, S] + [F] * 9),
    ("FIRE", [S, F, F, F, F]), ("FLAME", [S, F, F, F, F]), ("BRIGHT", [S, F, F, F, F]), ("DEBUG", []),
    ("MOTION_PARTS", [S] + [F] * 9), ("WATER_SURFACE", [S, I, I] + [F] * 13 + [I] * 6), ("WATER_SHAKE", [I, I, F, F]),
]

TEXTURE_KINDS = {1: "ground", 2: "building", 3: "sky1", 4: "sky2", 5: "sky3", 6: "sky4", 7: "sun", 0x15: "water",
                 0x17: "lens flare", 0x18: "fire/effect"}

REVERB_NAMES = ["OFF", "ROOM", "STUDIO_A", "STUDIO_B", "STUDIO_C", "HALL", "SPACE", "ECHO", "DELAY", "PIPE", "MAX"]


class MapInfoError(Exception):
    pass


@dataclass
class Directive:
    index: int
    name: str
    args: list
    line: int          # 1-based line of the directive word


def decode(data):
    return data.decode("cp932", errors="replace")


def preprocess(buf):
    """PreProcess__FR9input_str (0x15fe30): blank `//` and `/* */` comments (keeps offsets)."""
    b = bytearray(buf)
    i, n = 0, len(b)
    while i < n:
        if b[i] == 0x2F and i + 1 < n and b[i + 1] == 0x2F:
            while i < n and b[i] not in (0x0A, 0x0D):
                b[i] = 0x20
                i += 1
            continue
        if b[i] == 0x2F and i + 1 < n and b[i + 1] == 0x2A:
            while i < n:
                if b[i] == 0x2A and i + 1 < n and b[i + 1] == 0x2F:
                    b[i] = b[i + 1] = 0x20
                    break
                b[i] = 0x20
                i += 1
            continue
        i += 1
    return bytes(b)


WS = b" \t\r\n"


def parse(data, tags=TAGS):
    """Tokenise like CScriptInterpreter. Returns a list of Directive."""
    buf = preprocess(data)
    n = len(buf)
    pos = 0
    out = []
    names = {t[0]: i for i, t in enumerate(tags)}

    def skipspace():
        nonlocal pos
        while pos < n and buf[pos] in WS:
            pos += 1
        return pos < n

    def line_of(p):
        return buf.count(b"\n", 0, p) + 1

    while True:
        if not skipspace():
            break
        start = pos
        while pos < n and buf[pos] not in WS:
            pos += 1
        if pos >= n:                      # the game needs a terminator after the word
            break
        word = buf[start:pos].decode("latin1")
        pos += 1                          # the consumed terminator
        if not ("A" <= word[0] <= "Z") or word not in names:
            if word == "test":
                break
            continue                      # unknown word: skipped
        idx = names[word]
        types = tags[idx][1]
        args = []
        for t in types:
            if not skipspace():
                raise MapInfoError(f"line {line_of(start)}: unexpected end of file in arguments of {word}")
            c = buf[pos]
            pos += 1
            tok = bytearray()
            if c == 0x2C:                 # ','
                skipspace()
            else:
                tok.append(c)
            while pos < n:
                c = buf[pos]
                pos += 1
                if c == 0x2C or c in WS:
                    break
                tok.append(c)
            s = bytes(tok)
            if t == F:
                if any(not (0x30 <= ch <= 0x39 or ch in b".-") for ch in s):
                    raise MapInfoError(f"line {line_of(start)}: bad float {s!r} in {word}")
                args.append(_atof(s))
            elif t == I:
                if any(not (0x30 <= ch <= 0x39 or ch == 0x2D) for ch in s):
                    raise MapInfoError(f"line {line_of(start)}: bad integer {s!r} in {word}")
                args.append(_atoi(s))
            else:
                if not s.startswith(b'"') or s.find(b'"', 1) < 0:
                    raise MapInfoError(f"line {line_of(start)}: bad string {s!r} in {word}")
                args.append(decode(s[1:s.find(b'"', 1)]))
        out.append(Directive(idx, word, args, line_of(start)))
    return out


def _atof(s):
    """C atof on the accepted character set: longest valid prefix."""
    t = s.decode("ascii")
    best = 0.0
    for k in range(len(t), 0, -1):
        try:
            best = float(t[:k])
            break
        except ValueError:
            continue
    else:
        best = 0.0
    return best


def _atoi(s):
    t = s.decode("ascii")
    k = 0
    if t[:1] == "-":
        k = 1
    while k < len(t) and t[k].isdigit():
        k += 1
    try:
        return int(t[:k])
    except ValueError:
        return 0


# --------------------------------------------------------------------------------------------------------------------
# semantic layer (mirrors the Command* handlers)
# --------------------------------------------------------------------------------------------------------------------

@dataclass
class Light:
    ambient: tuple = (0.0, 0.0, 0.0)
    dirs: list = field(default_factory=lambda: [None] * 4)      # normalised direction (towards the light) per slot 1..4
    colors: list = field(default_factory=lambda: [None] * 4)    # rgb, 128 = full
    fog: tuple = None            # (near, far, r, g, b, alpha_near, alpha_far)
    bg_top: tuple = None
    bg_bottom: tuple = None


@dataclass
class MapObj:               # GROUND / BUILD / WATER (static objects, EDIT_MAP_INFO+0x25a0, 0x2d8 bytes each)
    kind: str
    names: list              # 9 slots: LOD0, LOD1, LOD2, LOD3(mini), shadow, collision, (6) shade?, (7) water, (8) camera
    pos: tuple
    rot_deg: tuple
    anime: list = field(default_factory=list)       # indices into MapInfo.anims
    events: list = field(default_factory=list)      # indices into MapInfo.events


@dataclass
class PartsDef:             # BLD_PARTS/GRD_PARTS/ROAD_PARTS/RIVER_PARTS/BRIDGE_PARTS/LAKE_PARTS/ON_RIVER_PARTS
    index: int
    directive: str
    file: str                # name (PTS entry in scene.scn if it ends in .pts, else base name of the .mds set)
    flag: int                # third argument (0/1: rotation allowed? see doc)
    scale: float             # fourth argument
    extra: str = ""          # LAKE/ON_RIVER: ripple model
    info: dict = None        # PARTS_INFO
    anime: list = field(default_factory=list)
    events: list = field(default_factory=list)


@dataclass
class MapInfo:
    directives: list
    scn: str = "scene.scn"
    bgm: int = -1
    sound_set: int = -1
    reverb: tuple = ("STUDIO_A", 5, "STUDIO_C", 30)
    wind: tuple = (0.3, 0.0, 0.1, 0.4)
    time_stop: bool = False
    se_ambient_off: bool = False
    lights: list = field(default_factory=lambda: [Light() for _ in range(12)])
    dof: tuple = None
    shadow_level: tuple = None
    sky_follow: tuple = (1, 0, 1)
    chara_ambient: dict = field(default_factory=dict)
    textures: list = field(default_factory=list)   # (dir, file, type, flag)
    skies: dict = field(default_factory=dict)       # 1..5 -> (file, anime ids)
    suns: dict = field(default_factory=dict)        # 1..4
    objects: list = field(default_factory=list)
    water_surfaces: list = field(default_factory=list)
    editareas: dict = field(default_factory=dict)
    edit_rects: list = field(default_factory=list)
    parts: dict = field(default_factory=dict)       # index -> PartsDef (BLD/GRD/ROAD/RIVER/BRIDGE/LAKE/ON_RIVER)
    roads: dict = field(default_factory=dict)       # ROAD n -> (file, flag, scale)
    rivers: dict = field(default_factory=dict)      # RIVER n -> (file, flag, scale, water model)
    anims: list = field(default_factory=list)       # OBJ_ANIME
    events: list = field(default_factory=list)      # MAPJUMP + ENTRANCE
    motion_parts: list = field(default_factory=list)
    people: list = field(default_factory=list)
    time_table: dict = field(default_factory=dict)  # (week, slot) -> 6 villager ids (-1 = none)
    people_list: list = field(default_factory=list)
    dirs_used: list = field(default_factory=list)   # every CD value seen
    ignored: list = field(default_factory=list)     # FIRE/FLAME/BRIGHT/OBJECT_TIMER: parsed and thrown away by the game


def build(directives):
    """Apply the directives in order, like the Command* handlers do."""
    m = MapInfo(directives)
    cur_dir = ""
    light_no = 0
    owner = None          # list of (anim ids, event ids) of the last SKY/SUN/GROUND/BUILD/WATER/*_PARTS
    now_parts = None
    now_vill = None
    week = 0
    mapjump = (0, "")

    def full(name):
        return (cur_dir + name) if name else ""

    for d in directives:
        a, nm = d.args, d.name
        if nm == "SCN":
            m.scn = a[0]
        elif nm == "LIGHT_NO":
            light_no = min(max(a[0] - 1, 0), 11)
        elif nm == "AMBIENT":
            m.lights[light_no].ambient = tuple(a)
        elif nm == "LIGHT_C":
            v = (a[0], a[1], a[2])
            ln = math.sqrt(sum(x * x for x in v)) or 1.0
            k = a[6] - 1
            if 0 <= k < 4:
                m.lights[light_no].dirs[k] = tuple(x / ln for x in v)
                m.lights[light_no].colors[k] = (a[3], a[4], a[5])
        elif nm == "FOG":
            m.lights[light_no].fog = tuple(a)
        elif nm == "BG_COL":
            m.lights[light_no].bg_top = tuple(a)
        elif nm == "BG_COL2":
            m.lights[light_no].bg_bottom = tuple(a)
        elif nm == "DOF":
            m.dof = tuple(a)
        elif nm == "CD":
            cur_dir = a[0]
            m.dirs_used.append(a[0])
        elif nm in ("GRD_IMG", "BLD_IMG", "SUN_IMG", "WATER_IMG", "FIRE_IMG", "FLER_IMG"):
            kind = {"GRD_IMG": 1, "BLD_IMG": 2, "SUN_IMG": 7, "WATER_IMG": 0x15, "FIRE_IMG": 0x18, "FLER_IMG": 0x17}[nm]
            m.textures.append((cur_dir, a[0], kind, a[1]))
        elif nm == "SKY_IMG":
            if 0 <= a[0] - 1 < 4:
                m.textures.append((cur_dir, a[1], a[0] + 2, a[2]))
        elif nm == "IMG":
            m.textures.append((cur_dir, a[1], a[0], a[2]))
        elif nm == "SKY":
            if 0 <= a[0] - 1 < 5:
                m.skies[a[0]] = [full(a[1]), a[2], [], []]
                owner = (m.skies[a[0]][2], m.skies[a[0]][3])
        elif nm == "SUN":
            if 0 <= a[0] - 1 < 4:
                m.suns[a[0]] = [full(a[1]), a[2], [], []]
                owner = (m.suns[a[0]][2], m.suns[a[0]][3])
        elif nm in ("GROUND", "BUILD", "WATER"):
            o = MapObj(nm, [full(x) for x in a[:8]] + [""], (a[8], a[9], a[10]), (a[11], a[12], a[13]))
            # quirk: the handler loops over 9 name slots but only 8 strings were parsed; slot 8 aliases the first float
            m.objects.append(o)
            owner = (o.anime, o.events)
        elif nm == "WATER_SURFACE":
            m.water_surfaces.append(dict(texture=a[0], nx=a[1], nz=a[2], p0=tuple(a[3:6]), p1=tuple(a[6:9]),
                                         origin=tuple(a[9:12]), params=tuple(a[12:16]), rgb=tuple(a[16:19]),
                                         extra=tuple(a[19:22]), shakes=[], part=now_parts))
        elif nm == "WATER_SHAKE":
            if m.water_surfaces:
                m.water_surfaces[-1]["shakes"].append(tuple(a))
        elif nm == "EDITAREA":
            m.editareas[a[0]] = dict(grid=full(a[1]), width=a[2], height=a[3], unit=a[4], unit_alt=a[5],
                                     origin=(a[6], a[7], a[8]))
        elif nm in ("BLD_PARTS", "GRD_PARTS", "ROAD_PARTS", "RIVER_PARTS", "BRIDGE_PARTS", "LAKE_PARTS", "ON_RIVER_PARTS"):
            p = PartsDef(a[0], nm, full(a[1]) if a[1] else "", a[2], a[3], a[4] if len(a) > 4 else "")
            m.parts[a[0]] = p
            now_parts = a[0]
            owner = (p.anime, p.events)
        elif nm == "PARTS_INFO":
            if now_parts is not None and now_parts in m.parts:
                m.parts[now_parts].info = dict(
                    width=a[0], height=a[1], grid=a[2], max_count=a[3],
                    elements=[(a[4 + 2 * i], a[5 + 2 * i]) for i in range(6)])
        elif nm == "ROAD":
            m.roads[a[0]] = (full(a[1]), a[2], a[3])
        elif nm == "RIVER":
            m.rivers[a[0]] = (full(a[1]), a[2], a[3], full(a[4]))
        elif nm == "OBJ_ANIME":
            idx = len(m.anims) + 1
            m.anims.append(dict(kind=a[0], mode=a[1], target=a[2], init=tuple(a[3:6]), delta=tuple(a[6:9]),
                                end=tuple(a[9:12])))
            if owner is not None:
                owner[0].append(idx)
        elif nm in ("FIRE", "FLAME", "BRIGHT", "OBJECT_TIMER"):
            m.ignored.append(d)
        elif nm == "MAPJUMP":
            mapjump = (a[0], a[1])
            m.events.append(dict(kind="mapjump", jump_id=a[0], map=a[1], door=None))
            if owner is not None:
                owner[1].append(len(m.events))
        elif nm == "ENTRANCE":
            if m.events:
                ev = m.events[-1]
                side = a[0][:1].upper()
                ev["door"] = dict(side={"L": -1, "R": 1}.get(side, 0), pos=tuple(a[1:4]), rot=tuple(a[4:7]),
                                  extent=tuple(a[7:10]))
        elif nm == "PEOPLE":
            m.people.append(dict(name=a[0], part=a[1], element=a[2], indoor_only=a[3], moves=a[4],
                                 speed=a[5], pos=tuple(a[6:9]), rot_y=a[9], talk_event=None, talk_rot=None, talk_dir=None))
        elif nm == "PEOPLE2":
            m.people.append(dict(name=a[0], part=-1, element=-1, indoor_only=a[1], moves=a[2], speed=a[3],
                                 pos=(0, 0, 0), rot_y=0, talk_event=None, talk_rot=None, talk_dir=None))
            now_vill = m.people[-1]
        elif nm == "TALK_EVENT" and now_vill is not None:
            now_vill["talk_event"] = tuple(a)
        elif nm == "TALK_ROT" and now_vill is not None:
            now_vill["talk_rot"] = a[0]
        elif nm == "TALK_DIR" and now_vill is not None:
            now_vill["talk_dir"] = a[0]
        elif nm == "TIME_TABLE_NO":
            week = a[0] if 0 <= a[0] <= 6 else 0
        elif nm == "TIME_TABLE":
            if a[0] >= 0:
                m.time_table[(week, a[0])] = tuple(a[1:7])
        elif nm == "TIME_STOP":
            m.time_stop = True
        elif nm == "SKY_FOLLOW":
            m.sky_follow = tuple(a)
        elif nm == "SHADOW_LEVEL":
            m.shadow_level = tuple(a)
        elif nm == "EDITAREA_RECT":
            lo = (min(a[0], a[3]), min(a[1], a[4]), min(a[2], a[5]))
            hi = (max(a[0], a[3]), max(a[1], a[4]), max(a[2], a[5]))
            m.edit_rects.append((lo, hi))
        elif nm == "BGM_NO":
            m.bgm = a[0]
        elif nm == "SOUND_SET":
            m.sound_set = a[0]
        elif nm == "REVERBE":
            m.reverb = (a[0], a[1], a[2], a[3])
        elif nm == "MOTION_PARTS":
            m.motion_parts.append(dict(file=full(a[0]) if a[0] else "", pos=tuple(a[1:4]), rot=tuple(a[4:7])))
        elif nm == "SE_AMBIENT_OFF":
            m.se_ambient_off = True
        elif nm == "WIND":
            m.wind = tuple(a)
        elif nm == "CHARA_AMBIENT":
            if 0 <= a[0] - 1 < 4:
                m.chara_ambient[a[0]] = tuple(a[1:4])
        elif nm == "PEOPLE_LIST":
            m.people_list = list(a)
    return m


def load(path):
    p = Path(path)
    if p.is_dir():
        p = p / "mapinfo.cfg"
    data = p.read_bytes()
    return build(parse(data))


def _check_interior():
    files = sorted(GEDIT.glob("**/in/**/*.cfg")) + [GEDIT / "interior" / "mapinfo.cfg"]
    import collections
    cnt = collections.Counter()
    bad = 0
    for f in files:
        try:
            ds = parse(f.read_bytes(), INTERIOR_TAGS)
        except MapInfoError as e:
            print("FAIL", f, e)
            bad += 1
            continue
        for d in ds:
            cnt[d.name] += 1
    print(len(files), "interior cfg files,", bad, "failed;", dict(cnt))


def _check():
    import collections
    files = sorted(GEDIT.glob("*/mapinfo.cfg"))
    cnt = collections.Counter()
    nargs = collections.Counter()
    for f in files:
        if f.parent.name == "interior":
            continue
        data = f.read_bytes()
        try:
            ds = parse(data)
        except MapInfoError as e:
            print("FAIL", f, e)
            continue
        for d in ds:
            cnt[d.name] += 1
        m = build(ds)
        # sanity: every directive was consumed, words skipped are only garbage between directives
        nargs[len(ds)] += 1
    print(len(files) - 1, "files parsed")
    for k, v in sorted(cnt.items(), key=lambda kv: TAG_INDEX[kv[0]]):
        print(f"  {k:16s} {v}")
    unused = [n for n, _ in TAGS if n not in cnt]
    print("directives never used on the disc:", unused)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        _check()
        _check_interior()
    elif len(sys.argv) > 1:
        m = load(sys.argv[1])
        for k, v in m.__dict__.items():
            if k != "directives":
                print(k, v)
    else:
        print(__doc__)
