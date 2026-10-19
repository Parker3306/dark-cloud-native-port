"""Dark Cloud `.sne` camera/actor path files (data/opdat/scene/*.sne), used by the opening.

A .sne is an ordinary pack (tools/dc/pack.py, 0x50-byte entry headers) with three entries `<name>.cfg`, `<name>.mds`, `<name>.mot`:
  * .cfg  text: VERTEX_ANIME 0 / SHADOW_VERTEX_ANIME 0 / MODEL "<name>.mds" / MOTION 0,"<name>.mot","","" / KEY_START 0 / KEY 10,10,0.3 / MOTION_END
          (the KEY line is only the default motion 0 = frames 10..10; the opening script supplies the real range with CAMERA).
  * .mds  a tiny MDS (tools/dc/mds.py): object 0 = `dummyRoot`, then one object per actor placeholder named like the actor's model
          (`c09a`, `c11a`, `p17a1`..`p17a35`, `p19a`, `c01d`, ...; with a small box mesh; the node matrix holds the actor's FACING in its
          rotation part and the initial position in row 3) and, in some files, `cam` and `int` objects (unused by the game: the camera is
          driven by the camera tracks below).
  * .mot  the usual `.mot` record chain (0x20-byte header {object, param, type, unk=0x20, count, more} + count x 0x20-byte keys:
          u32 frame number at +0, vec4 at +0x10), see notes/native-plan.md.  Track types used:
            2      translation of MDS object `object` (x,y,z; the game writes it into the node's translation, rotation kept)
            30     camera position  (object 0)  vec = (x, y, z)
            31     camera target    (object 0)  vec = (x, y, z)
            33     camera field of view in degrees (vec.x) (object 0);  32 (roll, degrees) exists in the code but is not used by the opening files
          Key times are ABSOLUTE frame numbers: the opening script's `CAMERA key,start,end,speed` plays the window [start, end] of these keys.
Interpolation is linear between the two keys that bracket the (fractional) time.

usage: opening_sne.py <file.sne|name>        summary
       opening_sne.py <file.sne> t [t ...]   camera at frame t(s)
"""
import math
import os
import struct
import sys
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pack  # noqa: E402

SCENE_DIR = os.path.join(HERE, '..', '..', 'data', 'opdat', 'scene')

T_TRANS, T_CAMPOS, T_CAMREF, T_ROLL, T_FOV = 2, 30, 31, 32, 33


@dataclass
class Track:
    object: int
    param: int
    type: int
    keys: list          # [(frame, (x, y, z, w))]


@dataclass
class Node:
    index: int
    name: str
    parent: int
    matrix: tuple       # 16 floats, row-vector convention (row 3 = translation)
    has_mesh: bool


@dataclass
class Sne:
    name: str
    cfg: str
    nodes: list
    tracks: list
    by_name: dict = field(default_factory=dict)

    def camera_range(self):
        t = [x for x in self.tracks if x.type == T_CAMPOS]
        return (t[0].keys[0][0], t[0].keys[-1][0]) if t else None

    def _track(self, typ, obj=None):
        for t in self.tracks:
            if t.type == typ and (obj is None or t.object == obj):
                return t
        return None

    @staticmethod
    def _lerp(t, time):
        ks = t.keys
        if time <= ks[0][0]:
            return ks[0][1]
        if time >= ks[-1][0]:
            return ks[-1][1]
        lo, hi = 0, len(ks)
        while lo < hi:                      # first key with frame > time
            mid = (lo + hi) // 2
            if ks[mid][0] <= time:
                lo = mid + 1
            else:
                hi = mid
        a, b = ks[lo - 1], ks[lo]
        f = (time - a[0]) / (b[0] - a[0]) if b[0] != a[0] else 0.0
        return tuple(a[1][i] + (b[1][i] - a[1][i]) * f for i in range(4))

    def camera_at(self, time):
        """-> (position xyz, target xyz, fov degrees or None)"""
        p, r, f = self._track(T_CAMPOS), self._track(T_CAMREF), self._track(T_FOV)
        return (self._lerp(p, time)[:3] if p else None, self._lerp(r, time)[:3] if r else None,
                self._lerp(f, time)[0] if f else None)

    def node_pos_at(self, name, time):
        """translation of the placeholder node `name` (actor position) at `time`; None if the node has no track."""
        n = self.by_name.get(name)
        if n is None:
            return None
        t = self._track(T_TRANS, n.index)
        return self._lerp(t, time)[:3] if t else tuple(n.matrix[12:15])

    def node_yaw(self, name):
        """facing used by the game: atan2(m[8], m[10]) of the node's world matrix (row 2 = local Z axis) - rotation is constant."""
        n = self.by_name.get(name)
        return math.atan2(n.matrix[8], n.matrix[10]) if n else None


def parse_mot(d):
    out, pos = [], 0
    while pos + 0x20 <= len(d):
        obj, param, typ, unk, count, more = struct.unpack_from('<6I', d, pos)
        pos += 0x20
        keys = []
        for _ in range(count):
            t = struct.unpack_from('<I', d, pos)[0]
            keys.append((t, struct.unpack_from('<4f', d, pos + 0x10)))
            pos += 0x20
        out.append(Track(obj, param, typ, keys))
        if not more:
            break
    return out


def parse_nodes(m):
    if len(m) < 16 or m[:4] != b'MDS\0':
        return []
    n = struct.unpack_from('<I', m, 8)[0]
    nodes = []
    for i in range(n):
        o = 0x10 + i * 0x70
        name = m[o + 8:o + 0x28].split(b'\0')[0].decode('latin1')
        moff, par = struct.unpack_from('<Ii', m, o + 0x28)
        nodes.append(Node(i, name, par, struct.unpack_from('<16f', m, o + 0x30), moff != 0))
    return nodes


def load_sne(path):
    if not os.path.exists(path):
        path = os.path.join(SCENE_DIR, path if path.endswith('.sne') else path + '.sne')
    p = pack.parse(open(path, 'rb').read())
    cfg = next((e for e in p.entries if e.name.endswith('.cfg')), None)
    mds = next((e for e in p.entries if e.name.endswith('.mds')), None)
    mot = next((e for e in p.entries if e.name.endswith('.mot')), None)
    nodes = parse_nodes(mds.data) if mds else []
    s = Sne(os.path.basename(path), cfg.data.decode('cp932', 'replace') if cfg else '', nodes, parse_mot(mot.data) if mot else [])
    s.by_name = {n.name: n for n in nodes}
    return s


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    s = load_sne(a[0])
    if len(a) > 1:
        for t in a[1:]:
            print(t, s.camera_at(float(t)))
        return
    print(s.name, 'camera range', s.camera_range())
    print('nodes:', [(n.name, n.parent) for n in s.nodes][:40])
    kinds = {}
    for t in s.tracks:
        kinds.setdefault(t.type, []).append((t.object, len(t.keys)))
    for k, v in sorted(kinds.items()):
        print('  track type', k, 'objects/keys', v[:6], '...' if len(v) > 6 else '')


if __name__ == '__main__':
    main()
