"""Dark Cloud opening (data/opdat/opening.scr): parser, timeline and consistency checks.

The script is run by the title overlay (TITLE.BIN): `CScript::Step` (0x01DABEE0) matches one of 26 command names by prefix
(table at 0x01DD5380, 0x2C bytes per entry: name ptr, name length, argument count, argument types), parses the arguments
as floats (`ON` = 1, `OFF` = 0; first argument without comma, the others after a comma) into `CScript+0xE0C8..` and
`CScript::CheckScript` (0x01DAC110, a 26 way switch at 0x01DDBEC0) stores them into the CScript object (0x01DE1B00).
See notes/opening.md for the full semantics.

usage:  opening_dump.py commands            raw command list (line, command, args, comment)
        opening_dump.py timeline [--lang N] annotated timeline (default), N = optext_N.mes bank (1 = English)
        opening_dump.py check               consistency checks (camera ranges against the .sne files, unknown commands)
        opening_dump.py stats               command histogram
        opening_dump.py objects             per stage-object motion tables (KEY lines with their comments)
Library: parse_script(), simulate(), load_sne_table(), Cmd.
"""
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
DATA = os.path.join(ROOT, 'data')
OPDAT = os.path.join(DATA, 'opdat')
TITLE_BIN = os.path.join(ROOT, 'extracted', 'TITLE.BIN')
TITLE_BASE = 0x01DABD00          # load address of the overlay file (file offset 0 = this address)

FRAME_HZ = 60.0                  # CScript::Step is called once per main loop iteration (assumed 60 Hz, see notes/opening.md)

# (name, number of arguments) in the order of the game's table (0x01DD5380); the order matters because the game
# matches by prefix: SE_STOP must come before SE, OBJ_MOTION2 before OBJ_MOTION, SCENE_LOAD before SCENE, WAIT_KEY before WAIT.
COMMANDS = [
    ('OBJ_MOTION2', 3), ('OBJ_MOTION', 2), ('OBJ_MOVE', 2), ('OBJ_TALK', 7), ('OBJ_EYE', 3), ('OBJ_MOUTH', 3),
    ('OBJ_DISP', 2), ('OBJ_STEP', 2), ('CAMERA', 4), ('SE_STOP', 3), ('SE', 4), ('SOUND_FADE', 3), ('FADE_IN', 2),
    ('FADE_OUT', 2), ('MESSAGE', 6), ('NEXT_MES', 1), ('MES_CLR', 0), ('LOAD_OBJ', 2), ('SPRITE', 1), ('BOM', 4),
    ('BEEM', 6), ('SCENE_LOAD', 1), ('SCENE', 1), ('END', 0), ('WAIT_KEY', 1), ('WAIT', 1),
]
CMD_NARGS = dict(COMMANDS)
# the table says OBJ_MOTION has 3 argument slots but the third has type 0 (no text) so only 2 are parsed; the file uses 2.

# stage object index -> (name from the script comments, English)
OBJ_NAMES = {
    0: ('シーダ', 'Seeda'), 1: ('魔人', 'Majin (Dark Genie)'), 2: ('司祭', 'Priest'), 3: ('フラッグ', 'Flag'),
    4: ('魔人B', 'Majin bust-up'), 5: ('魔人C/蝶2', 'Majin hand / butterfly 2'), 6: ('呪いダンスリーダー', 'curse dance leader'),
    7: ('呪いダンサー', 'curse dancers (35)'), 8: ('トアンB', 'Toan B'), 9: ('ペイジ', 'Paige'), 10: ('レネ', 'Rene'),
    11: ('トアン', 'Toan'), 12: ('ジーナ', 'Gina'), 13: ('カール', 'Karl'), 14: ('オルネット', 'Ornet'), 15: ('マッチョ', 'Macho'),
    16: ('クロード', 'Claude'), 17: ('コマッチョ', 'Little Macho'), 18: ('オッド', 'Odd'), 19: ('村ダンサー男1', 'village dancer m'),
    20: ('村ダンサー女1', 'village dancer f'), 21: ('精霊王', 'Spirit King'), 22: ('蝶', 'butterfly'),
}
SCENE_NAMES = {0: 'A cursed dance (opinfo.cfg)', 1: 'B Norune village at night (norn.cfg)', 2: "B' Toan's house (toan.cfg)",
               3: 'C Norune festival (norn2.cfg)', 4: 'C dancers', 5: 'C Majin appears (majin.pak)', 6: 'C destruction',
               7: 'C destruction 2 (norn3.pak)', 8: 'D Spirit King (seirei.cfg)', 9: 'D Spirit King 2 (seirei2.pak)'}
