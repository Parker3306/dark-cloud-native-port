"""Re-draw the in-dungeon HUD (topStatusInfo @0x1b04f0, BtStatusErrDraw, the attack gauge of MainDraw) from the game's own textures,
using the rectangles read from the decompile.  An executable specification of notes/menus.md section A.

Usage: python -I tools/dc/hud_mock.py out.png [--life 80/100] [--dur 20/31] [--water 18/30] [--exp 5/20] [--items 148:3,145:12,0:0]
       [--sel 1] [--floor 5] [--boss] [--status 4,16] [--gauge 55] [--weapon 258] [--unit 0] [--map] [--crystal] [--ura] [--bg file.png]
Textures: commenu/a_usa/itempack.img (+ item/weapon icons copied into it like DngActiveItemTextureCopy), stayframe.img, dun/pack/teximg.pac:wepready.
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent.parent / "scripts"))
import img as imgmod  # noqa: E402
import mem  # noqa: E402
import pack as packmod  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

D = HERE.parent.parent / "data"


def tex(path, name=None):
    for n, t in imgmod.textures((D / path).read_bytes()):
        if name is None or n == name:
            return t.decode()
    raise KeyError(name)


def pak_tex(path, entry, name):
    p = packmod.parse((D / path).read_bytes())
    for n, t in imgmod.textures(p.get(entry).data):
        if n == name:
            return t.decode()
    raise KeyError(name)


class Screen:
    def __init__(self, bg=None):
        self.im = Image.new("RGBA", (640, 448), (24, 40, 24, 255))
        if bg:
            self.im.alpha_composite(Image.open(bg).convert("RGBA").resize((640, 448)))

    def sprite(self, t, dst, src, alpha=0x80):
        """set2DSprite(tex, dst rect, src rect, alpha): the game scales src to dst; alpha 0x80 = opaque."""
        sx, sy, sw, sh = src
        if sw <= 0 or sh <= 0 or dst[2] <= 0 or dst[3] <= 0:
            return
        piece = t.crop((sx, sy, sx + sw, sy + sh)).resize((dst[2], dst[3]), Image.NEAREST)
        a = piece.getchannel("A").point(lambda v: int(v * min(alpha & 0xff, 0x80) / 0x80))
        piece.putalpha(a)
        self.im.alpha_composite(piece, (dst[0], dst[1]))

    def gradient(self, rect, c_left, c_right):
        """set2DSpriteC4: horizontal gradient (the four vertex colours are left, right, left, right)."""
        x, y, w, h = rect
        if w <= 0:
            return
        d = ImageDraw.Draw(self.im)
        for i in range(w):
            f = i / max(1, w - 1)
            c = tuple(int(c_left[k] + (c_right[k] - c_left[k]) * f) for k in range(3))
            d.line([(x + i, y), (x + i, y + h - 1)], fill=c + (255,))


# BtStatusAlarmInit colours (left, right): life, weapon, 15..20 %, <= 15 %
PAL = {"life": ((0x40, 0xc1, 0x82), (0x00, 0x81, 0xff)), "weapon": ((0xff, 0x80, 0x00), (0xff, 0x20, 0x00)),
       "30": ((0xff, 0x00, 0xc6), (0x3f, 0x00, 0x31)), "15": ((0xff, 0x00, 0x00), (0x3f, 0x00, 0x00))}


def pal(max_, cur, weapon=False):
    if 0.2 * max_ < cur:
        return PAL["weapon" if weapon else "life"]
    if 0.3 * max_ < cur or cur <= 0.15 * max_:
        return PAL["15"]
    return PAL["30"]


class Hud:
    def __init__(self, scr):
        self.s = scr
        self.ip = tex("commenu/a_usa/itempack.img")
        self.sf = tex("commenu/a_usa/stayframe.img")
        self.icons = tex("commenu/a_usa/itemlst.img", "itemicon")
        self.wicons = tex("commenu/a_usa/itemlst.img", "wepicon")
        self.wr = pak_tex("dun/pack/teximg.pac", "wepready.img", "wepready")
        info = mem.read(0x27DE50, 2368)

        def icon_cell(i):
            idx = i + 0xaf if i <= 0x50 else i - 0x51
            return struct.unpack_from("<h", info, idx * 8 + 4)[0]
        self.icon_cell = icon_cell

    def cell(self, sheet, c):
        return sheet.crop(((c & 7) * 32, (c >> 3) * 32, (c & 7) * 32 + 32, (c >> 3) * 32 + 32))

    def digits(self, x, y, v, row=0):
        """ValuePrint @0x1b0060: 12x12 digits from stayframe row 0xb0 (+0xc*row), at least 2 digits (zero padded); returns the digit count."""
        v = int(v)
        s = [int(c) for c in ("%03d" % v if v >= 100 else "%02d" % v)]       # ValuePrint: hundreds only if >= 100, tens and units always
        for i, d in enumerate(s):
            self.s.sprite(self.sf, (x + 12 * i, y, 12, 12), (d * 12, 0xb0 + row * 12, 12, 12))
        return len(s)

    def bar(self, y, n_seg, alpha):
        ip, S = self.ip, self.s
        S.sprite(ip, (0x30, y, 4, 0x10), (0x68, 0x70, 4, 0x10), alpha)
        for i in range(n_seg - 2):
            S.sprite(ip, (i * 4 + 0x34, y, 4, 0x10), (0x6c, 0x70, 4, 0x10), alpha)
        xr = (n_seg - 1) * 4
        S.sprite(ip, (xr + 0x30, y, 4, 0x10), (0x70, 0x70, 4, 0x10), alpha)
        return xr

    def draw(self, a):
        S, ip, sf = self.s, self.ip, self.sf
        y = a.rogoY3 - 10
        al = a.alarm
        # --- life
        cur, mx = a.life
        n = max(2, int(0.74 * mx))
        seg = max(2, (n >> 2) + 1)
        alpha = 0x80 if 0.2 * mx < cur else al
        xr = self.bar(y, seg, alpha)
        if cur > 0:
            S.gradient((0x34, y + 5, int(0.74 * cur) - 2, 6), *pal(mx, cur))
        x = xr + 0x38
        nd = self.digits(x, y + 7, cur)
        x += nd * 10
        S.sprite(sf, (x, y + 7, 12, 12), (0x78, 0xb0, 12, 12))
        self.digits(x + 10, y + 7, mx)
        S.sprite(ip, (0x20, y, 16, 16), (0x40, 0xa0, 16, 16), alpha)
        # --- weapon durability
        mxd, curd = a.dur
        f = 1.49495 * mxd
        n = max(2, int(f))
        seg = max(2, (n >> 2) + 2)
        alpha = 0x80 if 0.2 * mxd < curd else al
        xr = self.bar(y + 0x11, seg, alpha)
        if curd > 0:
            S.gradient((0x34, y + 0x16, int(1.49495 * curd) - 2, 6), *pal(mxd, curd, True))
        # exp line: MGFillBox in 640x224 field space (1 field line = 2 screen lines), y = (y+0x1f)>>1 field lines
        fy = ((y + 0x1f) >> 1) * 2
        wlen = int(f) - 1
        d = ImageDraw.Draw(S.im)
        d.rectangle([52, fy, 52 + wlen - 1, fy + 1], fill=(0x40, 0x40, 0x40, 255))
        if a.exp[0] > 0:
            e = int(a.exp[0] * (int(f) / a.exp[1])) - 1
            d.rectangle([52, fy, 52 + e - 1, fy + 1], fill=(0, 0xa2, 0xff, 255))
        x = xr + 0x38
        c = int(curd) + (1 if curd - int(curd) > 0 else 0)
        nd = self.digits(x, y + 0x18, c)
        x += nd * 10
        S.sprite(sf, (x, y + 0x18, 12, 12), (0x78, 0xb0, 12, 12))
        self.digits(x + 10, y + 0x18, mxd)
        S.sprite(ip, (0x20, y + 0x10, 16, 16), (0x40, 0xb0, 16, 16), alpha)
        # --- water
        wmax, wcur = a.water
        wal = al if wcur <= 0.15 * wmax else 0x80
        S.sprite(ip, (0x20, y + 0x22, 0x12, 0x14), (0x64, 0x84, 0x12, 0x14), wal)
        for i in range(int(wmax) // 10):
            S.sprite(ip, (i * 0x12 + 0x32, y + 0x22, 0x12, 0x14), (0x64, 0x98, 0x12, 0x14), wal)
        if int(wmax) % 10:
            S.sprite(ip, ((int(wmax) // 10) * 0x12 + 0x32, y + 0x22, 0x12, 0x14), (0x64, 0xac, 0x18, 0x14), wal)
        for i in range(int(wcur) // 10):
            S.sprite(ip, (i * 0x12 + 0x33, y + 0x22, 0x10, 0x14), (0, 0x48, 0x10, 0x14), 0x50)
        if int(wcur) % 10:
            k = int((int(wcur) % 10) / 2.5)
            S.sprite(ip, ((int(wcur) // 10) * 0x12 + 0x33, y + 0x22, 0x10, 0x14), ((3 - k) * 0x10, 0x48, 0x10, 0x14), 0x50)
        # --- quick items: the game first copies the 32x32 icon cells into itempack at (32 + 32 * slot, 0)
        for i, (iid, cnt) in enumerate(a.items):
            if iid > 0x83:
                ip.alpha_composite(self.cell(self.icons, self.icon_cell(iid)), (0x20 + i * 0x20, 0))
        for i in range(3):
            xx = i * 0x28
            S.sprite(ip, (xx + 0x126, y + 2, 0x24, 0x24), (0x24, 0x5c, 0x24, 0x24))
            iid, cnt = a.items[i]
            if iid not in (-1, 0):
                S.sprite(ip, (xx + 0x128, y + 4, 0x1f, 0x1f), (i * 0x20 + 0x20, 0, 0x1f, 0x1f))
                if cnt > 1:
                    if cnt // 10 > 0:
                        S.sprite(sf, (xx + 0x12f, y + 0x17, 12, 12), ((cnt // 10) * 12, 0xd4, 12, 12))
                    S.sprite(sf, (xx + 0x13b, y + 0x17, 12, 12), ((cnt % 10) * 12, 0xd4, 12, 12))
        # --- floor plaque, map / crystal icons
        S.sprite(ip, (0x1fc, y, 0x66, 0x29), (0x9a, 1, 0x66, 0x29))
        if a.ura:
            S.sprite(ip, (0x23c, y, 0x26, 0x29), (0xda, 0x2b, 0x26, 0x29))
        if a.crystal:
            S.sprite(ip, (0x1fe, y + 5, 0x20, 0x20), (0, 0xa0, 0x20, 0x20))
        if a.map:
            S.sprite(ip, (0x21e, y + 5, 0x20, 0x20), (0x20, 0xa0, 0x20, 0x20))
        fl = a.floor
        if a.boss:
            S.sprite(sf, (0x23c, y + 0x12, 0x26, 0x11), (0x78, 0x9e, 0x26, 0x12))
        else:
            base = 0x23f if fl < 10 else 0x245
            if fl > 9:
                S.sprite(sf, (base - 4, y + 0x12, 0xe, 0x11), ((fl // 10) * 12, 0x9e, 12, 0x12))
            S.sprite(sf, (base + 9, y + 0x12, 0xe, 0x11), ((fl % 10) * 12, 0x9e, 12, 0x12))
        # --- item cursor (itemNowSel 1..3)
        px = a.sel * 0x28
        for (dx, dy, sx, sy) in ((0xfc, 0, 0x4a, 0x48), (0x113, 0, 0x5a, 0x48), (0xfc, 0x16, 0x4a, 0x58), (0x113, 0x16, 0x5a, 0x58)):
            S.sprite(sf, (px + dx, y + dy, 16, 16), (sx, sy, 16, 16))
        # --- attack gauge, bottom left (alpha follows the slide-in)
        ga = (a.rogoY3 + 0x60) & 0xff
        g = a.gauge
        if g >= 100:
            S.sprite(self.wr, (0x28, 0x18c, 0x80, 0x1c), (0, 0, 0x80, 0x1c), ga)
        else:
            S.sprite(self.wr, (0x28, 0x18c, 0x80, 0x1c), (0, 0x54, 0x80, 0x1c), ga)
            S.sprite(self.wr, (0x42, 0x18c, int(g), 0x1c), (0x1a, 0x38, int(g), 0x1c), ga)
        S.sprite(ip, (0x18, 0x18c, 0x2a, 0x1d), (0x78, 0x37, 0x2a, 0x1d), ga)
        S.sprite(ip, (0x45, 0x188, 0x2b, 0x10), (0xa2, 0x37, 0x2b, 0x10), ga)
        wid = a.weapon
        defw = (257, 299, 314, 331, 347, 363)[a.unit]
        sx, sy = 0, 0
        if wid == defw + 1:
            sy = 0x20
        if wid == defw:
            sx, sy = 0x20, 0x20
        if wid > 0:
            ip.alpha_composite(self.cell(self.wicons, self.icon_cell(wid)), (sx, sy))
        S.sprite(ip, (0x1d, 0x184, 0x20, 0x20), (sx, sy, 0x20, 0x20), ga)
        # --- status ailment tags (BtStatusErrDraw): bit -> (col, row) in itempack at (0x84 + col*0x3e, 0x54 + row*0x24), drawn at (0x1ae, y)
        for bit, (cx, cy) in ((0x40, (0, 2)), (0x20, (1, 2)), (0x10, (1, 1)), (8, (0, 1)), (4, (1, 0))):
            if a.status & bit:
                S.sprite(ip, (0x1ae, y, 0x3e, 0x23), (0x84 + cx * 0x3e, 0x54 + cy * 0x24, 0x3e, 0x24))


def pair(s):
    a, b = s.split("/")
    return float(a), float(b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--life", default="80/100")
    ap.add_argument("--dur", default="20/31")
    ap.add_argument("--water", default="18/30")
    ap.add_argument("--exp", default="5/20")
    ap.add_argument("--items", default="148:3,145:12,0:0")
    ap.add_argument("--sel", type=int, default=1)
    ap.add_argument("--floor", type=int, default=5)
    ap.add_argument("--boss", action="store_true")
    ap.add_argument("--status", default="0")
    ap.add_argument("--gauge", type=float, default=55)
    ap.add_argument("--weapon", type=int, default=258)
    ap.add_argument("--unit", type=int, default=0)
    ap.add_argument("--map", action="store_true")
    ap.add_argument("--crystal", action="store_true")
    ap.add_argument("--ura", action="store_true")
    ap.add_argument("--bg")
    ap.add_argument("--rogo", type=int, default=0x20)
    a = ap.parse_args()
    a.life = pair(a.life)
    d = pair(a.dur)
    a.dur = (d[1], d[0])      # given as cur/max, the code wants (max, cur)
    w = pair(a.water)
    a.water = (w[1], w[0])
    a.exp = pair(a.exp)
    a.items = [(int(i.split(":")[0]), int(i.split(":")[1])) for i in a.items.split(",")]
    a.status = sum(int(v, 0) for v in a.status.split(","))
    a.rogoY3 = a.rogo
    a.alarm = 0x80
    scr = Screen(a.bg)
    Hud(scr).draw(a)
    scr.im.convert("RGB").save(a.out)
    print("saved", a.out)


if __name__ == "__main__":
    main()
