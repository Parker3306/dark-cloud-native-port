"""Static Georama edit tables of the main ELF (see notes/georama_edit.md):
  EditPartsData   0x2540D0  6 towns x 24 parts x 0xbc   GetEditAtraPartsData 0x158e00 / CEditPartsInfo::Initialize 0x19a570
  EditElementData 0x25AF00  6 towns x 100 x 12          GetEditAtraData 0x158d80 (part names 0..39, tips 40..79, persons 80..99)
  EditCharaData   0x267070  5 towns x 20 x 0x14         villager -> (part, element) table copied into VILLAGER_INFO by LoadEditMapData
  AtraAppearData  0x25DF30  6 pointers -> {atra id, floor spec, count} lists (dungeon Atla distribution, BtAtraListMake 0x1c09c0)
usage: georama_edit_tbl.py [town]          EditPartsData of one/all towns
       georama_edit_tbl.py --join          EditPartsData joined with mapinfo.cfg parts and PTS info (mismatches flagged)
       georama_edit_tbl.py --elem|--chara|--atra   the other tables
       georama_edit_tbl.py --json          everything as JSON on stdout"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdata import Elf

BASE = 0x2540D0


def cstr(e, va):
    if va == 0:
        return None
    try:
        d = e.read(va, 64)
    except KeyError:
        return '<bad %x>' % va
    return d.split(b'\0')[0].decode('cp932', 'replace')


def entry(e, town, part):
    va = BASE + town * 0x125c + part * 0xbc
    w = struct.unpack('<47i', e.read(va, 0xbc))
    d = {'va': va, 'w0': w[0], 'w1': w[1], 'w2': w[2], 'w3': w[3], 'w4': w[4], 'elems': []}
    for k in range(6):
        b = 5 + 7 * k
        d['elems'].append({'id': w[b], 'w1': w[b + 1], 'names': [cstr(e, x & 0xffffffff) for x in w[b + 2:b + 6]], 'w6': w[b + 6]})
    return d


def elem_table(e, town):
    out = []
    for i in range(100):
        a, b, c = struct.unpack('<3i', e.read(0x25AF00 + town * 0x4b0 + i * 12, 12))
        out.append({'id': i, 'tile': a, 'msg': b, 'copies': c})
    return out


def chara_table(e, town):
    out = []
    for i in range(20):
        b = e.read(0x267070 + town * 400 + i * 0x14, 0x14)
        nm, = struct.unpack('<I', b[:4])
        sh = struct.unpack('<8h', b[4:20])
        out.append({'villager': i, 'model': cstr(e, nm), 'part': sh[0], 'element': sh[1], 'moves': sh[2],
                    'speed': struct.unpack('<f', b[12:16])[0], 'hide_until_event': sh[6]})
    return out


def atra_table(e, dungeon):
    ptr, = struct.unpack('<I', e.read(0x25DF30 + dungeon * 4, 4))
    rows, i = [], 0
    while True:
        a, f, c = struct.unpack('<3i', e.read(ptr + 12 * i, 12))
        if a == -1:
            break
        rows.append({'atra': a, 'floor': f, 'count': c})
        i += 1
    return rows


def atra_messages(lang='a_usa'):
    """English (or other `commenu/<lang>`) bank atrames.bin of emenu.pak: part / tip / person names and descriptions."""
    import pack, mesdump
    pk = pack.parse(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'data', 'commenu', lang, 'emenu.pak'), 'rb').read())
    b = pk.get('atrames.bin').data
    n = len(b) // 2
    sh = struct.unpack('<%dH' % n, b[:n * 2])
    table = sh[0]
    out = {}
    for i in range(table // 2):
        ident, off = sh[2 + 2 * i], sh[3 + 2 * i]
        q = table + 1 + off
        s = []
        while q < n and sh[q] != 0xff01:
            s.append(mesdump.glyph(sh[q]))
            q += 1
        out.setdefault(ident, ''.join(s))
    return out


def names_report():
    e = Elf()
    m = atra_messages()

    def nm(i):
        t = m.get(i, '')
        return t.split(chr(10))[0].strip('"') if t else '?'
    for t in range(5):
        el = elem_table(e, t)
        print('== town', t)
        for p in range(24):
            d = entry(e, t, p)
            if d['w0'] == 0:
                continue
            msg = el[p]['msg']
            print(' part %2d %-20s max=%d trig=%d' % (p, nm(1000 + t * 200 + d['w3']), d['w0'], d['w4']))
            for k, x in enumerate(d['elems']):
                if x['id'] < 0:
                    continue
                tip = el[x['id'] + 40]
                base = 1000 + t * 200 + tip['msg'] + (0x28 if x['id'] >= 40 else 0) + 0x28
                print('      elem %d id=%2d %-18s rel=%s npc_req=%s objs=%s' % (k, x['id'], nm(base), x['w1'], x['w6'], [n for n in x['names'] if n]))


def main():
    if '--json' in sys.argv:
        import json
        e = Elf()
        out = {'parts': {t: [entry(e, t, p) for p in range(24)] for t in range(6)},
               'elements': {t: elem_table(e, t) for t in range(6)},
               'chara': {t: chara_table(e, t) for t in range(5)},
               'atra': {d: atra_table(e, d) for d in range(6)}}
        print(json.dumps(out, indent=1))
        return
    if '--join' in sys.argv:
        for t in range(5):
            report(t)
        return
    if '--elem' in sys.argv:
        e = Elf()
        for t in range(6):
            print('town', t, [(r['id'], r['tile'], r['msg'], r['copies']) for r in elem_table(e, t) if r['tile'] >= 0])
        return
    if '--chara' in sys.argv:
        e = Elf()
        for t in range(5):
            print('town', t)
            for r in chara_table(e, t):
                if r['model']:
                    print('  ', r)
        return
    if '--names' in sys.argv:
        names_report()
        return
    if '--atra' in sys.argv:
        e = Elf()
        for d in range(6):
            print('dungeon', d, atra_table(e, d))
        return
    e = Elf()
    towns = [int(sys.argv[1])] if len(sys.argv) > 1 else range(6)
    for t in towns:
        print('== town', t)
        for p in range(24):
            d = entry(e, t, p)
            els = [(x['id'], x['w1'], x['w6'], [n for n in x['names'] if n]) for x in d['elems'] if x['id'] != -1 or x['w1'] != -1 or x['w6'] != -1 or any(x['names'])]
            print(p, 'w0-4=', d['w0'], d['w1'], d['w2'], d['w3'], d['w4'], els)




def join(town):
    """Join EditPartsData with mapinfo.cfg parts and the PTS info of data/gedit/e0<town+1>/scene.scn.  Returns rows of dicts."""
    import mapinfo, pts
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'data', 'gedit', 'e0%d' % (town + 1))
    mi = mapinfo.load(base)
    scn = pts.open_scn(os.path.join(base, 'scene.scn'))
    e = Elf()
    rows = []
    for p in range(24):
        d = entry(e, town, p)
        row = {'part': p, 'tbl': d}
        pd = mi.parts.get(p)
        row['def'] = pd
        if pd and pd.file.endswith('.pts'):
            nm = os.path.splitext(os.path.basename(pd.file))[0]
            pt = scn.parts.get(nm)
            row['pts'] = pt.info if pt else None
        else:
            row['pts'] = None
        rows.append(row)
    return rows


def report(town):
    print('== town', town)
    for r in join(town):
        d = r['tbl']
        pd = r['def']
        if d['w0'] == 0 and not pd:
            continue
        info = r['pts']
        ids = [x['id'] for x in d['elems']]
        s = '%2d %-8s %-22s max=%-3d stack=%d w2=%2d w3=%2d trig=%d tbl_ids=%s' % (
            r['part'], pd.directive.replace('_PARTS', '') if pd else '-', os.path.basename(pd.file) if pd else '-', d['w0'], d['w1'], d['w2'], d['w3'], d['w4'], ids)
        if info:
            s += ' | pts %dx%d max=%d f14=%d f1c=%d ids=%s' % (info.width, info.height, info.max_count, info.f14, info.f1c, list(info.element_ids))
            if list(info.element_ids) != ids:
                s += '  <<ID MISMATCH'
        elif pd and pd.info:
            s += ' | cfg %dx%d max=%d' % (pd.info['width'], pd.info['height'], pd.info['max_count'])
        print(s)


if __name__ == '__main__':
    main()