SCENE_LOAD_FILES = {1: 'norn/norn.pak', 2: 'toan/toan.pim', 3: 'norn2/norn2.pim', 4: 'norn2/ndance.pak', 5: 'norn2/majin.pak',
                    6: 'norn2/norn2.pim', 7: 'norn3/norn3.pak', 8: 'norn4/seirei.pak', 9: 'norn4/seirei2.pak'}


class Cmd:
    __slots__ = ('line', 'name', 'args', 'comment', 'section', 'raw')

    def __init__(self, line, name, args, comment, section, raw):
        self.line, self.name, self.args, self.comment, self.section, self.raw = line, name, args, comment, section, raw

    def __repr__(self):
        return f'Cmd({self.line}, {self.name}, {self.args})'

    def i(self, k):
        return int(self.args[k])


def read_script(path=None):
    path = path or os.path.join(OPDAT, 'opening.scr')
    return open(path, 'rb').read().decode('cp932').replace('\r\n', '\n').split('\n')


def parse_script(path=None):
    """-> list of Cmd.  Mirrors CScript::Step: comments (`//` to end of line), whitespace skipped, prefix matching in table order,
    numbers read with atof, ON = 1.0, OFF = 0.0.  Raises ValueError on a line the game would `exit(-1)` on."""
    out = []
    section = ''
    for ln, line in enumerate(read_script(path), 1):
        code, _, comment = line.partition('//')
        comment = comment.strip()
        s = line.strip()
        if s.startswith('//') and '■' in s:
            section = s.lstrip('/ ').strip()
        code = code.strip()
        if not code:
            continue
        for name, nargs in COMMANDS:
            if code.startswith(name):
                rest = code[len(name):].strip()
                break
        else:
            raise ValueError(f'line {ln}: unknown command {code!r}')
        args = []
        if rest:
            for tok in rest.split(','):
                tok = tok.strip()
                if tok == 'ON':
                    args.append(1.0)
                elif tok == 'OFF':
                    args.append(0.0)
                else:
                    args.append(float(tok))
        if len(args) != nargs:
            raise ValueError(f'line {ln}: {name} expects {nargs} arguments, got {len(args)}: {code!r}')
        out.append(Cmd(ln, name, args, comment, section, code))
    return out


# ------------------------------------------------------------------------------------------------------------------
def load_sne_table():
    """The 125 camera files in the order the game streams them (pointer table at 0x01DD5820 in TITLE.BIN: name ptr, cfg ptr)."""
    d = open(TITLE_BIN, 'rb').read()

    def cstr(a):
        o = a - TITLE_BASE
        return d[o:d.index(b'\0', o)].decode('latin1')
    ents, a = [], 0x01DD5820
    while True:
        p = struct.unpack_from('<I', d, a - TITLE_BASE)[0]
        if not (0x01DDB000 < p < 0x01DE0000) or 'scene' not in cstr(p):
            break
        ents.append(cstr(p).split('/')[-1])
        a += 8
    return ents


def wait_steps(n, sp):
    """Number of CScript::Step calls from the Step that executes `WAIT n` to the Step that executes the next command."""
    c = n - 1.0
    if c < 1.0:
        return 1
    return int(math.floor((n - 2.0) / sp)) + 2


