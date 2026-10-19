"""Dump every Georama town's villager data in readable form (see notes/villagers.md).

usage: python -I tools/dc/villagers.py [e01 e02 ...] [--interiors] [--persons]

Per town (default e01..e05):
  * mapinfo.cfg PEOPLE2 rows (model, 'indoor' arg, moves, speed, TALK_EVENT/ROT/DIR), TIME_TABLE week 0 (4 buckets x 6 villager numbers),
    PEOPLE_LIST (only used when present: random schedule);
  * EditCharaData (ELF 0x267070) rows that OVERRIDE the first 16 rows (model, part, element, moves, speed, hide_until_event);
  * `person` function records (type 1) of the placeable parts: the standing point of each villager relative to its house (--persons);
  * interiors (--interiors): the _LOAD_IN_VILLAGER calls of every house's event.stb (slot, villager, model, x, y, z, yaw) and which
    villagers the house forces inside with _APPEAR_VILLAGER_IN.
"""
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mapinfo  # noqa: E402
import pts  # noqa: E402
import georama_edit_tbl as G  # noqa: E402
from elfdata import Elf  # noqa: E402

ROOT = os.path.join(HERE, '..', '..')
GEDIT = os.path.join(ROOT, 'data', 'gedit')
BUCKETS = ['day(11.5-2.5)', 'dusk(2.5-5.5)', 'night(5.5-8.5)', 'morning(8.5-11.5)']


def town_report(town, persons=False):
    e = Elf()
    t = int(town[2:]) - 1
    m = mapinfo.load(os.path.join(GEDIT, town))
    chara = G.chara_table(e, t)
    print('=' * 100)
    print('%s  (town index %d)   TIME_STOP=%s' % (town, t, m.time_stop))
    print('villager rows: mapinfo PEOPLE2 (+ EditCharaData override for the first 16)')
    print('  v  mapinfo(model arg1 moves speed talk_event rot dir)         EditCharaData(model part elem moves speed hide_until_event)')
    for i, p in enumerate(m.people):
        c = chara[i] if i < len(chara) else None
        cs = ('%-7s part %2d elem %2d moves %d speed %.2f hide %d' % (c['model'], c['part'], c['element'], c['moves'], c['speed'],
                                                                   c['hide_until_event'])) if c else '-'
        print('  %2d %-6s %d %d %.2f ev=%s rot=%s dir=%s    %s' % (i, p['name'], p['indoor_only'], p['moves'], p['speed'], p['talk_event'],
                                                                 p['talk_rot'], p['talk_dir'], cs))
    for week in range(7):
        rows = [m.time_table.get((week, b)) for b in range(4)]
        if any(rows):
            print('TIME_TABLE week %d' % week)
            for b in range(4):
                print('   %-18s %s' % (BUCKETS[b], rows[b]))
    print('PEOPLE_LIST', m.people_list)
    if persons:
        scn = pts.open_scn(os.path.join(GEDIT, town, 'scene.scn'))
        print('person records (type 1) per part: villager, local pos, local yaw, window')
        for idx, pd in sorted(m.parts.items()):
            nm = os.path.basename(pd.file)
            nm = nm[:-4] if nm.endswith('.pts') else nm
            p = scn.parts.get(nm)
            if not p:
                continue
            for f in p.funcs:
                if f.type == 1:
                    print('   part %2d %-8s villager %2d pos (%8.2f %8.2f %8.2f) yaw %6.3f time %g..%g' %
                          (idx, nm, f.id, f.pos[0], f.pos[1], f.pos[2], f.rot[1], f.t_start, f.t_end))


def _args_before(lines, k):
    """arguments pushed before the ext call on lines[k]: walk back over push/neg lines"""
    out = []
    j = k - 1
    while j >= 0:
        parts = lines[j].split()
        if len(parts) < 2 or parts[1] not in ('push', 'neg'):
            break
        out.append(lines[j])
        j -= 1
    out.reverse()
    vals = []
    for ln in out:
        parts = ln.split()
        if parts[1] == 'neg':
            vals[-1] = -vals[-1]
            continue
        kind, raw = int(parts[2]), int(parts[3])
        if kind == 1:
            vals.append(raw)
        elif kind == 2:
            vals.append(round(struct.unpack('<f', struct.pack('<I', raw & 0xffffffff))[0], 3))
        elif kind == 3:
            vals.append(ln.split(';', 1)[1].strip().strip('"') if ';' in ln else raw)
        else:
            vals.append(raw)
    return vals


def interior_report(town):
    import event_usage as eu
    from stbdis import disassemble
    names = eu.event_names()
    for p in sorted(glob.glob(os.path.join(GEDIT, town, 'in', '*', 'event.stb'))):
        b = open(p, 'rb').read()
        code, fl = eu.functions_of(b)
        house = os.path.basename(os.path.dirname(p))
        loads, appear = [], []
        for fid, rec, pc, end in fl:
            lines = list(disassemble(b, names, pc, end, code))
            for k, ln in enumerate(lines):
                if '_LOAD_IN_VILLAGER' in ln:
                    loads.append(_args_before(lines, k)[1:])
                elif '_APPEAR_VILLAGER_IN' in ln or '_APPEAR_VILLAGER_OFF' in ln or '_APPEAR_VILLAGER_OUT' in ln or '_APPEAR_VILLAGER_ON' in ln:
                    a = _args_before(lines, k)
                    appear.append((ln.split('; ')[1].split(' ')[0].replace('_APPEAR_VILLAGER_', ''), a[1:], 'fn %s' % fid))
        print('  %-8s' % house, 'LOAD_IN_VILLAGER (slot, vil, model, x, y, z, yaw):', loads, ' APPEAR:', appear)


def main(argv):
    towns = [a for a in argv if a.startswith('e0')] or ['e01', 'e02', 'e03', 'e04', 'e05']
    for t in towns:
        town_report(t, '--persons' in argv)
        if '--interiors' in argv:
            print('interiors of', t)
            interior_report(t)


if __name__ == '__main__':
    main(sys.argv[1:])
