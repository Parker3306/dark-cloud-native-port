"""Dungeon event scripts (data/dun/script/d0N/event.stb): ext-function usage table, marker -> function map, per-function closures.

usage:
  dungeon_events.py usage                 markdown table: every ext function used by the 7 dungeon scripts, calls per dungeon (static call sites)
  dungeon_events.py funcs                 function ids of every dungeon script (the entry points the engine can start)
  dungeon_events.py markers               PT_MARKER lines of dun/*.cfg  (name, radius, event id, item-use, auto, switch, ext) and which script function they start
  dungeon_events.py ext                   markdown table of the DUNGEON ext table (ids 1010..1101): signature, semantics (notes/event_ext.csv + corrections), static call sites per dungeon
  dungeon_events.py closure d01 510 ...   ext calls (with literal args when they are plain constants) reachable from the functions, in first-use order
  dungeon_events.py state d01 510 ...     same, only the calls that change game state (flags, items, map jumps, spawns, switches ...)
Static counts: one call site in a loop counts once.  Names come from notes/event_ext.csv via event_usage.event_names() (EVENT table 0..1000 + DUNGEON table 1010..1101).
"""
import glob
import os
import re
import sys
from collections import defaultdict, OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import event_usage as eu
import stbtext

DATA = eu.DATA
DUNS = ['d01', 'd02', 'd03', 'd04', 'd05', 'd06', 'd07']

STATE_PREFIX = ('_SSET_', '_SADD_', '_SITEM_', '_SSKILL', '_SEQUIP', '_SET_EVENT_', '_SET_FLOOR', '_SET_LIMMIT', '_SET_RES_LIMMIT',
                '_MAP_JUMP', '_SET_RETURN', '_RUN_SCRIPT_NO', '_CLEAN_MONSTOR', '_SET_MONSTOR_ID', '_SET_ATRA', '_SET_IBOX', '_SET_MAIN_CHR',
                '_SET_DEAD', '_SET_URA', '_GO_DUNGEON', '_SET_QUEST', '_SET_DUNGEON', '_LOAD_MONSTOR', '_SET_RANDOM_MAP', '_LOAD_DUNGEON_MAP2',
                '_SET_WORK_FLAG', '_SET_EVENT_FLG', '_HOLD_ITEM_EVENT', '_SET_NPC_ON_OFF', '_GOTO_', '_NAME_REGISTRY', '_OPEN_', '_ITEM_USE',
                '_SET_STATUS_ERR', '_BOM_SET', '_SET_RUBY', '_NO_', '_USER_WEAPON')


def script_path(d):
    return os.path.join(DATA, 'dun', 'script', d, 'event.stb')


def scan_file(d):
    """-> (function-id list, {ext name: call-site count}) over every function body, internal helpers included."""
    names = eu.event_names()
    r = eu.scan(script_path(d), names)
    counts = defaultdict(int)
    for fid, calls in r.items():
        for c in calls:
            if c is not None:
                counts[c] += 1
    fids = sorted(int(k) for k in r if re.fullmatch(r'-?\d+', k) and int(k) >= 0)
    return fids, counts, names


def cmd_usage():
    names = eu.event_names()
    tot = defaultdict(lambda: defaultdict(int))
    for d in DUNS:
        _, counts, _ = scan_file(d)
        for c, n in counts.items():
            tot[c][d] = n
    print('| id | name | ' + ' | '.join(DUNS) + ' | total |')
    print('|---|---|' + '---|' * (len(DUNS) + 1))
    for c in sorted(tot):
        row = [str(tot[c].get(d, '')) for d in DUNS]
        print('| %d | %s | %s | %d |' % (c, names.get(c, '???'), ' | '.join(row), sum(tot[c].values())))
    print()
    print('%d distinct ext functions; per dungeon: %s' % (len(tot), ', '.join('%s %d' % (d, sum(1 for c in tot if d in tot[c])) for d in DUNS)))


def cmd_funcs():
    for d in DUNS:
        fids, _, _ = scan_file(d)
        print(d, ' '.join(map(str, fids)))


def read_cfg(path):
    return open(path, 'rb').read().decode('cp932', errors='replace')


MARKER = re.compile(r'PT_MARKER\s+"([^"]*)"\s*,\s*([-\d.]+)\s*,\s*(-?\d+)\s*,\s*(ON|OFF)\s*,\s*(ON|OFF)\s*,\s*(ON|OFF)\s*,\s*(ON|OFF)')


def markers():
    out = []
    for p in sorted(glob.glob(os.path.join(DATA, 'dun', 'd0*.cfg'))):
        t = read_cfg(p)
        for m in MARKER.finditer(t):
            out.append((os.path.basename(p), m.group(1), float(m.group(2)), int(m.group(3)), m.group(4), m.group(5), m.group(6), m.group(7)))
    return out


def cmd_markers():
    print('cfg | marker name | radius | event id | item-use | auto | switch(initially on) | ext   -> script function present?')
    fidsets = {d: set(scan_file(d)[0]) for d in DUNS}
    seen = set()
    for row in markers():
        if row in seen:
            continue
        seen.add(row)
        cfg, name, rad, ev, a, b, c, e = row
        d = cfg[:3]
        print('%-18s %-14s %5.1f %4d %-3s %-3s %-3s %-3s  %s' % (cfg, name, rad, ev, a, b, c, e, 'yes' if ev in fidsets.get(d, ()) else 'NO'))


