"""Record the stack garbage PresetSmallItemNo_Get (0x001BFEF0) reads in the real call chain.
The function copies the rate table of the map (190 shorts) into a local array and then indexes it with item ids up to 400: the ids above 190 read whatever the stack held before (a game bug,
notes/native-plan.md 'Floor events'). This probe replaces every `jal PresetSmallItemNo_Get` by a tail-jump stub that first copies the 0x600 bytes below the caller's stack pointer
(the callee's frame) and the four arguments into a buffer, then runs a whole BtCleatRandomMap (srand(s0) first) and prints the rate slots (ids 191..400) of every call.
usage: probe_preset.py [s0]    Needs PCSX2 in a dungeon (tools/dc/reset_game.sh)."""
import os, struct, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave, PHASES
from mips import assemble
sys.path.insert(0, HERE)
import liverig

PRESET, SRAND, CLEAR_RANDOM_MAP = 0x001BFEF0, 0x001046E8, 0x01DB8790
PROBE, STATE, BUF = 0x01F62000, 0x01F62F00, 0x01F30000
SNAP = 0x600


def find_sites(p):
    word = 0x0C000000 | (PRESET >> 2)
    sites = []
    for base in range(0x001B0000, 0x001E0000, 0x1000):
        data = p.read_bytes(base, 0x1000)
        for i in range(0, 0x1000, 4):
            if struct.unpack_from('<I', data, i)[0] == word:
                sites.append(base + i)
    return sites


def install(p, sites):
    code = assemble('''
        li    t8, %#x
        lw    t0, 0(t8)
        sw    a0, 0(t0)
        sw    a1, 4(t0)
        sw    a2, 8(t0)
        sw    a3, 12(t0)
        addiu t0, t0, 16
        addiu t1, sp, -%#x
        addiu t2, sp, 0
    loop:
        lw    t3, 0(t1)
        sw    t3, 0(t0)
        addiu t1, t1, 4
        bne   t1, t2, loop
        addiu t0, t0, 4
        sw    t0, 0(t8)
        j     %#x
        nop
    ''' % (STATE, SNAP, PRESET), PROBE)
    p.write_bytes(PROBE, code)
    originals = {s: p.read(s) for s in sites}
    for s in sites:
        p.write(s, 0x0C000000 | (PROBE >> 2))
    return originals


def main():
    s0 = int(sys.argv[1]) if len(sys.argv) > 1 else 255808013
    with Pine() as p:
        if '--dungeon' in sys.argv:
            liverig.reload_state(p)
            d, f = int(sys.argv[sys.argv.index('--dungeon') + 1]), int(sys.argv[sys.argv.index('--floor') + 1])
            print('entered', liverig.enter_floor(p, d, f))
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        gate = PHASES['update']['mailbox'] + 8
        p.write(gate + 4, 0)
        p.write(gate, 1)
        time.sleep(0.5)
        sites = find_sites(p)
        print('call sites:', [hex(s) for s in sites])
        p.write(STATE, BUF, 4)
        p.write_bytes(BUF, bytes(16 * (SNAP + 16)))
        originals = install(p, sites)
        try:
            for c in (dict(func=SRAND, a=(s0,)), dict(func=CLEAR_RANDOM_MAP)):
                cave.call_batch([c], timeout=60.0)
        finally:
            for s, w in originals.items():
                p.write(s, w, 4)
        end = p.read(STATE)
        n = (end - BUF) // (SNAP + 16)
        print('%d calls recorded' % n)
        for k in range(n):
            blob = p.read_bytes(BUF + k * (SNAP + 16), SNAP + 16)
            a = struct.unpack_from('<4i', blob, 0)
            stack = blob[16:]
            # entry sp = end of the snapshot; rate(id) is at entry_sp - 0x322 + 2 * id
            rates = {}
            for ident in range(191, 401):
                off = SNAP - 0x322 + 2 * ident
                if 0 <= off <= SNAP - 2:
                    rates[ident] = struct.unpack_from('<h', stack, off)[0]
            if '--json' in sys.argv:
                import json
                with open(sys.argv[sys.argv.index('--json') + 1], 'a') as jf:
                    jf.write(json.dumps({'s0': s0, 'dungeon': sys.argv[sys.argv.index('--dungeon') + 1] if '--dungeon' in sys.argv else None, 'call': k, 'args': a, 'rates': [rates.get(i, 0) for i in range(191, 401)]}) + chr(10))
                continue
            nonzero = {i: v for i, v in rates.items() if v}
            if '--ids' in sys.argv:
                print('call %2d args %s ids191-256: %s' % (k, a, [rates[i] for i in range(191, 257)]))
                continue
            print('call %2d args %s: %d of 210 rate slots (ids 191..400) are not zero: %s' % (k, a, len(nonzero), dict(list(nonzero.items())[:24])))
        p.write(gate, 0)


main()
