"""Dump the item / attach / weapon data tables of the game as CSV (work/tables/*.csv) with names.

Tables (main ELF data segment): ComItemInfo 0x27DE50 (8 bytes per id: u8 type [0 attach, 1 item, 2 weapon], +2 index into the type's table,
+4 icon cell, +6 message number), ITEM_LIST 0x27D0A0 (0x14 bytes), AttachList 0x27CA60 (0x20), WeaponList 0x27A6C0 (0x4c).
id -> ComItemInfo row: id <= 0x50: id + 0xaf, else id - 0x51 (ItemDb::info of native/dccore/src/items.cpp).
Names come from meswin/_systeme.bin (message = ComItemInfo+6 + 100).   usage: dump_items.py [outdir]
"""
import csv
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mem  # noqa: E402
import menu_mes  # noqa: E402

ROOT = os.path.join(HERE, '..', '..')
CHARS = ['Toan', 'Xiao', 'Goro', 'Ruby', 'Ungaga', 'Osmond']
ELEM = ['fire', 'ice', 'thunder', 'wind', 'holy']
SLAY = ['dragon', 'undead', 'marine', 'rock', 'plant', 'beast', 'sky', 'metal', 'mimic', 'mage']


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'work', 'tables')
    os.makedirs(out, exist_ok=True)
    sysmes = menu_mes.load_bytes(open(os.path.join(ROOT, 'data', 'meswin', '_systeme.bin'), 'rb').read())
    info = mem.read(0x27DE50, 2368)
    items = mem.read(0x27D0A0, 3500)
    attach = mem.read(0x27CA60, 1600)
    weapons = mem.read(0x27A6C0, 9120)

    def row(i):
        idx = i + 0xaf if i <= 0x50 else i - 0x51
        if (idx + 1) * 8 > len(info):
            return None
        t = info[idx * 8]
        return t, struct.unpack_from('<h', info, idx * 8 + 2)[0], struct.unpack_from('<h', info, idx * 8 + 4)[0], struct.unpack_from('<h', info, idx * 8 + 6)[0]

    menu = menu_mes.load_bytes(__import__('pack').parse(open(os.path.join(ROOT, 'data', 'commenu', 'a_usa', 'dungeon', 'dunmenu5.pak'), 'rb').read()).get('allmenu.mes').data)

    def name(i):
        n = sysmes_name(i)
        if not n:   # attach items have no system text: first quoted word of the description (allmenu.mes id + 500)
            d = menu.get(i + 500, '')
            if '"' in d:
                n = d.split('"')[1]
        return n

    def sysmes_name(i):
        r = row(i)
        m = (r[3] + 100) if r else i + 100      # message number = ComItemInfo+6 + 100 (GetWeaponMsgNo / item names)
        return sysmes.get(m, '').replace('\n', ' ')

    wi = csv.writer(open(os.path.join(out, 'items.csv'), 'w', newline=''))
    wi.writerow(['id', 'hex', 'name', 'icon_cell', 'msg', 'flags_u32@4', 'flags_u16@8', 'val1@a', 'val2@c', 'kind@10', 'raw'])
    wa = csv.writer(open(os.path.join(out, 'attach.csv'), 'w', newline=''))
    wa.writerow(['id', 'hex', 'name', 'icon_cell', 's2', 's4', 's6', 'attack', 'endurance', 'speed', 'magic'] + ELEM + SLAY + ['raw'])
    ww = csv.writer(open(os.path.join(out, 'weapons.csv'), 'w', newline=''))
    ww.writerow(['id', 'hex', 'name', 'char', 'max_whp', 'attack', 'endurance', 'speed', 'magic', 'holes(6)', 'hole_types'] + ELEM + SLAY +
                ['exp_base', 'exp_per_level', 'flags@38', 'buildup_mask1@3c', 'buildup_mask2@40', 'attack_cap', 'magic_cap', 'icon_cell', 'msg'])
    for i in range(1, 0x180):
        r = row(i)
        if not r or r[0] > 2:
            continue
        t, idx, icon, msg = r
        if t == 1 and 0 <= idx and (idx + 1) * 0x14 <= len(items):
            e = items[idx * 0x14:(idx + 1) * 0x14]
            wi.writerow([i, hex(i), name(i), icon, msg, hex(struct.unpack_from('<I', e, 4)[0]), hex(struct.unpack_from('<H', e, 8)[0]),
                         struct.unpack_from('<h', e, 10)[0], struct.unpack_from('<h', e, 12)[0], struct.unpack_from('<i', e, 0x10)[0], e.hex()])
        elif t == 0 and 0 <= idx and (idx + 1) * 0x20 <= len(attach):
            e = attach[idx * 0x20:(idx + 1) * 0x20]
            wa.writerow([i, hex(i), name(i), icon] + list(struct.unpack_from('<3h', e, 2)) + list(struct.unpack_from('<4h', e, 8)) +
                        list(struct.unpack_from('<5b', e, 0x10)) + list(struct.unpack_from('<10b', e, 0x15)) + [e.hex()])
        elif t == 2 and i >= 0x101 and 0 <= idx and (idx + 1) * 0x4c <= len(weapons):
            e = weapons[idx * 0x4c:(idx + 1) * 0x4c]
            holes = [b for b in e[0x0b:0x11]]
            ww.writerow([i, hex(i), name(i), CHARS[e[10]] if e[10] < 6 else e[10], struct.unpack_from('<h', e, 0)[0], struct.unpack_from('<h', e, 2)[0],
                         struct.unpack_from('<h', e, 4)[0], struct.unpack_from('<h', e, 6)[0], struct.unpack_from('<h', e, 8)[0],
                         sum(1 for h in holes if h > 0), ''.join(str(h) for h in holes)] +
                        [struct.unpack_from('<h', e, 0x12 + 2 * k)[0] for k in range(5)] + [struct.unpack_from('<h', e, 0x1c + 2 * k)[0] for k in range(10)] +
                        [struct.unpack_from('<b', e, 0x30)[0], struct.unpack_from('<h', e, 0x32)[0], hex(struct.unpack_from('<H', e, 0x38)[0]),
                         hex(struct.unpack_from('<I', e, 0x3c)[0]), hex(struct.unpack_from('<I', e, 0x40)[0]), struct.unpack_from('<h', e, 0x44)[0],
                         struct.unpack_from('<h', e, 0x46)[0], icon, msg])
    print('written', out)


if __name__ == '__main__':
    main()
