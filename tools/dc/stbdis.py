"""Disassembler for the game's script bytecode (.stb): python tools/dc/stbdis.py dun/monstor/e01a.stb [function id]
Instructions are {op, a, b} words; external calls (op 0x15) are annotated with the function name from the game's ext_func_info tables
(monster table first, then the dungeon-event and town-event tables: ids overlap between the three)."""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import disc as D
from elfdata import Elf

OPS = ["?0", "load", "addr", "push", "drop", "assign", "add", "sub", "mul", "div", "mod", "neg", "tofloat", "toint", "cmp", "return", "jmp", "jf", "jt", "call",
       "print", "ext", "nop", "yield", "and", "or", "not", "end", "endskip", "sin", "cos"]
TABLES = {'monster': (0x2918A0, 736), 'dungeon': (0x26B310, 544), 'event': (0x269C50, 2456)}
INDIRECT = (2, 4, 0x10, 0x20)    # addressing modes that pop an index


def ext_names(order=('monster', 'dungeon', 'event')):
    e = Elf()
    funcs = {}
    for line in open(os.path.join(HERE, '..', '..', 'notes', 'symbols.csv'), encoding='latin1'):
        f = line.strip().split(',')
        if len(f) >= 5 and f[2] == 'FUNC':
            try:
                funcs[int(f[0], 16)] = f[4].replace('__FP12RS_STACKDATAi', '')
            except ValueError:
                pass
    out = {}
    for tname in order:
        addr, size = TABLES[tname]
        for i in range(size // 8):
            fp, idn = struct.unpack('<Ii', e.read(addr + 8 * i, 8))
            if idn >= 0:
                out.setdefault(idn, funcs.get(fp, hex(fp)))
    return out


def stack_effect(op, a, bb):
    if op in (1, 2):
        return (1 if bb in INDIRECT else 0), 1
    if op == 3:
        return 0, 1
    if op == 4:
        return 1, 0
    if op == 5:
        return 2, 1
    if op in (6, 7, 8, 9, 10, 0xE, 0x18, 0x19):
        return 2, 1
    if op in (0xB, 0xC, 0xD, 0x1A, 0x1D, 0x1E):
        return 1, 1
    if op in (0x11, 0x12):
        return 1, 0
    return None


def disassemble(b, names, pc_start, pc_end, code):
    o = pc_start
    st = []
    lines = []
    while o < pc_end and o + 12 <= len(b):
        op, a, bb = struct.unpack_from('<3I', b, o)
        text = '%06x  %-8s %d %d' % (o, OPS[op] if op < len(OPS) else '?%d' % op, a, bb)
        if op == 3 and a == 2:
            text += '   ; %g' % struct.unpack('<f', struct.pack('<I', bb))[0]
        elif op == 3 and a == 3:
            end = b.index(b'\0', code + bb)
            text += '   ; "%s"' % b[code + bb:end].decode('latin1')
        elif op == 0x15:
            fid = st[-a] if 0 < a <= len(st) else None
            text += '   ; %s (id %s)' % (names.get(fid, '?'), fid)
            if a:
                del st[-a:]
        elif op == 0x13:
            text += '   ; call record %#x' % (code + bb)
        elif op in (0x10, 0x11, 0x12):
            text += '   ; -> %06x' % (code + a)
        eff = stack_effect(op, a, bb)
        if op == 3:
            st.append(bb if a == 1 else None)
        elif eff is not None:
            for _ in range(eff[0]):
                if st:
                    st.pop()
            st.extend([None] * eff[1])
        lines.append(text)
        o += 12
    return lines


def main():
    path = sys.argv[1].replace('/', '\\')
    want = int(sys.argv[2]) if len(sys.argv) > 2 else None
    d = D.Disc(os.path.join(HERE, '..', '..', 'iso', 'Dark Cloud (USA).iso'), readonly=True)
    b = d.read(path)
    names = ext_names()
    w = lambda o: struct.unpack_from('<I', b, o)[0]
    code, table, count = w(8), w(0xC), w(0x10)
    funcs = [(w(table + 8 * i), w(table + 8 * i + 4)) for i in range(count)]
    print('header: default record %#x, code base %#x, %d functions: %s' % (w(4), code, count, [(i, hex(o)) for i, o in funcs]))
    recs = funcs + [(-1, w(4))]
    # Each function is laid out as [record: 0x38 bytes {entry, ?, nslots, nargs, ...}][body]; bodies end where the next record begins.
    # Internal functions only appear as targets of `call` instructions, so discover records by following calls.
    records = {rec for _, rec in recs}
    changed = True
    while changed:
        changed = False
        for rec in sorted(records):
            pc = code + w(rec)
            nxt = [r for r in sorted(records) if r > pc]
            end = nxt[0] if nxt else len(b)
            o = pc
            while o < end and o + 12 <= len(b):
                op, a_, bb = struct.unpack_from('<3I', b, o)
                if op == 0x13 and code + bb not in records and code + bb < len(b):
                    records.add(code + bb)
                    changed = True
                o += 12
            if changed:
                break
    names_of = {rec: fid for fid, rec in recs}
    for rec in sorted(records):
        fid = names_of.get(rec, None)
        if want is not None and fid != want:
            continue
        pc = code + w(rec)
        nxt = [r for r in sorted(records) if r > pc]
        end = nxt[0] if nxt else len(b)
        label = 'function %d' % fid if fid is not None else 'internal function'
        print()
        print('%s  record %#x entry %#x slots %d args %d' % (label, rec, pc, w(rec + 8), w(rec + 0xC)))
        for line in disassemble(b, names, pc, end, code):
            print(line)

if __name__ == '__main__':
    main()
