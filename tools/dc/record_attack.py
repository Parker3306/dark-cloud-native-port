"""Records the live game while Toan swings his sword: presses the OK button at given ticks (the CGamePad On/Down getters are patched to read RAM masks), and saves per tick the
player state, the attack variables, the hit boxes in the pool and the monsters (work/attack_rec.json).
usage: record_attack.py [ticks] [arrange_seed] [--near SLOT] [--dist D] [--presses t,t,...] [--hold N] [--yaw A]"""
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from validate_monster import *   # noqa: F401,F403
import validate_monster as vm
from validate_hurt import player_state, cam_state, PLAYER_CHAR
from mips import assemble

NL = chr(10)
GETTERS = {'LX': 0x0012B310, 'LY': 0x0012B340, 'RX': 0x0012B2B0, 'RY': 0x0012B2E0}   # CGamePad::GetLX / LY / RX / RY, patched to read RAM
STICK = 0x01F7B100
STICK_SLOT = {'LX': 0, 'LY': 4, 'RX': 8, 'RY': 12}


def axis_cal(raw):
    v = raw - 0x80
    if v < 0x32 and -0x32 < v:
        return 0
    if v < 1:
        n = (raw - 0x4e) * 0x80
        return -((-n) // 0x4e) if n < 0 else n // 0x4e
    return ((raw - 0xb1) * 0x80) // 0x4e


def install_sticks(p):
    for key, addr in GETTERS.items():
        code = assemble('li v0, %#x' % (STICK + STICK_SLOT[key]) + NL + ' lw v0, 0(v0)' + NL + ' jr ra' + NL + ' nop' + NL, addr)
        p.write_bytes(STICK + STICK_SLOT[key], struct.pack('<i', 0))
        p.write_bytes(addr, code)


def parse_sticks(spec):
    # 't0-t1:lx:ly,...' raw stick bytes (128 = centre) held from tick t0 to t1
    out = []
    for part in (spec or '').split(','):
        if not part:
            continue
        rng, lx, ly = part.split(':')
        a, b = rng.split('-')
        out.append((int(a), int(b), int(lx), int(ly)))
    return out
PAD_ON_FN, PAD_DOWN_FN, PAD_OK = 0x0012B810, 0x0012B870, 0x002A348C
MASKS = 0x01F7B010          # [0] = buttons held, [4] = buttons newly pressed this frame
ATTACK_VARS = dict(gauge=(0x01DC44C8, 'f'), cnt4578=(0x01DC4578, 'i'), cnt4560=(0x01DC4560, 'i'), flag4568=(0x01DC4568, 'i'), flag4570=(0x01DC4570, 'i'), rot4574=(0x01DC4574, 'f'),
                   rel4498=(0x01DC4498, 'i'), charge449c=(0x01DC449c, 'f'), cd45cc=(0x01DC45CC, 'h'), hit45ca=(0x01DC45CA, 'h'), lunge=(0x01DC4590, 'f'), lunge_dec=(0x01DC4594, 'f'),
                   lunge_vec=(0x01DC4580, '3f'), canact=(0x01DC44E4, 'h'), flag44dc=(0x01DC44DC, 'i'), hold44f0=(0x01DC44F0, 'i'), busy=(0x01DC44A4, 'i'), cd45c8=(0x01DC45C8, 'h'),
                   gauge_full=(0x01DC44CC, 'i'), vy=(0x01DC2554, 'f'), velo=(0x01DC2550, 'f'), velo_z=(0x01DC2558, 'f'), stick=(0x01DC453C, 'f'), leap_v=(0x01DC44B0, '3f'), leap_on=(0x01DC44C0, 'i'), hgt=(0x01DC44C4, 'f'), dirx=(0x01DC4534, 'f'), dirz=(0x01DC4538, 'f'))


def read_var(p, spec):
    addr, kind = spec
    if kind == 'f':
        return f32(p, addr)
    if kind == 'i':
        return i32(p, addr)
    if kind == 'h':
        return struct.unpack('<h', p.read_bytes(addr, 2))[0]
    return list(struct.unpack('<' + kind, p.read_bytes(addr, struct.calcsize('<' + kind))))


def install_pad(p):
    lui_lw = lambda base: assemble('li v0, %#x\n lw v0, 0(v0)\n' % base, 0)
    def words(*w):
        return b''.join(struct.pack('<I', x) for x in w)
    and_v0_v0_a1 = (2 << 21) | (5 << 16) | (2 << 11) | 0x24
    and_a1_v0 = and_v0_v0_a1
    sltu_v0_zero_v0 = (0 << 21) | (2 << 16) | (2 << 11) | 0x2B
    jr_ra = 0x03E00008
    for fn, off in ((PAD_ON_FN, 0), (PAD_DOWN_FN, 4)):
        code = assemble('li v0, %#x\n lw v0, 0(v0)\n' % (MASKS + off), fn) + words(and_v0_v0_a1, sltu_v0_zero_v0, jr_ra, 0)
        p.write_bytes(fn, code)
    p.write_bytes(MASKS, struct.pack('<2I', 0, 0))


SET_FN, RING, STUB = 0x001B57A0, 0x01F7C000, 0x01F7C000 + 0x4010


def install_set_hook(p):
    """Logs every CCollisionData::Set call (pos, damage, radius, caller, target, kind, life) into a RAM ring: the sword's hit boxes live only inside one frame."""
    def w(*x):
        return b''.join(struct.pack('<I', v) for v in x)
    SLL_T6_T5_6 = (13 << 16) | (14 << 11) | (6 << 6)
    code = assemble('''
        li t4, %#x
        lw t5, 0(t4)
    ''' % RING, STUB) + w(SLL_T6_T5_6)
    code += assemble('''
        addu t6, t6, t4
        addiu t6, t6, 16
        lw t7, 0(a1)
        sw t7, 0(t6)
        lw t7, 4(a1)
        sw t7, 4(t6)
        lw t7, 8(a1)
        sw t7, 8(t6)
        lw t7, 12(a1)
        sw t7, 12(t6)
        sw a2, 16(t6)
        swc1 f12, 20(t6)
        sw ra, 24(t6)
        sw t0, 28(t6)
        sw t1, 32(t6)
        sw a3, 36(t6)
        sw t2, 40(t6)
        sw t3, 44(t6)
        addiu t5, t5, 1
        sw t5, 0(t4)
    ''', STUB + len(code))
    code += w(0x7FBF00A0)   # sq ra,0xa0(sp): the instruction the hook jump replaced
    code += assemble('j %#x' % (SET_FN + 8) + chr(10) + ' nop' + chr(10), STUB + len(code))
    p.write_bytes(RING, bytes(16))
    p.write_bytes(STUB, code)
    # [0x1b57a0] = j stub ; [0x1b57a4] = addiu sp,sp,-0xb0 (the original first instruction, now in the delay slot)
    p.write_bytes(SET_FN, struct.pack('<2I', 0x08000000 | ((STUB >> 2) & 0x3FFFFFF), 0x27BDFF50))


def read_set_log(p, seen):
    n = p.read(RING)
    out = []
    for i in range(0, n):
        b = p.read_bytes(RING + 16 + i * 64, 48)
        pos = struct.unpack_from('<4f', b, 0)
        dmg, rad, ra, tgt, kind, life, t2, t3 = struct.unpack_from('<ifIiiiii', b, 16)
        out.append(dict(pos=pos, damage=dmg, radius=rad, ra=ra, target=tgt, kind=kind, life=life, t2=t2, t3=t3))
    p.write(RING, 0)   # the ring is small: start over every tick
    return out, 0


EXT_FN, EXT_RING, EXT_STUB = 0x0023DD00, 0x01F90000, 0x01F91000


def install_ext_hook(p):
    """Logs every CRunScript::ext call (monster index, function id, argument slots) into a RAM ring that the caller empties each tick."""
    def w(*x):
        return b''.join(struct.pack('<I', v) for v in x)
    SLL_T6_T5_5 = (13 << 16) | (14 << 11) | (5 << 6)           # t6 = t5 << 5
    SLL_T8_T5_6 = (13 << 16) | (24 << 11) | (6 << 6)           # t8 = t5 << 6
    lines = ['li t4, %#x' % EXT_RING, 'lw t5, 0(t4)']
    code = assemble(NL.join(lines) + NL, EXT_STUB) + w(SLL_T8_T5_6, SLL_T6_T5_5)   # t8 = n*64, t6 = n*32  (entry stride 96 = 64 + 32)
    body = ['addu t6, t6, t8', 'addu t6, t6, t4', 'addiu t6, t6, 16',
            'li t7, 0x2a34d0', 'lw t7, 0(t7)', 'lw t7, 0x90(t7)', 'sw t7, 0(t6)', 'lw t7, 4(a1)', 'sw t7, 4(t6)', 'sw a2, 8(t6)']
    for i in range(9):
        body += ['lw t7, %d(a1)' % (8 + 8 * i), 'sw t7, %d(t6)' % (12 + 8 * i), 'lw t7, %d(a1)' % (12 + 8 * i), 'sw t7, %d(t6)' % (16 + 8 * i)]
    body += ['addiu t5, t5, 1', 'sw t5, 0(t4)']
    code += assemble(NL.join(body) + NL, EXT_STUB + len(code))
    code += w(0x7FBF0010)   # sq ra,0x10(sp)
    code += assemble('j %#x' % (EXT_FN + 8) + NL + ' nop' + NL, EXT_STUB + len(code))
    p.write_bytes(EXT_RING, bytes(16))
    p.write_bytes(EXT_STUB, code)
    p.write_bytes(EXT_FN, struct.pack('<2I', 0x08000000 | ((EXT_STUB >> 2) & 0x3FFFFFF), 0x27BDFFE0))


def read_ext_log(p):
    n = p.read(EXT_RING)
    out = []
    for i in range(n):
        b = p.read_bytes(EXT_RING + 16 + i * 96, 96)
        words = struct.unpack('<24I', b)
        out.append(dict(mon=words[0], id=words[1], n=words[2], slots=[(words[3 + 2 * k], words[4 + 2 * k]) for k in range(9)]))
    p.write(EXT_RING, 0)
    return out


SELECT_CHR_UNIT = 0x001D1030
DEF_WEAPON = (257, 299, 314, 331, 347, 363)


def switch_character(p, cave, us, ch, weapon=0):
    """Puts character `ch` into the live record (their first weapon cloned from the current weapon record, full life) and calls selectChrUnit(ch, 1)."""
    cur = struct.unpack('<b', p.read_bytes(us + 4, 1))[0]
    if cur != ch:
        src = us + 0x450c + cur * 0xaa8 + struct.unpack('<b', p.read_bytes(us + 0x4340 + cur, 1))[0] * 0xf8
        rec = bytearray(p.read_bytes(src, 0xf8))
        struct.pack_into('<h', rec, 0, weapon or DEF_WEAPON[ch] + 1)
        struct.pack_into('<h', rec, 0xc, 99)       # a weapon that does not wear out during the test
        struct.pack_into('<f', rec, 0x10, 99.0)
        p.write_bytes(us + 0x450c + ch * 0xaa8, bytes(rec))
        p.write_bytes(us + 0x4340 + ch, bytes(1))
        mx = struct.unpack('<h', p.read_bytes(us + 6 + ch * 2, 2))[0]
        p.write_bytes(us + 0x12 + ch * 2, struct.pack('<h', mx if mx > 0 else 100))
    cave.call_batch([dict(func=SELECT_CHR_UNIT, a=(ch, 1))], timeout=60.0)


def main():
    args = sys.argv[1:]
    opts = {}
    for key in ('--near', '--dist', '--presses', '--hold', '--yaw', '--bring', '--stick', '--monsters', '--character', '--poke', '--weapon', '--btn'):
        if key in args:
            i = args.index(key)
            opts[key] = args[i + 1]
            del args[i:i + 2]
    ticks = int(args[0]) if args else 200
    aseed = int(args[1], 0) if len(args) > 1 else 12345
    dist = float(opts.get('--dist', 30))
    presses = [int(x) for x in opts.get('--presses', '10').split(',')]
    hold = int(opts.get('--hold', 3))
    with Pine() as p:
        p.load_state(1)
        time.sleep(6)
        cave = Cave(p)
        cave.uninstall()
        time.sleep(0.3)
        cave.install()
        time.sleep(1)
        gate = PHASES['update']['mailbox'] + 8
        hb_addr = PHASES['update']['mailbox'] + 4
        p.write(gate + 4, 0)
        p.write(gate, 1)
        h0 = p.read(hb_addr)
        for _ in range(200):
            time.sleep(0.01)
            if p.read(hb_addr) != h0:
                break
        time.sleep(0.1)
        ok_mask = p.read(PAD_OK)
        dng = p.read(NOW_DNG_MAP)
        seed, rooms, mapno = p.read(dng + 0x468), p.read(dng + 0xb650), p.read(SELECT_MAP)
        unit = p.read(NOW_MONSTOR)
        us = p.read(USER_STATUS)
        floor = p.read_bytes(us, 4)[2]
        monster_ids = [int(x) for x in opts['--monsters'].split(',')] if '--monsters' in opts else None
        if monster_ids:   # a different set of monsters than the floor's (loads their models, scripts and shot effects through the game's own BtLoadMonstor)
            import swap_monsters
            swap_monsters.swap(p, cave, aseed, monster_ids)
        for item in (opts.get('--poke') or '').split(','):   # UserStatus offset=value (32 bit), e.g. 0x4324=1
            if item:
                off, val = item.split('=')
                p.write(us + int(off, 0), int(val, 0) & 0xFFFFFFFF)
        if '--character' in opts:   # play another character: gives them their first weapon in the live record, then lets the game's own selectChrUnit load the model / weapon
            switch_character(p, cave, us, int(opts['--character']), int(opts['--weapon'], 0) if '--weapon' in opts else 0)
        install_probe(p)
        cave.call_batch([dict(func=SRAND, a=(aseed,))])
        cave.call_batch([dict(func=BT_ARRENGE)], timeout=20.0)
        slots0 = [slot_state(p, unit, k) for k in range(16)]
        live = [k for k in range(16) if slots0[k]['state'] != -1]
        target = int(opts['--near']) if '--near' in opts else live[0]
        tp = slots0[target]['pos']
        fr = p.read(CHARA_FRAME)
        bring = None
        if '--bring' in opts:   # fetch a monster to the player's start position instead of walking the player to it (far away the game does not refresh its collision polygons for a while)
            k = int(opts['--bring'])
            target = k
            ppos = struct.unpack('<3f', p.read_bytes(fr + 0x220, 12))
            bring = (k, ppos[0], ppos[1], ppos[2] - dist)
            p.write_bytes(unit + 0x1fcd0 + k * 0x3510 + 0x10, struct.pack('<3f', *bring[1:]))
        else:
            for addr in (fr + 0x220, PLAYER_VEC):
                p.write_bytes(addr, struct.pack('<3f', tp[0], tp[1], tp[2] + dist))   # he faces -z
        status_file = os.path.join(WORK, 'user_status.bin')
        open(status_file, 'wb').write(b''.join(p.read_bytes(us + off, 0x400) for off in range(0, 0x9000, 0x400)))
        install_pad(p)
        install_sticks(p)
        sticks = parse_sticks(opts.get('--stick'))
        install_set_hook(p)
        install_ext_hook(p)
        set_seen = 0
        pool_base = p.read(0x002A35E0)
        rs = lambda: p.read(p.read(IMPURE) + 0x58)
        rec = dict(btn=opts.get('--btn'), monsters=monster_ids, sticks=sticks, bring=bring, ok_mask=ok_mask, target=target, aseed=aseed, seed=seed, rooms=rooms, mapno=mapno, floor=floor, presses=presses, hold=hold, start=player_state(p, us, fr),
                   cam=cam_state(p), motion_table=[(i32(p, p.read(PLAYER_CHAR + 0x344) + i * 0x10), i32(p, p.read(PLAYER_CHAR + 0x344) + i * 0x10 + 4), f32(p, p.read(PLAYER_CHAR + 0x344) + i * 0x10 + 8)) for i in range(41)],
                   start_vars={k: read_var(p, v) for k, v in ATTACK_VARS.items()}, ticks=[],
                   statics={hex(a): i32(p, a) for a in (0x2a2b7c, 0x2a36a0, 0x2a36a4, 0x2a36ac, 0x2a36b4, 0x2a36b8)}, motion_state=[f32(p, PLAYER_CHAR + 0x2f0), f32(p, PLAYER_CHAR + 0x2f4), f32(p, PLAYER_CHAR + 0x2f8), i32(p, PLAYER_CHAR + 0x2fc), i32(p, PLAYER_CHAR + 0x300),
                                 i32(p, PLAYER_CHAR + 0x304), i32(p, PLAYER_CHAR + 0x308), i32(p, PLAYER_CHAR + 0x30c)], vy=f32(p, 0x01DC2554),
                   slots_start=[slot_state(p, unit, k) for k in range(16)])
        last_count = p.read(PROBE_OUT + 8)
        feed_prev = rs()
        for t in range(ticks):
            on = 0
            down = 0
            for pt in presses:
                if pt <= t < pt + hold:
                    on |= ok_mask
                    if t == pt:
                        down |= ok_mask
            for item in (opts.get('--btn') or '').split(','):   # t0-t1:mask: other pad buttons held in these ticks
                if item:
                    rng_, mk = item.split(':')
                    b0, b1 = rng_.split('-')
                    if int(b0) <= t <= int(b1):
                        on |= int(mk, 0)
                        if t == int(b0):
                            down |= int(mk, 0)
            p.write_bytes(MASKS, struct.pack('<2I', on, down))
            lx, ly = 128, 128
            for a, b, sx, sy in sticks:
                if a <= t <= b:
                    lx, ly = sx, sy
            p.write_bytes(STICK + STICK_SLOT['LX'], struct.pack('<I', axis_cal(lx) & 0xFFFFFFFF))
            p.write_bytes(STICK + STICK_SLOT['LY'], struct.pack('<I', axis_cal(ly) & 0xFFFFFFFF))
            hb = p.read(hb_addr)
            p.write(gate + 4, 1)
            for _ in range(2500 if t < 3 else 600):
                if p.read(hb_addr) != hb:
                    break
                time.sleep(0.002)
            else:
                if t > 10:
                    print('the game stopped advancing at tick %d (an event or a menu took over?); keeping what was recorded' % t)
                    break
                raise RuntimeError('the game did not advance a frame at tick %d' % t)
            r_step, hold_arg, count, r_exit = p.read(PROBE_OUT), p.read(PROBE_OUT + 4), p.read(PROBE_OUT + 8), p.read(PROBE_OUT + 12)
            fed = (r_step, hold_arg, r_exit) if count != last_count else (feed_prev, 1, feed_prev)
            last_count = count
            feed_prev = rs()
            entry = dict(t=t, on=on, down=down, rand=fed, player=player_state(p, us, fr), vars={k: read_var(p, v) for k, v in ATTACK_VARS.items()},
                         monsters=[slot_state(p, unit, k) for k in range(16)])
            sph = {}
            for k in range(16):
                cnt = p.read(unit + k * 0x160 + 0x5d9a0)
                if cnt and 0 < cnt < 16:
                    blob = p.read_bytes(unit + k * 0x160 + 0x5d850, 0x160)
                    sph[k] = [(struct.unpack_from('<3f', blob, i * 16), struct.unpack_from('<f', blob, 0xf0 + i * 4)[0]) for i in range(cnt)]
            entry['msph'] = sph
            bb = unit + target * 0x510
            bpos = p.read_bytes(bb + 0x55250, 256)
            brad = struct.unpack('<16f', p.read_bytes(bb + 0x55390, 64))
            bact = struct.unpack('<16i', p.read_bytes(bb + 0x55450, 64))
            entry['bcols'] = [(k, list(struct.unpack_from('<3f', bpos, k * 16)), brad[k]) for k in range(16) if bact[k]]
            flags = p.read_bytes(pool_base + 0x3c00, 96 * 4)
            pool = []
            for i in range(96):
                if struct.unpack_from('<i', flags, i * 4)[0]:
                    b = p.read_bytes(pool_base + i * 0xa0, 0xa0)
                    pool.append(dict(i=i, raw=b.hex()))
            entry['pool'] = pool
            fxb = p.read(0x002A35D8)
            fx = []
            for slot_i in range(5):
                sb = fxb + slot_i * 0xa160
                if p.read(sb) == 0:
                    continue
                for ii in range(8):
                    if struct.unpack('<h', p.read_bytes(sb + 0xa000 + 2 * ii, 2))[0]:
                        base_i = sb + ii * 0x46c * 4
                        fx.append(dict(slot=slot_i, i=ii, phase=struct.unpack('<h', p.read_bytes(sb + 0x9ff0 + 2 * ii, 2))[0], time=f32(p, base_i + 0x52c * 4), mot=i32(p, base_i + 0x78a * 4),
                                       mode=i32(p, base_i + 0x789 * 4), life=i32(p, sb + (0x27f4 + ii) * 4), vel=list(struct.unpack('<3f', p.read_bytes(sb + (0x27d0 + ii * 4) * 4, 12)))))
            entry['fx'] = fx
            pfx = []
            sb = 0x01E8DA60   # CharaMainEffect: the character's own effect object
            for ii in range(8):
                if struct.unpack('<h', p.read_bytes(sb + 0xa000 + 2 * ii, 2))[0]:
                    base_i = sb + ii * 0x46c * 4
                    pfx.append(dict(i=ii, phase=struct.unpack('<h', p.read_bytes(sb + 0x9ff0 + 2 * ii, 2))[0], time=f32(p, base_i + 0x52c * 4), life=i32(p, sb + (0x27f4 + ii) * 4),
                                    vel=list(struct.unpack('<3f', p.read_bytes(sb + (0x27d0 + ii * 4) * 4, 12))), cd=i32(p, sb + (0x283c + ii) * 4), wait=p.read_bytes(sb + 0xa140 + ii, 1)[0],
                                    pos=list(struct.unpack('<3f', p.read_bytes(base_i + 0x470 * 4 + 0x220, 12)))))
            entry['pfx'] = pfx
            entry['sets'], set_seen = read_set_log(p, set_seen)
            entry['ext'] = read_ext_log(p)
            ri = p.read(0x2A34F8)
            drops = []
            for i in range(32):
                marker = i32(p, ri + 0x290 + i * 4)
                if marker != -1:
                    pos = struct.unpack('<3f', p.read_bytes(ri + 0x10 + i * 16, 12))
                    drops.append(dict(i=i, pos=pos, gold=i32(p, ri + 0x310 + i * 4), item=i32(p, ri + 0x594 + i * 4), state=i32(p, ri + 0x390 + i * 4), timer=f32(p, ri + 0x410 + i * 4), block=i32(p, ri + 0x514 + i * 4)))
            entry['drops'] = drops
            entry['gold'] = struct.unpack('<H', p.read_bytes(us + 0x4346, 2))[0]
            entry['wexp'] = struct.unpack('<h', p.read_bytes(us + 0x450c + 0x14, 2))[0]
            entry['bag0'] = struct.unpack('<h', p.read_bytes(us + 0x436e, 2))[0]
            rec['ticks'].append(entry)
        p.write(gate, 0)
        # restore the pad functions: reload of the state is needed anyway before the next run
    json.dump(rec, open(os.path.join(WORK, 'attack_rec.json'), 'w'))
    print('recorded', len(rec['ticks']), 'ticks; target slot', target, 'player', rec['start']['pos'])


if __name__ == '__main__':
    main()
