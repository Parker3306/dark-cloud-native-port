"""Georama "requests" (the per-town arrangement wishes of the villagers), re-implemented from
CEditGround::RequestCheck 0x1a5fe0 and NornRequest 0x1a69c0 / MatatagiRequest 0x1a6d90 / QueensRequest 0x1a73f0 /
MuskaRequest 0x1a7920 / YellowRequest 0x1a7f40 (see notes/georama_edit.md section 6).

The module models a town layout (list of placed parts) and evaluates the request bits with the same primitives as the game:
  rect_part(p, m)      GetRectParts(rect, part, margin)          0x1a62c0
  rect_cell(p, dx, dz) GetRectParts(rect, part, dx, dz)          0x1a6410
  rect_dir(p, d, k)    GetRectDirParts(rect, part, dir, depth)   0x1a6750
  has(rect, type, a)   CheckPartsRect(type, area, rect)          0x1a61e0
  check_rot(a, b, k)   CheckRot(a, b, k)                         0x1a7890
usage: georama_requests.py [--all]   evaluates the developer layouts data/gedit/e0N/gdata0.edt (complete towns) and prints the
                                     request bits and how many of them hold
"""
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mapinfo           # noqa: E402
import pts as dcpts      # noqa: E402

GEDIT = HERE.parent.parent / 'data' / 'gedit'
KINDS = {'BLD_PARTS': 0, 'GRD_PARTS': 0, 'ROAD_PARTS': 1, 'RIVER_PARTS': 2, 'BRIDGE_PARTS': 3, 'LAKE_PARTS': 4, 'ON_RIVER_PARTS': 5}


def rot_footprint(grid, w0, h0, rot):
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


class Part:
    def __init__(self, slot, ptype, kind, area, cx, cz, rot, w0, h0, grid):
        self.slot, self.type, self.kind, self.area, self.cx, self.cz, self.rot = slot, ptype, kind, area, cx, cz, rot
        self.w0, self.h0, self.grid = w0, h0, grid

    def size(self, rot=None):
        r = self.rot if rot is None else rot
        return (self.h0, self.w0) if r & 1 else (self.w0, self.h0)

    @property
    def w(self):
        return self.size()[0]

    @property
    def h(self):
        return self.size()[1]


