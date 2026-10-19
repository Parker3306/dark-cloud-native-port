"""Build a replacement character pack (.chr) from a fitted low-poly mesh + baked atlas, reusing the ORIGINAL skeleton, motions, bind pose and config.

Input  : low.npz  (pos [V,3] game space, tri [T,3], uv [T,3,2] per corner, Blender uv: v up) from blender/fit_bake.py
         atlas.png (baked colour for those UVs)
         the original pack (data/dun/mainchara/<id>.chr) + work/models/<name>/model.{json,npz} (world-space original parts and their weights)
Output : a new pack with
   - the MDS where ONLY the `skin` object keeps a mesh (the new character); every rigid part (hands, feet, face, hair, hat, belt, bag...) loses its mesh;
     the cloth objects keep tiny invisible meshes so the cloth simulation and its allocations still work,
   - a new .wgt giving every vertex of the new mesh skin weights transferred from the original (rigid parts count as 100 % to their driving joint),
   - a new IMG container: one 256x256 8-bit atlas (named like the original body texture) + tiny transparent textures for the cloth,
   - base.cfg without the face TEX_ANIME blocks (the new face is part of the atlas).
Usage: python -I tools/dc/build_character.py <name> <fit dir> [--out pack.chr] [--hd-dir dir]
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(ROOT / "scripts"))
import export_model as EM  # noqa: E402
import img as I  # noqa: E402
import mds as M  # noqa: E402
import pack as P  # noqa: E402
import wgt as W  # noqa: E402


# ---------------------------------------------------------------- textures
def tim2_8bit(indices, palette_rgba, w, h):
    """TIM2 with an 8-bit image and a 32-bit CLUT, in the same layout the game's own files use. palette_rgba: [256,4] uint8, alpha 0..255."""
    pal = np.zeros((256, 4), np.uint8)
    pal[: len(palette_rgba)] = palette_rgba
    pal[:, 3] = np.minimum(pal[:, 3].astype(np.uint16) // 2 + (pal[:, 3] > 0) * 0, 128)   # PS2 alpha: 0x80 = opaque
    order = np.arange(256)
    blk = (order // 8) % 4
    order = np.where(blk == 1, order + 8, np.where(blk == 2, order - 8, order))      # CLUT swizzle (an involution)
    pal = pal[order]
    image = indices.astype(np.uint8).tobytes()
    clut = pal.tobytes()
    total = 48 + len(image) + len(clut)
    head = b"TIM2" + bytes([3, 0, 1, 0]) + bytes(8)
    pic = struct.pack("<IIIHHBBBBHHQQII", total, len(clut), len(image), 48, 256, 0, 1, 3, 5, w, h, 0, 0xffffff8800000000, 0, 0)
    return head + pic + image + clut


def quantize(im_rgb, size):
    small = im_rgb.resize((size, size), Image.LANCZOS)
    q = small.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(q.getpalette()[: 256 * 3], np.uint8).reshape(-1, 3)
    rgba = np.zeros((256, 4), np.uint8)
    rgba[: len(pal), :3] = pal
    rgba[:, 3] = 255
    return np.asarray(q, np.uint8), rgba


def build_img(textures):
    """textures: [(name, tim2 bytes)] -> IMG container bytes."""
    n = len(textures)
    off = 0x10 + 0x30 * n
    head = bytearray(b"IMG\0" + struct.pack("<I", n) + bytes(8))
    body = bytearray()
    for name, tim in textures:
        e = bytearray(0x30)
        e[:len(name)] = name.encode()
        struct.pack_into("<I", e, 0x20, off + len(body))
        head += e
        body += tim
    return bytes(head + body)


# ---------------------------------------------------------------- atlas clean-up
def _uv_coverage(uv, size):
    """Boolean mask of the atlas texels covered by the low mesh's UV triangles. uv: [T,3,2] (v up)."""
    from PIL import ImageDraw
    im = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(im)
    for t in uv:
        d.polygon([(float(u) * size, (1.0 - float(v)) * size) for u, v in t], fill=255)
    return np.asarray(im) > 0


def inpaint_holes(atlas, uv, thresh=3):
    """Texels the baker missed (rays that hit nothing) come out black inside the islands: fill them from their neighbours."""
    import cv2
    a = np.asarray(atlas.convert("RGB"))
    size = a.shape[0]
    cov = _uv_coverage(uv, size)
    lum = a.astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    hole = cov & (lum < thresh)
    hole = cv2.dilate(hole.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0
    fixed = cv2.inpaint(a, (hole * 255).astype(np.uint8), 5, cv2.INPAINT_TELEA)
    print(f"atlas: inpainted {int(hole.sum())} black texels inside islands ({100 * hole.sum() / max(1, cov.sum()):.1f} % of the covered area)")
    return Image.fromarray(fixed)


def brighten(atlas, uv, target=100.0):
    """The game multiplies textures by its lighting, so a realistic (dark) albedo looks dim: lift mid-tones until the covered area's mean luminance reaches `target`
    (the stock body textures sit at 85-150)."""
    a = np.asarray(atlas.convert("RGB"), dtype=np.float32) / 255.0
    cov = _uv_coverage(uv, a.shape[0])
    w = np.array([0.299, 0.587, 0.114], np.float32)

    def mean_at(g):
        return float((((a[cov] ** (1.0 / g)) @ w) * 255).mean())
    lo, hi = 0.5, 4.0
    if mean_at(1.0) >= target:
        return atlas
    for _ in range(25):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if mean_at(mid) < target else (lo, mid)
    g = (lo + hi) / 2
    print(f"atlas: brightness gamma {g:.2f} (mean luminance {mean_at(1.0):.0f} -> {mean_at(g):.0f})")
    return Image.fromarray(np.clip(a ** (1.0 / g) * 255, 0, 255).astype(np.uint8))


# ---------------------------------------------------------------- weights
def write_bind(pack_rel, model_dir, stem=None):
    """Bind-pose world matrices of every object, rebuilt from the pack's .bbp (a chain of LOCAL bind matrices, row vectors). For some characters
    (Ruby) the model's own object matrices are an arms-down rest pose while the skin mesh and these bind matrices are a T-pose."""
    pk = P.parse((ROOT / "data" / pack_rel).read_bytes())
    stem = stem or Path(pack_rel).stem
    mds = M.parse(pk.get(stem + ".mds").data)
    bbp = pk.get(stem + ".bbp").data
    n = len(bbp) // 64
    W = []
    for i, o in enumerate(mds.objects):
        loc = np.array(struct.unpack_from("<16f", bbp, 64 * i), dtype=np.float64).reshape(4, 4) if i < n else np.eye(4)
        W.append(loc if o.parent < 0 else loc @ W[o.parent])
    (Path(model_dir) / "bind_world.json").write_text(json.dumps([w.tolist() for w in W]))
    return W


def load_bind(model_dir):
    f = Path(model_dir) / "bind_world.json"
    return [np.array(w) for w in json.loads(f.read_text())] if f.exists() else None


def reference_samples(model_dir, exclude=()):
    """Dense surface samples of the ORIGINAL character with their skin weights: (points [N,3], weights [N, B], bone ids [B])."""
    meta = json.loads((Path(model_dir) / "model.json").read_text())
    data = np.load(Path(model_dir) / "model.npz")
    objs = meta["objects"]
    vw = {int(v): {int(b): float(w) for b, w in ws.items()} for v, ws in meta["weights"]["vertices"].items()}
    bones = sorted({b for ws in vw.values() for b in ws} | {o["parent"] for o in objs if False})
    rigid_parent = {}
    meta["meshes"] = [mm for mm in meta["meshes"] if mm["name"] not in exclude]
    for mm in meta["meshes"]:
        if mm["name"] != "skin":
            rigid_parent[mm["key"]] = objs[mm["object"]]["parent"]
    bones = sorted(set(bones) | set(rigid_parent.values()))
    bi = {b: i for i, b in enumerate(bones)}
    pts, wts, cloth = [], [], []
    bind = load_bind(model_dir)
    for mm in meta["meshes"]:
        k = mm["key"]
        pos = data[f"{k}_pos"].astype(np.float64)
        tri = data[f"{k}_tri"][:, :, 0]
        if bind is not None and mm["name"] != "skin":          # rigid parts sit in the rest pose; the skin is in the bind pose: bring them to the bind pose
            b = objs[mm["object"]]["parent"]
            if b is not None and b >= 0:
                rest = np.array(objs[b]["world"])
                if 0.05 < np.abs(rest[3, :3] - bind[b][3, :3]).max() < 7.0:   # skeleton really is posed differently (Ruby's arms); larger gaps mean the .bbp chain is not a clean bind pose for this bone
                    pos = (np.c_[pos, np.ones(len(pos))] @ np.linalg.inv(rest) @ bind[b])[:, :3]
        if mm["name"] == "skin":
            vwm = np.zeros((len(pos), len(bones)))
            for v, ws in vw.items():
                for b, w in ws.items():
                    vwm[v, bi[b]] = w / 100.0
        else:
            vwm = np.zeros((len(pos), len(bones)))
            vwm[:, bi[rigid_parent[k]]] = 1.0
        a, b_, c = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
        wa, wb, wc = vwm[tri[:, 0]], vwm[tri[:, 1]], vwm[tri[:, 2]]
        for ba, bb, bc in ((1 / 3, 1 / 3, 1 / 3), (.5, .5, 0), (0, .5, .5), (.5, 0, .5), (.7, .15, .15), (.15, .7, .15), (.15, .15, .7)):
            pts.append(ba * a + bb * b_ + bc * c)
            wts.append(ba * wa + bb * wb + bc * wc)
            cloth.append(np.full(len(tri), mm["name"].startswith("cloth")))
        pts.append(pos)
        wts.append(vwm)
        cloth.append(np.full(len(pos), mm["name"].startswith("cloth")))
    reference_samples.cloth = np.concatenate(cloth)
    return np.concatenate(pts), np.concatenate(wts), bones


# Body-geometry numbers used by the weight transfer and the winding repair. These defaults are Toan's; `auto_profile` derives them for another
# character from the positions of its joints.
PROFILE = dict(leg_right=(6, 7, 10, 13), leg_left=(18, 19, 22, 25), comp_ymax=8.0, leg_split_y=9.3,
               arm_x=2.8, arm_ymin=11.5, arm_cy=12.85, arm_cz=0.0, leg_ymax=8.6, leg_cx=0.75, leg_cz=0.3, torso_cz=0.2)


ARM_HINT = ("sdr", "arm", "hand")
LEG_HINT = ("leg", "foot", "toe")
TORSO_HINT = ("mune", "spine", "hip", "sokei", "neck")
HEAD_HINT = ("face", "head", "mimi")
NOT_BONES = ("null", "eff", "weapon", "item", "dcol", "ctr", "chn")


def joint_regions(model_dir):
    """{bone object index: region} from the skeleton: 0 torso, 1 head, 2 arm +x, 3 arm -x, 4 leg +x, 5 leg -x. Bone names decide when they say so
    (Ruby: l_farm, r_upleg, spine, face ...); otherwise a joint inherits the region of its nearest bone ancestor and chain roots are classified by position
    in the BIND pose. Returns (regions, bind positions, model height)."""
    meta = json.loads((Path(model_dir) / "model.json").read_text())
    data = np.load(Path(model_dir) / "model.npz")
    H = max(float(data[m["key"] + "_pos"][:, 1].max()) for m in meta["meshes"] if m["name"] in ("skin", "kao", "face"))
    objs = meta["objects"]
    bind = load_bind(model_dir)
    children = {}
    for j, oj in enumerate(objs):
        children.setdefault(oj["parent"], []).append(j)

    def subtree(i):
        res, stack = [], list(children.get(i, []))
        while stack:
            k = stack.pop()
            res.append(k)
            stack += children.get(k, [])
        return res

    def is_jnt(k):
        return objs[k]["name"].lower().startswith("jnt")

    def jnt_tips(i):
        """Leaf jnt joints under (and including) joint i: the ends of the chain(s) it starts."""
        sub = [k for k in subtree(i) if is_jnt(k)]
        return [k for k in [i] + sub if not any(is_jnt(c) for c in subtree(k))] or [i]

    out, pos = {}, {}
    for i, o in enumerate(objs):
        name = o["name"].lower()
        if o["has_mesh"] or name.startswith(NOT_BONES) or name.startswith("eff"):
            continue
        x, y, z = np.array(o["world"])[3, :3]            # rest-pose position: for Toan/Xiao it equals the bind pose; Ruby's names decide instead
        pos[i] = (x, y, z)
        side = (lambda: 2 if x > 0 else 3)
        if any(h in name for h in ARM_HINT):
            r = side()
        elif any(h in name for h in LEG_HINT):
            r = 4 if x >= 0 else 5
        elif any(h in name for h in TORSO_HINT):
            r = 0
        elif any(h in name for h in HEAD_HINT):
            r = 1
        else:
            tips = jnt_tips(i)
            if len(tips) > 1:                                  # a branching joint (pelvis, chest) is torso; branching only inside the head (ears) is head
                r = 1 if all(np.array(objs[t]["world"])[3, 1] > 0.8 * H for t in tips) else 0
            else:
                tx, ty, tz = np.array(objs[tips[0]]["world"])[3, :3]
                bx = bind[tips[0]][3, 0] if bind is not None else tx             # the rest pose can hang the arms down (Ungaga, Ruby): the bind pose has them out
                if max(abs(tx), abs(bx)) > 0.15 * H and (y > 0.4 * H or abs(bx) > 0.15 * H):
                    ax = bx if abs(bx) > abs(tx) else tx
                    r = 2 if ax > 0 else 3
                elif ty < 0.15 * H:
                    r = 4 if tx >= 0 else 5
                elif ty > 0.8 * H:
                    r = 1
                else:
                    r = 0
        out[i] = r
    return out, pos, H


def auto_profile(model_dir):
    reg, pos, H = joint_regions(model_dir)
    legs_l = tuple(sorted(b for b, r in reg.items() if r == 4))
    legs_r = tuple(sorted(b for b, r in reg.items() if r == 5))
    bind = load_bind(model_dir)
    bpos = {b: tuple(bind[b][3, :3]) if bind is not None else pos[b] for b in pos}            # T-pose (bind) positions: the arm axis of the SKIN mesh
    arms = [bpos[b] for b, r in reg.items() if r in (2, 3) and bpos[b][1] > 0.6 * H]
    if not arms:                                                                              # bind chain unusable for this character: rest pose
        arms = [pos[b] for b, r in reg.items() if r in (2, 3) and pos[b][1] > 0.6 * H]
    legc = [abs(pos[b][0]) for b in legs_l + legs_r if pos[b][1] < 0.55 * H]
    prof = dict(PROFILE)
    prof.update(leg_left=legs_l, leg_right=legs_r, comp_ymax=0.48 * H, leg_split_y=0.56 * H, leg_ymax=0.52 * H,
                arm_ymin=0.68 * H, arm_cy=float(np.mean([a[1] for a in arms])), arm_cz=float(np.mean([a[2] for a in arms])),
                arm_x=0.17 * H, leg_cx=float(np.mean([c for c in legc if c > 0.01] or [0.05 * H])))
    return prof, reg


def transfer_weights(verts, tri, ref_pts, ref_w, bones=None, passes=2, max_inf=3, cloth_ref=None, cloth_bias=0.35):
    n = len(verts)
    out = np.zeros((n, ref_w.shape[1]))
    if cloth_ref is None:
        cloth_ref = np.zeros(len(ref_pts), bool)
    right = left = None
    if bones is not None:                                        # the feet nearly touch: never borrow weights from the other leg
        right = ref_w[:, [bones.index(b) for b in PROFILE['leg_right'] if b in bones]].sum(axis=1) > 0.5
        left = ref_w[:, [bones.index(b) for b in PROFILE['leg_left'] if b in bones]].sum(axis=1) > 0.5
    # side of each leg vertex = side of its connected component below the pelvis (a vertex a hair across x=0 must still follow its own boot)
    parent = list(range(n))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for t in tri:
        if verts[t, 1].max() < PROFILE['comp_ymax']:
            parent[find(t[1])] = find(t[0])
            parent[find(t[2])] = find(t[0])
    comp = np.array([find(i) for i in range(n)])
    side = np.where(verts[:, 0] >= 0, 1, -1)
    for c in np.unique(comp[verts[:, 1] < PROFILE['comp_ymax']]):
        m = (comp == c) & (verts[:, 1] < PROFILE['comp_ymax'])
        if m.sum() > 20:
            side[m] = 1 if verts[m, 0].mean() >= 0 else -1
    for s in range(0, n, 256):
        d = ((verts[s:s + 256, None, :] - ref_pts[None, :, :]) ** 2).sum(axis=2)
        d[:, cloth_ref] *= cloth_bias                  # the original poncho hangs from the torso, not from the arms: prefer it over nearby arm skin
        if right is not None:
            low = verts[s:s + 256, 1] < PROFILE['leg_split_y']
            d[np.ix_(low & (side[s:s + 256] > 0), right)] += 1e3
            d[np.ix_(low & (side[s:s + 256] < 0), left)] += 1e3
        out[s:s + 256] = ref_w[d.argmin(axis=1)]
    nbr = [set() for _ in range(n)]
    for t in tri:
        for i in range(3):
            nbr[t[i]].update((t[(i + 1) % 3], t[(i + 2) % 3]))
    for _ in range(passes):
        new = out.copy()
        for v in range(n):
            if nbr[v]:
                new[v] = 0.5 * out[v] + 0.5 * out[list(nbr[v])].mean(axis=0)
        out = new
    keep = np.argsort(-out, axis=1)[:, :max_inf]
    res = np.zeros_like(out)
    rows = np.arange(n)[:, None]
    res[rows, keep] = out[rows, keep]
    res /= res.sum(axis=1, keepdims=True)
    return res * 100.0


# ---------------------------------------------------------------- strips
def make_strips(tris, seed=0):
    """Greedy triangle stripper. tris: [T,3] vertex ids, each in the desired winding. Returns a list of strips (vertex id lists) that
    triangulate exactly like Dark Cloud's strips (even: a,b,c; odd: b,a,c)."""
    tris = [tuple(int(x) for x in t) for t in tris]
    edge = {}
    for i, (a, b, c) in enumerate(tris):
        for e in ((a, b), (b, c), (c, a)):
            edge.setdefault(e, []).append(i)
    used = [False] * len(tris)

    def neighbours_left(i):
        a, b, c = tris[i]
        n = 0
        for (x, y) in ((b, a), (c, b), (a, c)):                # opposite directed edges
            n += sum(1 for j in edge.get((x, y), []) if not used[j])
        return n

    def grow(start, rot, mark):
        a, b, c = tris[start]
        s = [(a, b, c), (b, c, a), (c, a, b)][rot]
        s = list(s)
        taken = [start]
        mark[start] = True
        while True:
            m = len(s) - 2                                      # index of the next triangle
            x, y = s[-2], s[-1]
            want = (y, x) if m % 2 == 1 else (x, y)             # directed edge the next triangle must contain
            nxt = None
            for j in edge.get(want, []):
                if not mark[j] and not used[j]:
                    nxt = j
                    break
            if nxt is None:
                break
            ta, tb, tc = tris[nxt]
            w = {(ta, tb): tc, (tb, tc): ta, (tc, ta): tb}[want]
            s.append(w)
            taken.append(nxt)
            mark[nxt] = True
        return s, taken

    import heapq
    import random
    rng = random.Random(seed)
    jitter = [rng.random() for _ in tris]
    heap = [(neighbours_left(i), jitter[i], i) for i in range(len(tris))]
    heapq.heapify(heap)
    strips = []
    while heap:
        deg, _j, i = heapq.heappop(heap)
        if used[i]:
            continue
        cur = neighbours_left(i)
        if cur != deg:
            heapq.heappush(heap, (cur, jitter[i], i))
            continue
        best = None
        for rot in range(3):
            mk = {}
            s_, taken = grow(i, rot, _Marks(mk))
            if best is None or len(taken) > len(best[1]):
                best = (s_, taken)
        for j in best[1]:
            used[j] = True
        strips.append(best[0])
    return strips


class _Marks:
    def __init__(self, d):
        self.d = d

    def __getitem__(self, k):
        return self.d.get(k, False)

    def __setitem__(self, k, v):
        self.d[k] = v


def chunk_strip_groups(strips, limit=48):
    """Pack strips into groups of at most `limit` tuples (bridged with degenerate triangles) so the game never has to split a strip itself.
    A strip longer than the limit is cut with a 2-vertex overlap at an even offset (keeps the winding parity)."""
    pieces = []
    for st in strips:
        if len(st) <= limit:
            pieces.append(st)
        else:
            step = limit - 2
            i = 0
            while i < len(st) - 2:
                pieces.append(st[i:i + limit])
                i += step
    groups, cur = [], []
    for pc in pieces:
        extra = 0 if not cur else (2 if (len(cur) + 2) % 2 == 0 else 3)
        if cur and len(cur) + extra + len(pc) > limit:
            groups.append(cur)
            cur = []
        if cur:
            cur += [cur[-1], pc[0]]
            if len(cur) % 2:
                cur.append(pc[0])
        cur += pc
    if cur:
        groups.append(cur)
    return groups


def join_strips(strips):
    out = []
    for s in strips:
        if out:
            out += [out[-1], s[0]]
            if len(out) % 2:                                    # the next strip's first triangle must start on an even index
                out.append(s[0])
        out += s
    return out


# ---------------------------------------------------------------- main build
def orient_consistent(pos, tri, rounds=30):
    """Start from the volume-based outward orientation, then repair reversed patches: wherever two neighbouring faces run their shared edge
    the same way, one of them is reversed; flip the one that points less away from the body axis. (Backface culling in the game makes a reversed
    patch vanish and we then see the dark inside of the model.)"""
    tri = orient_outward(pos, tri).copy()
    cen = pos[tri].mean(axis=1)
    rad = radial_dir(cen)
    total_flips = 0
    best = (10 ** 9, tri.copy())
    for rnd in range(rounds):
        a_, b_, c_ = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
        n = np.cross(b_ - a_, c_ - a_)
        score = ((n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-12)) * rad).sum(axis=1)
        directed = {}
        for i, t in enumerate(tri):
            for k in range(3):
                directed.setdefault((int(t[k]), int(t[(k + 1) % 3])), []).append(i)
        flip = set()
        for (a, b), fs in directed.items():
            if len(fs) == 2:                                    # the same directed edge used twice: one face is reversed
                f, g = fs
                flip.add(f if score[f] < score[g] else g)
        conflicts = sum(1 for fs in directed.values() if len(fs) == 2)
        if conflicts < best[0]:
            best = (conflicts, tri.copy())
        if not flip:
            break
        for f in flip:
            tri[f] = tri[f][[0, 2, 1]]
        total_flips += len(flip)
    print(f"orientation repair: best round leaves {best[0]} conflicting edges ({total_flips} flips tried)")
    return best[1]


def orient_outward(pos, tri):
    """Flip all triangles if the mesh's signed volume is negative (so cross(b-a, c-a) points outward)."""
    a, b, c = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
    vol = np.einsum("ij,ij->i", a, np.cross(b, c)).sum()
    return tri if vol > 0 else tri[:, [0, 2, 1]]


def radial_dir(c):
    """Direction pointing away from the body part's axis, for the face centroids c [N,3] (game space, T-pose)."""
    P_ = PROFILE
    r = np.zeros_like(c)
    arm = (np.abs(c[:, 0]) > P_["arm_x"]) & (c[:, 1] > P_["arm_ymin"])
    leg = (c[:, 1] < P_["leg_ymax"]) & ~arm
    r[arm] = np.stack([np.zeros(arm.sum()), c[arm, 1] - P_["arm_cy"], c[arm, 2] - P_["arm_cz"]], axis=1)
    cx = np.where(c[:, 0] >= 0, P_["leg_cx"], -P_["leg_cx"])
    r[leg] = np.stack([c[leg, 0] - cx[leg], np.zeros(leg.sum()), c[leg, 2] - P_["leg_cz"]], axis=1)
    rest = ~arm & ~leg
    r[rest] = np.stack([c[rest, 0], np.zeros(rest.sum()), c[rest, 2] - P_["torso_cz"]], axis=1)
    return r / (np.linalg.norm(r, axis=1, keepdims=True) + 1e-9)


def vertex_normals(pos, tri):
    """Area-weighted vertex normals from the faces that face away from the body axis only: the inner side of thin shells (poncho,
    scarf) would otherwise drag the normals of shared rim vertices inward and the game's lighting turns those patches black."""
    a, b, c = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
    fn = np.cross(b - a, c - a)
    nfn = fn / (np.linalg.norm(fn, axis=1, keepdims=True) + 1e-12)
    outward = (nfn * radial_dir((a + b + c) / 3)).sum(axis=1) > -0.15
    n = np.zeros_like(pos)
    for k in range(3):
        np.add.at(n, tri[outward, k], fn[outward])
    allf = np.zeros_like(pos)
    for k in range(3):
        np.add.at(allf, tri[:, k], fn)
    lacking = np.linalg.norm(n, axis=1) < 1e-9                  # vertices only touched by inward faces: use all faces, flipped to point outward
    n[lacking] = allf[lacking]
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    return n


def original_winding_sign(mds, world):
    """+1 if the original skin triangles (in the game's strip/list order) have cross(b-a, c-a) along the stored normals, else -1."""
    i, o = next((i, o) for i, o in enumerate(mds.objects) if o.name == "skin")
    m = o.mesh
    pos = np.array(m.positions)[:, :3]
    nrm = np.array(m.normals)[:, :3]
    s = 0.0
    for g in m.groups:
        for a, b, c in EM.triangulate(g, m.stride):
            pa, pb, pc = pos[a[0]], pos[b[0]], pos[c[0]]
            s += float(np.dot(np.cross(pb - pa, pc - pa), nrm[a[1]] + nrm[b[1]] + nrm[c[1]]))
    return 1 if s > 0 else -1


CONFIGS = {
    "toan": dict(skin="skin", keep_meshes=(), body_tex="c01d02", clear_tex="c01d04", keep_textures=(), profile="toan", target_lum=85.0),
    "c04b": dict(skin="skin", keep_meshes=("o",), body_tex="c04b04", clear_tex=None, keep_textures=("c04b02",), profile="auto", full_img="tiny"),
    "c05a": dict(skin="skin", keep_meshes=(), body_tex="c05a03", clear_tex=None, keep_textures=("c05a02", "c05a04"), profile="auto", full_img="tiny"),
    "c10b": dict(stem="c10a", skin="skin", keep_meshes=(), body_tex="c10a02", clear_tex="c10a03", keep_textures=(), profile="auto", target_lum=95.0),
    "c04b_02": dict(skin="skin", keep_meshes=("o",), body_tex="c04b02", clear_tex=None, keep_textures=(), profile="auto"),
    "c04b_only": dict(skin="skin", keep_meshes=("o",), body_tex="c04b04", clear_tex=None, keep_textures=(), profile="auto"),
}


def build(name, pack_rel, model_dir, fit_dir, out_pack, atlas_size=256, hd_dir=None, cfg=None):
    cfg = cfg or CONFIGS.get(name, CONFIGS["toan"])
    stem = cfg.get("stem") or Path(pack_rel).stem
    write_bind(pack_rel, model_dir, stem)
    if cfg.get("profile") == "auto":
        prof, _ = auto_profile(model_dir)
        PROFILE.update(prof)
        print("profile:", {k: (round(v, 2) if isinstance(v, float) else v) for k, v in PROFILE.items()})
    pk = P.parse((ROOT / "data" / pack_rel).read_bytes())
    mds = M.parse(pk.get(f"{stem}.mds").data)
    world = EM.world_matrices(mds)
    low = np.load(Path(fit_dir) / "low.npz")
    pos, tri, uv = low["pos"].astype(np.float64), low["tri"], low["uv"]
    orig_tri = tri
    tri = orient_consistent(pos, tri)
    sign = original_winding_sign(mds, world)
    if sign < 0:
        tri = tri[:, [0, 2, 1]]
    print(f"new mesh: {len(pos)} vertices, {len(tri)} triangles; original winding sign {sign}")

    skin_i, skin = next((i, o) for i, o in enumerate(mds.objects) if o.name == cfg["skin"])
    inv = np.linalg.inv(world[skin_i])
    local = np.c_[pos, np.ones(len(pos))] @ inv
    nrm = vertex_normals(pos, tri if sign > 0 else tri[:, [0, 2, 1]])        # normals point outward regardless of the draw winding
    # UV: per-corner -> unique uv entries; tuple = (position, normal, uv index)
    corner_uv = np.clip(uv, 0.0, 1.0).reshape(-1, 2)
    corner_uv = np.stack([corner_uv[:, 0], 1.0 - corner_uv[:, 1]], axis=1)       # game v runs top-down
    # triangles came out of Blender with their corners in the same order as `low['tri']` (before orientation flips); rebuild the per-corner lookup
    orig_tri = low["tri"]
    tri_corner_uv = {}
    for ti, t in enumerate(orig_tri):
        for k in range(3):
            tri_corner_uv[(ti, int(t[k]))] = tuple(np.round(np.array([uv[ti][k][0], 1.0 - uv[ti][k][1]]), 5))
    uv_index, uv_list = {}, []
    tuples_of = []                                              # per output triangle: 3 tuple ids (position, uv index)
    tuple_id, tuple_list = {}, []
    # tri may have been flipped; map back by sorting corners of each original triangle
    flipped = not np.array_equal(tri, orig_tri)
    for ti in range(len(tri)):
        ids = []
        for corner in tri[ti]:
            key = tri_corner_uv[(ti, int(corner))]
            if key not in uv_index:
                uv_index[key] = len(uv_list)
                uv_list.append(key)
            tk = (int(corner), uv_index[key])
            if tk not in tuple_id:
                tuple_id[tk] = len(tuple_list)
                tuple_list.append(tk)
            ids.append(tuple_id[tk])
        tuples_of.append(ids)
    best = None
    for seed in range(8):
        st = make_strips(np.array(tuples_of), seed)
        sq = join_strips(st)
        if best is None or len(sq) < len(best[1]):
            best = (st, sq)
    strips, _ = best
    long_strips = [st for st in strips if len(st) - 2 >= 3]
    short = [st for st in strips if len(st) - 2 < 3]
    seq_groups = chunk_strip_groups(long_strips)
    seq = [t for g in seq_groups for t in g]
    # short strips -> plain triangle list, honouring the strip's alternating winding
    lst = []
    for st in short:
        for i in range(len(st) - 2):
            a_, b_, c_ = (st[i], st[i + 1], st[i + 2]) if i % 2 == 0 else (st[i + 1], st[i], st[i + 2])
            lst += [a_, b_, c_]
    print(f"strips: {len(long_strips)} long ({len(seq)} vertices) + {len(lst) // 3} list triangles -> {len(seq) + len(lst)} draw tuples "
          f"({(len(seq) + len(lst)) / len(tri):.2f} per triangle); unique tuples {len(tuple_list)}, uv entries {len(uv_list)}")

    # mesh object (reuse the original skin mesh's headers/material template)
    old = skin.mesh
    mat = bytearray(old.materials[0])
    mat[0x34:0x54] = cfg["body_tex"].encode().ljust(0x20, b"\0")
    mesh = M.Mesh(dict(old.header_extra), [tuple(map(float, p[:3])) + (1.0,) for p in local],
                  [tuple(map(float, n)) + (1.0,) for n in nrm], [(u, v, 1.0, 0.0) for u, v in uv_list], [],
                  [M.Group(4, 0, [(tuple_list[t][0], tuple_list[t][0], tuple_list[t][1]) for t in g]) for g in seq_groups]
                  + ([M.Group(3, 0, [(tuple_list[t][0], tuple_list[t][0], tuple_list[t][1]) for t in lst])] if lst else []), [bytes(mat)])
    mesh.pads = {}
    mesh.group_head = old.group_head
    mesh.stride = 3
    skin.mesh = mesh

    # remove rigid parts; shrink cloth to invisible
    invisible = 0
    for i, o in enumerate(mds.objects):
        if o.mesh is None or o.name == cfg["skin"] or o.name in cfg["keep_meshes"]:
            continue
        if o.name.startswith("cloth"):
            o.mesh.positions = [(x * 0.001, y * 0.001, z * 0.001, w) for x, y, z, w in o.mesh.positions]
            for k in range(len(o.mesh.materials)):
                m_ = bytearray(o.mesh.materials[k])
                m_[0x34:0x54] = (cfg["clear_tex"] or cfg["body_tex"]).encode().ljust(0x20, b"\0")
                o.mesh.materials[k] = bytes(m_)
            invisible += 1
        else:
            o.mesh = None
    print(f"rigid parts removed, {invisible} cloth meshes made invisible")

    # skin weights
    ref_pts, ref_w, bones = reference_samples(model_dir, exclude=tuple(cfg["keep_meshes"]))
    tmpl, _ = W.parse(pk.get(f"{stem}.wgt").data)
    have = {r.bone for r in tmpl}
    missing = [b for b in bones if b not in have]
    print("driving joints:", bones, "missing from the weight template:", missing)
    wts = transfer_weights(pos, tri, ref_pts, ref_w, bones, cloth_ref=reference_samples.cloth)
    vw = {v: {bones[j]: float(wts[v, j]) for j in range(len(bones)) if wts[v, j] > 0.4} for v in range(len(pos))}
    recs = W.build(tmpl, vw, min_percent=0.4)
    new_wgt = W.write(recs)
    print(f"weights: {sum(len(r.entries) for r in recs)} entries in {len(new_wgt)} bytes")

    # textures
    atlas = Image.open(Path(fit_dir) / "atlas.png").convert("RGB")
    atlas = inpaint_holes(atlas, low["uv"])
    atlas = brighten(atlas, low["uv"], cfg.get("target_lum", 100.0))
    idx, pal = quantize(atlas, atlas_size)
    body = tim2_8bit(idx, pal, atlas_size, atlas_size)
    img_name = next(e.name for e in pk.entries if e.name.endswith(".img") and "w_h" not in e.name)
    old_img = pk.get(img_name).data
    textures = [(cfg["body_tex"], body)]
    if cfg.get("full_img"):                                      # keep the original texture set, order and sizes; only the body texture's data is replaced
        textures = []
        for tname, tobj in I.textures(old_img):
            if tname == cfg["body_tex"]:
                textures.append((tname, body))
            elif cfg.get("full_img") == "tiny" and tname not in cfg["keep_textures"]:   # same names and order, but unused textures shrink to 64x64
                textures.append((tname, tim2_8bit(np.zeros((64, 64), np.uint8), np.zeros((256, 4), np.uint8), 64, 64)))
            else:
                textures.append((tname, old_img[tobj.pos:tobj.pos + 16 + tobj.total]))
    elif cfg["clear_tex"]:
        textures.append((cfg["clear_tex"], tim2_8bit(np.zeros((64, 64), np.uint8), np.zeros((256, 4), np.uint8), 64, 64)))
    for tname, tobj in ([] if cfg.get("full_img") else I.textures(old_img)):   # textures other models in the pack still use (e.g. Xiao's hand-up arms)
        if tname in cfg["keep_textures"]:
            textures.append((tname, old_img[tobj.pos:tobj.pos + 16 + tobj.total]))
    new_img = build_img(textures)

    cfg_text = pk.get("base.cfg").data.decode("shift-jis")
    lines, skip = [], False
    for ln in cfg_text.splitlines():
        if ln.startswith("TEX_ANIME ") or ln.startswith("TEX_ANIME_DATA"):
            skip = True
            continue
        if ln.startswith("TEX_ANIME_END"):
            skip = False
            continue
        if ln.startswith("CLOTH "):               # the cloth simulation would move the (shrunk) cloth meshes back out and draw them black
            continue
        lines.append(ln)
    new_cfg = "\r\n".join(lines).encode("shift-jis") if "\r\n" in cfg else "\n".join(lines).encode("shift-jis")

    pk.replace(f"{stem}.mds", M.dump(mds))
    pk.replace(f"{stem}.wgt", new_wgt)
    pk.replace(img_name, new_img)
    pk.replace("base.cfg", new_cfg)
    out = P.dump(pk)
    Path(out_pack).write_bytes(out)
    print(f"pack {len(out)} bytes -> {out_pack}")
    # HD replacement texture
    if hd_dir:
        import tim2 as T2
        import pcsx2_names as N
        t = next(t for n, t in I.textures(new_img) if n == cfg["body_tex"])
        fname = N.pcsx2_name(t) if hasattr(N, "pcsx2_name") else None
        print("PCSX2 replacement name:", fname)
        if fname:
            Path(hd_dir).mkdir(parents=True, exist_ok=True)
            atlas.save(Path(hd_dir) / (fname + ".png"))
    return out


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("fit_dir")
    ap.add_argument("--pack", default="dun/mainchara/c01d.chr")
    ap.add_argument("--model-dir", default=str(ROOT / "work/models/toan"))
    ap.add_argument("--out")
    ap.add_argument("--cfg", help="key in CONFIGS (default: the character name)")
    ap.add_argument("--hd-dir")
    a = ap.parse_args()
    build(a.name, a.pack, a.model_dir, a.fit_dir, a.out or Path(a.fit_dir) / f"{Path(a.pack).stem}.new.chr", hd_dir=a.hd_dir, cfg=CONFIGS[a.cfg or a.name])
