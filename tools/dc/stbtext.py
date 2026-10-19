"""Pseudo-code printer for the event / town / dungeon-event .stb scripts (readable alternative to stbdis.py).
usage: python tools/dc/stbtext.py gedit/e01/event.stb [function id]    (path relative to data/; prints the function and every internal function it calls)

The VM is a stack machine; this tool replays the stack symbolically: expressions are rebuilt, external calls print as
NAME(arg, ...) with the ext names of the EVENT + DUNGEON tables (not the monster table that stbdis.py prefers), `vN` are frame slots
(N = 0 .. nargs-1 are the arguments), `&vN` is a pointer slot (output argument), jumps keep their target as a label.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import event_usage as eu

OPS = ["?0", "load", "addr", "push", "drop", "assign", "add", "sub", "mul", "div", "mod", "neg", "tofloat", "toint", "cmp", "return", "jmp", "jf", "jt", "call",
       "print", "ext", "nop", "yield", "and", "or", "not", "end", "endskip", "sin", "cos"]
CMP = {0x28: '==', 0x29: '!=', 0x2a: '<', 0x2b: '<=', 0x2c: '>', 0x2d: '>='}
BIN = {6: '+', 7: '-', 8: '*', 9: '/', 10: '%', 0x18: '&', 0x19: '|'}


def var(w1, mode, st):
    name = 'v%d' % w1
    if mode in (2, 0x10):
        idx = st.pop() if st else '?'
        return '%s[%s]' % (name, idx)
    if mode in (4, 0x20):
        idx = st.pop() if st else '?'
        return '(*%s)[%s]' % (name, idx)
    return name


def text_function(b, code, names, pc, end, labels_wanted):
    out = []
    st = []
    o = pc
    targets = set()
    # first pass: jump targets
    p = pc
    while p + 12 <= min(end, len(b)):
        op, a, bb = struct.unpack_from('<3I', b, p)
        if op in (0x10, 0x11, 0x12):
            targets.add(code + a)
        p += 12
    while o < end and o + 12 <= len(b):
        op, a, bb = struct.unpack_from('<3I', b, o)
        if o in targets:
            out.append('L%06x:' % o)
        pre = '%06x  ' % o
        if op == 1:
            st.append(var(a, bb, st))
        elif op == 2:
            st.append('&' + var(a, bb, st))
        elif op == 3:
            if a == 1:
                v = bb - (1 << 32) if bb & 0x80000000 else bb
                st.append(str(v))
            elif a == 2:
                st.append('%gf' % struct.unpack('<f', struct.pack('<I', bb))[0])
            elif a == 3:
                e = b.index(b'\0', code + bb)
                st.append('"%s"' % b[code + bb:e].decode('latin1'))
            else:
                st.append('?')
        elif op == 4:
            e = st.pop() if st else '?'
            out.append(pre + e + ';')
        elif op == 5:
            v = st.pop() if st else '?'
            d = st.pop() if st else '?'
            st.append('(%s = %s)' % (d, v))
        elif op in BIN:
            r = st.pop() if st else '?'
            l = st.pop() if st else '?'
            st.append('(%s %s %s)' % (l, BIN[op], r))
        elif op == 0xe:
            r = st.pop() if st else '?'
            l = st.pop() if st else '?'
            st.append('(%s %s %s)' % (l, CMP.get(a, 'cmp%d' % a), r))
        elif op == 0xb:
            st.append('-' + (st.pop() if st else '?'))
        elif op == 0xc:
            st.append('float(%s)' % (st.pop() if st else '?'))
        elif op == 0xd:
            st.append('int(%s)' % (st.pop() if st else '?'))
        elif op == 0x1a:
            st.append('!' + (st.pop() if st else '?'))
        elif op == 0x1d:
            st.append('sin(%s)' % (st.pop() if st else '?'))
        elif op == 0x1e:
            st.append('cos(%s)' % (st.pop() if st else '?'))
        elif op == 0xf:
            out.append(pre + 'return %s;' % (st.pop() if st else '?'))
        elif op == 0x10:
            out.append(pre + 'goto L%06x;' % (code + a))
        elif op in (0x11, 0x12):
            c = st.pop() if st else '?'
            out.append(pre + ('if (!%s) goto L%06x;' if op == 0x11 else 'if (%s) goto L%06x;') % (c, code + a) + ('   // short-circuit keeps value' if bb else ''))
        elif op == 0x13:
            rec = code + bb
            nargs = struct.unpack_from('<I', b, rec + 0xc)[0]
            args = [st.pop() if st else '?' for _ in range(nargs)][::-1]
            labels_wanted.add(rec)
            st.append('fn_%x(%s)' % (rec, ', '.join(args)))
        elif op == 0x15:
            args = [st.pop() if st else '?' for _ in range(a)][::-1]
            fid = None
            try:
                fid = int(args[0])
            except ValueError:
                pass
            out.append(pre + '%s(%s);' % (names.get(fid, 'ext%s' % args[0]), ', '.join(args[1:])))
        elif op == 0x17:
            out.append(pre + 'yield;')
        elif op == 0x1b:
            out.append(pre + 'end;')
        elif op == 0x1c:
            out.append(pre + 'endskip;   // START-button skip stops here')
        elif op == 0x14:
            args = [st.pop() if st else '?' for _ in range(a)][::-1]
            out.append(pre + 'print(%s);' % ', '.join(args))
        elif op == 0x16:
            pass
        elif op in (0x18, 0x19):
            pass
        else:
            out.append(pre + '?op%d %d %d' % (op, a, bb))
        o += 12
    return out


def main():
    path = sys.argv[1]
    want = int(sys.argv[2]) if len(sys.argv) > 2 else None
    b = open(os.path.join(eu.DATA, path), 'rb').read()
    names = eu.event_names()
    code, fl = eu.functions_of(b)
    byrec = {rec: (fid, pc, end) for fid, rec, pc, end in fl}
    done = set()
    todo = []
    for fid, rec, pc, end in fl:
        if want is None and fid is not None or fid == want:
            todo.append(rec)
    if not todo and want is None:
        todo = [rec for fid, rec, pc, end in fl]
    while todo:
        rec = todo.pop(0)
        if rec in done or rec not in byrec:
            continue
        done.add(rec)
        fid, pc, end = byrec[rec]
        wanted = set()
        nslots, nargs = struct.unpack_from('<2I', b, rec + 8)
        print()
        print('%s  // record %#x slots %d args %d' % (('function %d' % fid) if fid is not None else 'fn_%x' % rec, rec, nslots, nargs))
        for l in text_function(b, code, names, pc, end, wanted):
            print('  ' + l)
        todo.extend(sorted(wanted - done))


if __name__ == '__main__':
    main()