# corrections / additions found by reading the decompile (notes/dungeon_events.md section 3); they replace the text of event_ext.csv
OVERRIDES = {
    1056: ('(k [, load [, x,y,z [, death_event]]])', 'READ. Spawns a live monster from the k-th base model loaded for this floor (BtEnemyLayoutList row of (dungeon, floor), models in load order; a monster row with extra model stems takes extra indices) into the first free view slot (16 slots, slot number = order of creation, boss scripts address their helpers by that number). `load` != 0 first calls BtLoadMonstor + CleanViewMonstor (needed after a heavy event wiped the monsters). 5/6 args give the position (2 args: the monster script function 1 places it itself, e.g. c12a `_SET_POSITION(200,40,0)`); the 6th argument is the id of an event function run when this monster dies (view slot +0x1e4c6, fired through CheckEventFlag2 as a normal event with heavy=0). Runs the monster script function 1 at once.'),
    1061: ('(fid [, heavy [, keep_fade]])', 'READ. Request 5: when the current event ends (or at once if the script yields) the loop runs function `fid` as a new event: DAT_01d56774 = heavy (2nd arg, default 0), DAT_01d567d0 = keep_fade (3rd arg; 0 = do not EdFadeInit at the start of the next event). Used as `_RUN_SCRIPT_NO(100)` after the exit stairs, `_RUN_SCRIPT_NO(199, 1, 0)` for the first-visit scenes.'),
    1021: ('(&result, item_id, ...)', 'READ. Request 1: opens the item picker (BtMiniItemSelect) restricted to the listed ids (-1 terminated list at DAT_01d5677c; a single argument = no restriction); result = chosen item id or -1. While it is open the event VM is not resumed (gameTask 0x19a).'),
    1066: ('(fid [, arg])', 'READ. DAT_01d567e0 = fid, DAT_01d567e4 = arg (heavy flag): when CMonstorUnit::GetMonstorNum() < 1 (live monsters of the floor, decremented on each kill) and the player is alive the loop runs fid once as an event and clears the request.'),
    1068: ('(zone)', 'READ. UserStatus+0x8b10 = zone (-1 none); loads sound set 0x1b. Zone is the "limit zone" of the floor (time limit/dark zone effect), restored by 1080.'),
    1050: ('()', 'VERIFIED (disassembly of request handler 4 at 0x01db4ce4). Request 4: BtSystemScriptAfter (leave event mode, camera back to the follow camera), gameTask = 0; the floor-start script may still be running its last yields. Last call of 90/100/198/199.'),
}


def cmd_ext():
    names = eu.event_names()
    import csv
    rows = {int(r['id']): r for r in csv.DictReader(open(os.path.join(HERE, '..', '..', 'notes', 'event_ext.csv'), encoding='utf-8'))}
    tot = defaultdict(lambda: defaultdict(int))
    for d in DUNS:
        _, counts, _ = scan_file(d)
        for c, n in counts.items():
            tot[c][d] = n
    print('| id | name | args | semantics | sites d01..d07 |')
    print('|---|---|---|---|---|')
    for i in sorted(k for k in names if k >= 1000):
        r = rows.get(i, {})
        args, sem = r.get('args', ''), r.get('semantics', '')
        if i in OVERRIDES:
            args, sem = OVERRIDES[i]
        sites = '/'.join(str(tot[i].get(d, 0)) for d in DUNS)
        print('| %d | %s | `%s` | %s | %s |' % (i, names[i], args.replace('|', '/'), sem.replace('|', '/').replace(chr(10), ' '), sites))


def closure_calls(d, fids):
    b = open(script_path(d), 'rb').read()
    names = eu.event_names()
    code, fl = eu.functions_of(b)
    byrec = {rec: (fid, pc, end) for fid, rec, pc, end in fl}
    byfid = {fid: rec for fid, rec, pc, end in fl if fid is not None}
    seen, todo, calls = set(), [byfid[f] for f in fids if f in byfid], []
    while todo:
        rec = todo.pop(0)
        if rec in seen or rec not in byrec:
            continue
        seen.add(rec)
        fid, pc, end = byrec[rec]
        wanted = set()
        for line in stbtext.text_function(b, code, names, pc, end, wanted):
            m = re.match(r'\s*[0-9a-f]+\s+(_[A-Z0-9_]+)\((.*)\);', line)
            if m:
                calls.append(('fn%s' % (fid if fid is not None else '_%x' % rec), m.group(1), m.group(2)))
        todo.extend(sorted(wanted - seen))
    return calls


def cmd_closure(state_only):
    d = sys.argv[2]
    fids = [int(a) for a in sys.argv[3:]]
    seen = set()
    for where, name, args in closure_calls(d, fids):
        if state_only and not name.startswith(STATE_PREFIX):
            continue
        key = (name, args)
        if key in seen:
            continue
        seen.add(key)
        print('%-10s %s(%s)' % (where, name, args))


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'usage'
    {'usage': cmd_usage, 'ext': cmd_ext, 'funcs': cmd_funcs, 'markers': cmd_markers,
     'closure': lambda: cmd_closure(False), 'state': lambda: cmd_closure(True)}[cmd]()