class Event:
    __slots__ = ('step', 'cmd', 'cut')

    def __init__(self, step, cmd, cut):
        self.step, self.cmd, self.cut = step, cmd, cut


class Cut:
    def __init__(self, index, step, cmd, sne):
        self.index, self.step, self.cmd, self.sne = index, step, cmd, sne
        self.events = []
        self.scene = None
        self.end_step = None

    @property
    def key(self):
        return self.cmd.i(0)

    @property
    def start(self):
        return self.cmd.i(1)

    @property
    def end(self):
        return self.cmd.i(2)

    @property
    def speed(self):
        return self.cmd.args[3]


def simulate(cmds, wait_key_steps=0):
    """Walk the script with the game's wait rule.  Returns (events, cuts, total_steps).  WAIT_KEY costs `wait_key_steps` (player dependent).
    Time unit = CScript::Step calls (1/60 s).  The speed that divides WAIT is the last CAMERA speed (CScript+0x4E4)."""
    sne = load_sne_table() if os.path.exists(TITLE_BIN) else []
    step, sp, scene, ncam = 0, 1.0, 0, 0
    cuts, events = [], []
    cut = None
    for c in cmds:
        if c.name == 'CAMERA':
            sp = c.args[3]
            cut = Cut(ncam, step, c, sne[ncam] if ncam < len(sne) else '?')
            cut.scene = scene
            cuts.append(cut)
            ncam += 1
        elif c.name == 'SCENE':
            scene = c.i(0)
        events.append(Event(step, c, cut))
        if cut is not None:
            cut.events.append(Event(step, c, cut))
        if c.name == 'WAIT':
            step += wait_steps(c.args[0], sp)
        elif c.name == 'WAIT_KEY':
            step += 2 + wait_key_steps
        elif c.name == 'END':
            break
    for a, b in zip(cuts, cuts[1:] + [None]):
        a.end_step = b.step if b else step
    return events, cuts, step


def fmt_time(steps):
    s = steps / FRAME_HZ
    return f'{int(s // 60)}:{s % 60:05.2f}'


# ------------------------------------------------------------------------------------------------------------------
def load_bank(n):
    sys.path.insert(0, HERE)
    import mesdump
    return mesdump.load(f'opdat/optext_{n}.mes')


def describe(c, bank):
    a = c.args
    n = c.name
    ia = [int(x) if float(x).is_integer() else x for x in a]
    if n == 'OBJ_TALK':
        t = bank.get(int(a[1]), '(no such message in this bank)')
        return (f'OBJ_TALK obj={ia[0]}({OBJ_NAMES.get(ia[0], ("?", "?"))[1]}) msg={ia[1]} frames={ia[2]} '
                f'pos=({ia[3]},{ia[4]}) tail=({ia[5]},{ia[6]})  "{t}"'.replace('\n', ' / '))
    if n == 'MESSAGE':
        t = bank.get(int(a[0]), '(no such message in this bank)')
        return f'MESSAGE msg={ia[0]} frames={ia[1]} pos=({ia[2]},{ia[3]}) tail=({ia[4]},{ia[5]})  "{t}"'.replace('\n', ' / ')
    if n == 'OBJ_DISP':
        return f'OBJ_DISP {ia[0]}({OBJ_NAMES.get(ia[0], ("?", "?"))[1]}) {"ON" if a[1] else "OFF"}'
    if n in ('OBJ_MOTION', 'OBJ_MOTION2', 'OBJ_MOVE', 'OBJ_STEP', 'OBJ_EYE', 'OBJ_MOUTH'):
        return f'{n} obj={ia[0]}({OBJ_NAMES.get(ia[0], ("?", "?"))[1]}) ' + ' '.join(str(x) for x in ia[1:])
    return n + ' ' + ', '.join(str(x) for x in ia)