class Town:
    """cells[area][(x, z)] = Part for every footprint cell with code >= 0x80; alt[area][(x,z)] = altitude steps (digit cells)."""

    def __init__(self, town_no):
        self.no = town_no
        self.dir = GEDIT / ('e0%d' % (town_no + 1))
        self.mi = mapinfo.load(self.dir)
        self.scn = dcpts.open_scn(self.dir / 'scene.scn')
        self.areas = self.mi.editareas
        self.parts = []
        self.cells = {a: {} for a in self.areas}
        self.alt = {a: {} for a in self.areas}
        self.total = 0

    # -- footprint data of a part type
    def footprint(self, ptype):
        p = self.mi.parts[ptype]
        if p.info:
            w0, h0 = p.info['width'], p.info['height']
            grid = bytes(0x80 if ch == 'x' else 0x81 if ch == 'd' else int(ch) for ch in p.info['grid'].replace('|', ''))
        else:
            nm = os.path.basename(p.file)[:-4] if p.file.endswith('.pts') else os.path.basename(p.file)
            info = self.scn.parts[nm].info
            w0, h0, grid = info.width, info.height, info.grid
        return w0, h0, grid, KINDS[p.directive]

    def place(self, ptype, rot, x, z):
        w0, h0, grid, kind = self.footprint(ptype)
        px, pz = x + 1.0, z + 1.0
        for k in sorted(self.areas):
            a = self.areas[k]
            ox, oy, oz = a['origin']
            ox -= 0.01 * a['width']
            oz -= 0.01 * a['height']
            if ox <= px <= ox + a['unit'] * a['width'] and oz <= pz <= oz + a['unit'] * a['height']:
                cx = int((px - ox + 0.1) / a['unit'])
                cz = int((pz - oz + 0.1) / a['unit'])
                part = Part(len(self.parts), ptype, kind, k, cx, cz, rot, w0, h0, grid)
                w, h, fp = rot_footprint(grid, w0, h0, rot)
                for i, j, c in fp:
                    if c == 0:
                        continue
                    gx, gz = cx + i - (w >> 1), cz + j - (h >> 1)
                    if c >= 0x80:
                        self.cells[k][(gx, gz)] = part
                    else:
                        self.alt[k][(gx, gz)] = self.alt[k].get((gx, gz), 0) + c
                self.parts.append(part)
                return part
        raise ValueError('outside every area')

    def load_edt(self, path):
        g = Path(path).read_bytes()
        cnt, size, off = struct.unpack_from('<3h', g, 0)
        recs = [struct.unpack_from('<hh3f', g, off + 16 * i) for i in range(cnt)]
        for pn, rot, x, y, z in recs:
            self.place(pn, rot, x, z)
        return self

    # -- helpers of the request code
    def first(self, ptype):
        for p in self.parts:
            if p.type == ptype:
                return p
        return None

    def kind_at(self, area, x, z):
        p = self.cells[area].get((x, z))
        return p.kind if p else -1

    def alt_at(self, part):
        return self.alt[part.area].get((part.cx, part.cz), 0)

    # GetRectParts(rect, part, margin) 0x1a62c0
    def rect_part(self, p, m):
        w, h = p.w, p.h
        return (p.cx - (w >> 1) - m, p.cz - (h >> 1) - m, w + 2 * m, h + 2 * m)

    # GetRectParts(rect, part, dx, dz) 0x1a6410
    def rect_cell(self, p, dx, dz):
        rot = p.rot
        W, H = p.size(0)
        dx -= W >> 1
        dz -= H >> 1
        ox = oz = 0
        pw, ph = W & 1, H & 1
        if pw == 1 and ph == 0:
            if rot == -1:
                ox = -1
            if rot == 2:
                oz = -1
        if pw == 0 and ph == 1:
            if rot == 1:
                oz = -1
            if rot == 2:
                ox = -1
        if pw == 0 and ph == 0:
            if rot == -1:
                ox = -1
            if rot == 1:
                oz = -1
            if rot == 2:
                ox = oz = -1
        if rot == -1:
            a, b = -dz, dx
        elif rot == 2:
            a, b = -dx, -dz
        elif rot == 1:
            a, b = dz, -dx
        else:
            a, b = dx, dz
        return (ox + p.cx + a, oz + p.cz + b, 1, 1)

    # GetRectDirParts(rect, part, dir, depth) 0x1a6750
    def rect_dir(self, p, d, depth):
        w, h = p.w, p.h
        u = d + p.rot
        if u > 2:
            u -= 4
        if u < -1:
            u += 4
        if not -2 < u < 3:
            return (0, 0, 0, 0)
        cxo, czo = w >> 1, h >> 1
        if u % 2 == 0:
            if u == 0:
                return (p.cx - cxo, p.cz - czo - depth, w, depth)
            return (p.cx - cxo, h + p.cz - czo, w, depth)
        if u == 1:
            return (p.cx - cxo - depth, p.cz - czo, depth, h)
        return (w + p.cx - cxo, p.cz - czo, depth, h)

    # GetPartsRect + CheckPartsRect 0x16fb50 / 0x1a61e0
    def parts_in_rect(self, area, rect):
        x0, z0, w, h = rect
        out = []
        for x in range(x0, x0 + w):
            for z in range(z0, z0 + h):
                p = self.cells.get(area, {}).get((x, z))
                if p is not None and p not in out:
                    out.append(p)
        return out

    def has(self, rect, ptype, area=0):
        if area not in self.areas:
            return False
        return any(p.type == ptype for p in self.parts_in_rect(area, rect))

    @staticmethod
    def check_rot(a, b, k=0):
        if a is None or b is None:
            return False
        r = a.rot + k
        if r > 2:
            r -= 4
        if r < -1:
            r += 4
        return r == b.rot

    def river_chain(self, area, x0, z0, x1, z1):
        seen = set()

        def rec(x, z):
            if not (0 <= x < 16 and 0 <= z < 16) or (x, z) in seen:
                return False
            seen.add((x, z))
            if self.kind_at(area, x, z) not in (2, 3, 5):
                return False
            if (x, z) == (x1, z1):
                return True
            return rec(x - 1, z) or rec(x + 1, z) or rec(x, z - 1) or rec(x, z + 1)
        return rec(x0, z0)


