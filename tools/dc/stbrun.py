"""Headless tracer for event scripts: a small Python port of the script VM (dccore/script.cpp) with a logging "null host".
usage: python tools/dc/stbrun.py gedit/e01/event.stb 256 [max_frames] [info=<npc info id>] [place=<0 outside|1 inside>]
The host answers every poll at once (loads done, fade done, ASQ idle, message complete, OK pressed, new-game flags = 0) and prints each ext call with its arguments
(pointer arguments as &vN) and a marker for every frame the script yielded. Use it to see which ext functions a function really executes."""
import math, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import event_usage as eu

class Slot:
    __slots__ = ('t', 'v')
    def __init__(self, t=0, v=0):
        self.t = t; self.v = v
    def f(self):
        return float(self.v) if self.t == 0 else struct.unpack('<f', struct.pack('<I', self.v & 0xffffffff))[0]

def mkf(x):
    return Slot(1, struct.unpack('<I', struct.pack('<f', x))[0])

class VM:
    def __init__(self, data, host, names):
        self.b = data
        self.w = lambda o: struct.unpack_from('<I', self.b, o)[0]
        self.code, self.table, self.count = self.w(8), self.w(0xC), self.w(0x10)
        self.host = host
        self.names = names
        self.stack = [Slot() for _ in range(4096)]
        self.sp = 16; self.base = 16; self.calls = []; self.pc = 0; self.done = True; self.steps = 0

    def func_rec(self, fid):
        for i in range(self.count):
            if self.w(self.table + 8 * i) == fid:
                return self.w(self.table + 8 * i + 4)
        return 0

    def run(self, fid):
        rec = self.func_rec(fid)
        if not rec:
            return -1
        nargs, nslots = self.w(rec + 0xc), self.w(rec + 8)
        self.calls = []; self.done = False
        self.base = 16 - nargs; self.sp = self.base + nslots
        for i in range(nargs, nslots):
            self.stack[self.base + i] = Slot()
        self.pc = self.code + self.w(rec)
        self.exec()
        return 0 if self.done else 1

    def resume(self):
        if self.pc:
            self.exec()
        return 0 if self.done else 1

    def push(self, s):
        self.stack[self.sp] = s; self.sp += 1
    def pop(self):
        self.sp -= 1
        return self.stack[self.sp]

    def exec(self):
        b = self.b; P = self.w
        def slot_index(w1, mode):
            if mode in (0x20, 4):
                idx = self.pop().v
                return self.stack[self.base + w1].v + idx
            if mode in (0x10, 2):
                idx = self.pop().v
                return self.base + w1 + idx
            return self.base + w1
        while True:
            op, a, bb = P(self.pc), P(self.pc + 4), P(self.pc + 8)
            self.steps += 1
            if self.steps > 3000000:
                raise RuntimeError('step limit')
            if op == 1:
                i = slot_index(a, bb)
                if bb in (0x20, 0x10, 8):
                    self.stack[i].t = 1
                self.push(Slot(self.stack[i].t, self.stack[i].v))
            elif op == 2:
                i = slot_index(a, bb)
                self.push(Slot(3, i))
            elif op == 3:
                if a == 1: self.push(Slot(0, bb - (1 << 32) if bb & 0x80000000 else bb))
                elif a == 3: self.push(Slot(2, self.code + bb))
                elif a == 2: self.push(Slot(1, bb))
            elif op == 4:
                self.sp -= 1
            elif op == 5:
                v = self.pop(); d = self.pop()
                if d.t == 3:
                    self.stack[d.v] = Slot(v.t, v.v)
                self.push(v)
            elif op in (6, 7, 8, 9):
                r = self.pop(); l = self.pop()
                if l.t == 0 and r.t == 0:
                    x, y = l.v, r.v
                    if op == 6: z = x + y
                    elif op == 7: z = x - y
                    elif op == 8: z = x * y
                    else:
                        z = int(x / y) if y else 0
                    z = (z + (1 << 31)) % (1 << 32) - (1 << 31)
                    self.push(Slot(0, z))
                else:
                    x, y = l.f(), r.f()
                    z = x + y if op == 6 else x - y if op == 7 else x * y if op == 8 else (x / y if y else 0.0)
                    self.push(mkf(z))
            elif op == 10:
                r = self.pop().v; l = self.pop().v
                self.push(Slot(0, int(math.fmod(l, r)) if r else 0))
            elif op == 0xb:
                v = self.pop()
                self.push(Slot(0, -v.v) if v.t == 0 else mkf(-v.f()))
            elif op == 0xc:
                v = self.pop(); self.push(mkf(v.f()))
            elif op == 0xd:
                v = self.pop(); self.push(Slot(0, v.v if v.t == 0 else int(v.f())))
            elif op == 0xe:
                r = self.pop(); l = self.pop()
                if l.t == 0 and r.t == 0:
                    x, y = l.v, r.v
                else:
                    x, y = l.f(), r.f()
                res = {0x28: y == x, 0x29: y != x, 0x2a: x < y, 0x2b: x <= y, 0x2c: y < x, 0x2d: y <= x}.get(a, False)
                self.push(Slot(0, 1 if res else 0))
            elif op == 0xf:
                v = self.pop()
                if not self.calls:
                    self.pc = 0; self.done = True; return
                self.sp = self.base
                pc, base, rec = self.calls.pop()
                self.base = base; self.pc = pc
                self.push(v)
            elif op == 0x10:
                self.pc = self.code + a; continue
            elif op == 0x11:
                v = self.pop()
                if not (v.t != 0 or v.v != 0):
                    if bb: self.push(Slot(0, 0))
                    self.pc = self.code + a; continue
            elif op == 0x12:
                v = self.pop()
                if v.t != 0 or v.v != 0:
                    if bb: self.push(Slot(0, 1))
                    self.pc = self.code + a; continue
            elif op == 0x13:
                rec = self.code + bb
                self.calls.append((self.pc, self.base, rec))
                nargs, nslots = P(rec + 0xc), P(rec + 8)
                self.base = self.sp - nargs; self.sp = self.base + nslots
                for i in range(nargs, nslots):
                    self.stack[self.base + i] = Slot()
                self.pc = self.code + P(rec) - 12
            elif op == 0x14:
                self.sp -= a
            elif op == 0x15:
                self.sp -= a
                fid = self.stack[self.sp].v
                self.host.ext(fid, self, self.sp + 1, a - 1)
            elif op == 0x17:
                self.pc += 12; return
            elif op == 0x18:
                r = self.pop().v; l = self.pop().v; self.push(Slot(0, l & r))
            elif op == 0x19:
                r = self.pop().v; l = self.pop().v; self.push(Slot(0, l | r))
            elif op == 0x1a:
                v = self.pop(); self.push(Slot(0, 1 if v.v == 0 else 0))
            elif op == 0x1b:
                self.done = True; self.pc = 0; return
            elif op == 0x1d:
                v = self.pop(); self.push(mkf(math.sin(v.f())))
            elif op == 0x1e:
                v = self.pop(); self.push(mkf(math.cos(v.f())))
            self.pc += 12

    def arg(self, base, i):
        return self.stack[base + i]
    def argv(self, base, i):
        s = self.stack[base + i]
        if s.t == 2:
            e = self.b.index(b'\0', s.v)
            return '"' + self.b[s.v:e].decode('latin1') + '"'
        if s.t == 3:
            return '&v%d' % (s.v - self.base) if 0 <= s.v - self.base < 200 else '&?'
        if s.t == 1:
            return '%g' % s.f()
        return str(s.v)
    def setv(self, base, i, v):
        s = self.stack[base + i]
        if s.t == 3:
            tgt = self.stack[s.v]
            if isinstance(v, float):
                tgt.v = struct.unpack('<I', struct.pack('<f', v))[0]
            else:
                tgt.v = v & 0xffffffff if v >= 0 else v
        return