def cmd_commands():
    for c in parse_script():
        print(f'{c.line:5d}  {c.name:11s} {", ".join(f"{x:g}" for x in c.args):28s} {c.comment}')


def cmd_stats():
    from collections import Counter
    cnt = Counter(c.name for c in parse_script())
    for k, v in cnt.most_common():
        print(f'{k:12s}{v}')
    print('total', sum(cnt.values()))


def cmd_timeline(lang=1, md=False):
    cmds = parse_script()
    bank = load_bank(lang)
    events, cuts, total = simulate(cmds)
    cur_scene = 0
    print(f'# opening.scr timeline (WAIT_KEY counted as 2 steps; Step rate {FRAME_HZ:g} Hz; {len(cmds)} commands, '
          f'{len(cuts)} CAMERA cuts, total {total} steps = {fmt_time(total)})')
    last_section = None
    for ev in events:
        c = ev.cmd
        if c.section != last_section:
            last_section = c.section
            print(f'\n== {c.section}   [{fmt_time(ev.step)}  step {ev.step}]')
        if c.name == 'CAMERA':
            cut = ev.cut
            span = (cut.end_step - cut.step)
            print(f'  {fmt_time(ev.step)}  CAMERA #{cut.index} {cut.sne} key={cut.key} frames {cut.start}..{cut.end} speed {cut.speed:g} '
                  f'(cut lasts {span} steps = {span / FRAME_HZ:.2f} s)')
            continue
        if c.name in ('WAIT',):
            continue
        if c.name == 'SCENE':
            cur_scene = c.i(0)
            print(f'  {fmt_time(ev.step)}  SCENE {cur_scene}  -> {SCENE_NAMES.get(cur_scene)}')
            continue
        if c.name == 'SCENE_LOAD':
            print(f'  {fmt_time(ev.step)}  SCENE_LOAD {c.i(0)}  (stream {SCENE_LOAD_FILES.get(c.i(0))})')
            continue
        print(f'  {fmt_time(ev.step)}    {describe(c, bank)}' + (f'   // {c.comment}' if c.comment else ''))


def cmd_check():
    cmds = parse_script()
    print('parsed', len(cmds), 'commands, all names/arg counts valid')
    events, cuts, total = simulate(cmds)
    sne = load_sne_table()
    print('CAMERA commands', len(cuts), 'sne table entries', len(sne))
    sys.path.insert(0, HERE)
    import opening_sne
    bad = 0
    for cut in cuts:
        s = opening_sne.load_sne(os.path.join(OPDAT, 'scene', cut.sne))
        rng = s.camera_range()
        ok = rng is not None and rng[0] <= cut.start and cut.end <= rng[1]
        if not ok:
            bad += 1
            print('  MISMATCH', cut.index, cut.sne, 'script', (cut.start, cut.end), 'sne', rng)
    print('cameras whose frame range is not inside the sne camera track:', bad)
    # every object index used is 0..22
    used = {c.i(0) for c in cmds if c.name.startswith('OBJ_')}
    print('objects used', sorted(used))
    print('SCENE_LOAD', sorted(c.i(0) for c in cmds if c.name == 'SCENE_LOAD'), 'SCENE', sorted(c.i(0) for c in cmds if c.name == 'SCENE'))
    print('total steps', total, fmt_time(total))


def cfg_keys(text):
    out, n, on = [], 0, False
    for ln in text.decode('cp932', 'replace').split('\n'):
        s = ln.strip()
        if s.startswith('KEY_START'):
            on, n = True, 0
        elif s.startswith('MOTION_END'):
            on = False
        elif on and s.startswith('KEY'):
            m = re.match(r'KEY\s+([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.\-]+)\s*(?://\s*(.*))?', s)
            if m:
                out.append((n, float(m.group(1)), float(m.group(2)), float(m.group(3)), (m.group(4) or '').strip()))
                n += 1
    return out