# ----------------------------------------------------------------------------------------------------------------------
def requests(t, gcompleted=lambda p: True, comp_event=lambda p: False, int_flag0=0):
    """Returns (total, set(request indices that are 1)) before the final "part must be complete" filter."""
    L = t.first
    R = set()
    n = t.no
    if n == 0:                                         # NornRequest
        total = 8
        if L(0) and L(0).rot == 1:
            R.add(0)
        if L(1) and t.has((10, 0, 4, 5), 1, 0):
            R.add(1)
        if L(2) and not t.has((2, 8, 9, 4), 2, 0):
            R.add(2)
        if L(3) and L(12) and t.has(t.rect_part(L(12), 2), 3, 0):
            R.add(3)
        if L(4) and L(6) and t.has(t.rect_part(L(6), 4), 4, 0):
            R.add(4)
        if L(5) and L(8) and t.has(t.rect_dir(L(8), 2, 4), 5, 0) and int_flag0 >= 100:
            R.add(5)
        if L(6) and L(1) and not t.has(t.rect_part(L(1), 4), 6, 0):
            R.add(6)
        if L(7):
            rect = t.rect_part(L(7), 4)
            cnt = sum(1 for p in t.parts_in_rect(L(7).area, rect) if p.type < 7)
            if cnt >= 2:
                R.add(7)
    elif n == 1:                                       # MatatagiRequest
        total = 10
        if all(a in t.areas for a in (0, 1, 2)):
            if t.river_chain(0, 5, 0, 2, 7) & t.river_chain(1, 3, 5, 11, 3) & t.river_chain(2, 4, 0, 3, 7):
                R.add(16)
            if L(0) and (t.has((0, 0, 4, 1), 0, 0) or t.has((0, 5, 1, 1), 0, 1)):
                R.add(0)
            if L(1):
                rect = t.rect_part(L(1), 1)
                if sum(1 for p in t.parts_in_rect(L(1).area, rect) if p.type == 15) > 3:
                    R.add(1)
            if L(2):
                rect = t.rect_part(L(2), 3)
                if any(t.has(rect, k, L(2).area) for k in (11, 12, 13)):
                    R.add(2)
            if L(3) and t.has(t.rect_part(L(3), 4), 14, L(3).area):
                R.add(3)
            if L(4) and t.has((0, 5, 7, 3), 4, 2):
                R.add(4)
            if L(5) and t.alt_at(L(5)) > 0:
                R.add(5)
            if L(6) and L(3) and t.has(t.rect_part(L(3), 3), 6, L(3).area):
                R.add(6)
            if L(7) and t.alt_at(L(7)) > 0:
                R.add(7)
            if L(14):
                rect = t.rect_part(L(14), 1)
                if sum(1 for p in t.parts_in_rect(L(14).area, rect) if p.type in (16, 17, 11, 12, 13)) > 13:
                    R.add(14)
    elif n == 2:                                       # QueensRequest
        total = 10
        if all(a in t.areas for a in (0, 1, 2)):
            if L(0) and (t.has((0, 0, 1, 7), 0, 0) or t.has((0, 0, 1, 8), 0, 2)):
                R.add(0)
            if L(1) and L(10) and t.has(t.rect_part(L(10), 3), 1, L(10).area):
                R.add(1)
            if L(2) and L(8) and t.has(t.rect_part(L(8), 2), 2, L(8).area):
                R.add(2)
            if L(3) and L(1) and t.has(t.rect_part(L(1), 2), 3, L(1).area):
                R.add(3)
            if L(4):
                if not L(9) or L(4).area != L(9).area:
                    R.add(4)
            if L(5) and L(5).rot == 0:
                R.add(5)
            if L(6) and L(11) and L(6).rot == -1 and t.has(t.rect_part(L(11), 3), 6, L(11).area):
                R.add(6)
            if L(7) and L(7).area == 0:
                R.add(7)
            if L(8) and t.has(t.rect_cell(L(8), 0, 3), 13, L(8).area):
                R.add(8)
            if L(9) and L(8) and t.has(t.rect_part(L(8), 1), 9, L(8).area):
                R.add(9)
    elif n == 3:                                       # MuskaRequest
        total = 9
        if 0 in t.areas:
            front = lambda p, ty: t.has(t.rect_dir(p, 2, 14), ty, 0)    # noqa: E731
            if L(0) and L(8) and t.has((1, 0, 6, 3), 0, 0) and front(L(0), 8) and t.check_rot(L(0), L(8), 1) and comp_event(0):
                R.add(0)
            if L(1) and L(10) and front(L(1), 10) and t.check_rot(L(1), L(10), 1):
                R.add(1)
            if L(2) and L(9) and front(L(2), 9) and t.check_rot(L(2), L(9), 0):
                R.add(2)
            if L(3) and L(9) and L(11) and t.has(t.rect_part(L(11), 2), 3, 0) and front(L(3), 9) and t.check_rot(L(3), L(9), 2):
                R.add(3)
            if L(4) and L(9) and front(L(4), 9) and t.check_rot(L(4), L(9), 1):
                R.add(4)
            if L(5) and L(10) and front(L(5), 10) and t.check_rot(L(5), L(10), 0):
                R.add(5)
            if L(6) and L(8) and front(L(6), 8) and t.check_rot(L(6), L(8), 0) and L(6).rot == 1:
                R.add(6)
            if L(7) and L(8) and front(L(7), 8) and t.check_rot(L(7), L(8), 2):
                R.add(7)
            if L(10) and t.has((2, 0, 3, 14), 10, 0) and L(10).rot == 0:
                R.add(10)
    elif n == 4:                                       # YellowRequest
        total = 13
        if 0 in t.areas:
            cr = t.check_rot
            if cr(L(0), L(7)) and t.has(t.rect_cell(L(0), 0, 2), 7) and t.has(t.rect_cell(L(0), 1, 2), 7):
                R.add(0)
            if cr(L(1), L(5)) and t.has(t.rect_cell(L(1), 0, -1), 5):
                R.add(1)
            if cr(L(2), L(6)) and t.has(t.rect_cell(L(2), 0, -1), 6):
                R.add(2)
            if cr(L(3), L(7)) and t.has(t.rect_cell(L(3), 2, 1), 7) and not t.has(t.rect_cell(L(3), 2, 0), 7):
                R.add(3)
            if cr(L(4), L(7)) and t.has(t.rect_cell(L(4), -1, 1), 7) and not t.has(t.rect_cell(L(4), -1, 0), 7):
                R.add(4)
            if cr(L(5), L(3)) and t.has(t.rect_cell(L(3), 1, 2), 5):
                R.add(5)
            if cr(L(6), L(4)) and t.has(t.rect_cell(L(4), 0, 2), 6):
                R.add(6)
            if L(7):
                R.add(7)
            if cr(L(8), L(7)) and t.has(t.rect_cell(L(8), 0, -1), 7) and t.has(t.rect_cell(L(8), 1, -1), 7):
                R.add(8)
            if cr(L(9), L(8)) and t.has(t.rect_cell(L(8), 0, 1), 9):
                R.add(9)
            if cr(L(10), L(8)) and t.has(t.rect_cell(L(8), 1, 1), 10):
                R.add(10)
            if cr(L(11), L(9)) and t.has(t.rect_cell(L(11), 0, -1), 9):
                R.add(11)
            if cr(L(12), L(10)) and t.has(t.rect_cell(L(12), 0, -1), 10):
                R.add(12)
    else:
        total = 0
    return total, R


def main():
    for n in range(5):
        d = GEDIT / ('e0%d' % (n + 1))
        t = Town(n).load_edt(d / 'gdata0.edt')
        total, R = requests(t, int_flag0=100, comp_event=lambda p: True)
        print('town %d (%s): %d parts placed, requests held %s of %d' % (n, d.name, len(t.parts), sorted(R), total))


if __name__ == '__main__':
    main()