class Host:
    def __init__(self, names, flags, ints):
        self.names = names; self.log = []; self.flags = flags; self.ints = ints
        self.mes_state = {}
    def ext(self, fid, vm, base, n):
        name = self.names.get(fid, 'ext%d' % fid)
        args = ', '.join(vm.argv(base, i) for i in range(n))
        self.log.append('%s(%s)' % (name, args))
        S = lambda i, v: vm.setv(base, i, v)
        if name in ('_LOAD_SYNC', '_LOAD_SND_SYNC', '_CHECK_FADE', '_ASQ_CHECK', '_CHECK_BGM_FADE', '_EB_LOOP'):
            S(0, 1 if name in ('_CHECK_FADE',) else 0)
        elif name == '_SGET_GAME_FLAG':
            S(1, self.flags.get(vm.arg(base, 0).v, 0))
        elif name == '_SGET_GAME_INT_FLAG':
            S(1, self.ints.get(vm.arg(base, 0).v, 0))
        elif name == '_SSET_GAME_FLAG':
            self.flags[vm.arg(base, 0).v] = vm.arg(base, 1).v
        elif name == '_SSET_GAME_INT_FLAG':
            self.ints[vm.arg(base, 0).v] = vm.arg(base, 1).v
        elif name in ('_CHECK_MES', ):
            S(1, 1)
        elif name in ('_CHECK_MES_COMPLETE',):
            S(1, 1)
        elif name in ('_CHECK_MES_WAIT',):
            S(1, 0)
        elif name in ('_GET_PADDOWN', '_GET_PADON'):
            S(0, 0x20)
        elif name == '_GET_APAD':
            for i in range(n): S(i, 0.0)
        elif name == '_SITEM_CHECK':
            S(1, -1)
        elif name == '_SITEM_CHECK_ALL':
            S(1, 0)
        elif name == '_GET_TALKNPC_INFO_ID':
            S(0, self.talk_info)
        elif name == '_CHECK_PLACE':
            S(0, self.place)
        elif name in ('_SGET_NPC_TALK_MES',):
            S(1, 0)
        elif name == '_GET_OLD_MAPNO':
            S(0, 2)
        elif name == '_GET_RANDOM':
            S(0, 12345)
        elif name == '_GET_NOW_TIME':
            S(0, 0)
        elif name == '_SGET_DUNGEON_STATUS':
            S(1, 0); S(2, 0)
        elif name in ('_CHECK_COMPLETE_PARTS', '_SCHECK_REQUEST', '_SGET_CMP_EVENT', '_CHECK_IN_VILLAGER', '_CHECK_VILLAGER', '_SGET_MAP_FLAG', '_GET_WORK_FLAG', '_SGET_REQUEST', '_SGET_ATRA_PARTS_NUM', '_SGET_DAY', '_GET_BGM_NO', '_GET_FLOOR_LEVEL', '_GET_OLD_FLOOR_LEVEL', '_GET_ACTION_MODE'):
            S(1 if name not in ('_GET_BGM_NO', '_SGET_DAY', '_GET_FLOOR_LEVEL', '_GET_OLD_FLOOR_LEVEL', '_GET_ACTION_MODE') else 0, 0)
        elif name in ('_GET_NPC_POS', '_GET_NPC_ROT', '_GET_CHARA_POS', '_GET_CAMERA_POS', '_GET_CAMERA_REF', '_GET_OBJHDL_POS', '_GET_OBJHDL_ROT', '_GET_EDIT_PARTS_POS'):
            for i in range(1, n): S(i, 0.0)
        elif name in ('_GET_CAMERA_ANGLE',):
            for i in range(n): S(i, 0.0)


def main():
    path = sys.argv[1]
    fid = int(sys.argv[2])
    maxf = int(sys.argv[3]) if len(sys.argv) > 3 else 400
    names = eu.event_names()
    data = open(os.path.join(eu.DATA, path), 'rb').read()
    host = Host(names, {}, {})
    host.talk_info = 0
    host.place = 0
    for a in sys.argv[4:]:
        k, v = a.split('=')
        if k == 'info': host.talk_info = int(v)
        elif k == 'place': host.place = int(v)
        elif k == 'flag': pass
    vm = VM(data, host, names)
    r = vm.run(fid)
    frames = 0
    while r == 1 and frames < maxf:
        r = vm.resume(); frames += 1
        host.log.append('-- frame %d' % frames)
    print('result', r, 'frames', frames)
    # compress repeated frame markers
    out = []
    for l in host.log:
        if l.startswith('-- frame') and out and out[-1].startswith('-- frame'):
            out[-1] = l
        else:
            out.append(l)
    print('\n'.join(out))

if __name__ == '__main__':
    main()