def cmd_objects():
    sys.path.insert(0, HERE)
    import pack
    srcs = [
        ('A obj0 Seeda', 'dungeon/dungeon.pak', '01c07a.cfg'), ('A obj1 Majin', 'dungeon/dungeon.pak', '01c08a.cfg'),
        ('A obj2 Priest', 'dungeon/dungeon.pak', '01c11a.cfg'), ('A obj3 Flag', 'dungeon/dungeon.pak', '01c09a.cfg'),
        ('A obj4 Majin bust', 'chara/01c08b.chr', '01c08b.cfg'), ('A obj5 Majin hand', 'chara/01c08c.chr', '01c08c.cfg'),
        ('A obj6 leader (phase 1a)', 'chara/01p19a1a.chr', '01p19a1a.cfg'), ('A obj7 dancer (phase 1a)', 'chara/01p17a1a.chr', '01p17a1a.cfg'),
        ('B obj8 Toan B', 'norn/norn.pak', None), ('B obj9 Paige', 'chara/03p09a.chr', '03p09a.cfg'),
        ('B obj10 Rene', 'chara/03p10a.chr', '03p10a.cfg'), ('B obj8 Toan', 'chara/03c01d.chr', '03c01d.cfg'),
        ('B obj11 Toan', 'chara/03c01d2.chr', '03c01d2.cfg'), ('B komono', 'toan/03komono.chr', '03komono.cfg'),
        ('C obj1 Majin', 'norn2/majin.pak', '04c08a.cfg'), ('C obj3 Flag', 'norn2/majin.pak', '04c09a.cfg'),
        ('C obj4 Majin bust', 'norn2/majinb.pak', '04c08b.cfg'), ('C obj11 Toan', 'norn2/norn2b.pak', '04c01d.cfg'),
        ('C obj12 Gina', 'norn2/norn2b.pak', '04p03a.cfg'), ('C obj13 Karl', 'norn2/norn2b.pak', '04p08a.cfg'),
        ('C obj14 Ornet', 'norn2/ndance.pak', '04p05b.cfg'), ('C obj15 Macho', 'norn2/ndance.pak', '04p01a.cfg'),
        ('C obj16 Claude', 'norn2/ndance.pak', '04p12a.cfg'), ('C obj17 Little Macho', 'norn2/norn2b.pak', '04p07a.cfg'),
        ('C obj18 Odd', 'norn2/norn2b.pak', '04p02a.cfg'), ('C obj19 village dancer m', 'norn2/ndance.pak', '04p15a.cfg'),
        ('C obj20 village dancer f', 'norn2/ndance.pak', '04p16a.cfg'),
        ('D obj8 Toan', 'norn4/seirei.pak', '05c01d.cfg'), ('D obj11 Toan 2', 'norn4/seirei.pak', '05c01e.cfg'),
        ('D obj21 Spirit King', 'norn4/seirei.pak', '05c03c.cfg'), ('D obj22 butterfly', 'norn4/seirei2.pak', 'buterfly.cfg'),
    ]
    for label, f, cfg in srcs:
        path = os.path.join(OPDAT, f)
        if not os.path.exists(path):
            continue
        p = pack.parse(open(path, 'rb').read())
        e = p.get(cfg) if cfg else None
        if e is None:
            continue
        print(f'== {label}   [{f} : {cfg}]')
        for n, a, b, sp, cm in cfg_keys(e.data):
            print(f'   {n:2d}: frames {a:g}..{b:g}  speed {sp:g}   {cm}')


def main():
    args = sys.argv[1:]
    what = args[0] if args else 'timeline'
    lang = 1
    if '--lang' in args:
        lang = int(args[args.index('--lang') + 1])
    if what == 'commands':
        cmd_commands()
    elif what == 'stats':
        cmd_stats()
    elif what == 'check':
        cmd_check()
    elif what == 'objects':
        cmd_objects()
    else:
        cmd_timeline(lang)


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    main()
