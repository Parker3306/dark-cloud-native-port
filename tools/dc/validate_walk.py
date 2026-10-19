"""Lock-step comparison of the C++ walker (player movement + follow camera) with the running game.
usage: validate_walk.py <scenario> [ticks]      scenarios: up, circle, wall, cam, mixed
The live game (dungeon, free-move, hooks installable) is paused at the end of a frame with the hook's GATE, its sticks are replaced by injected values
(the four CGamePad stick getters are patched to read RAM), and each tick is released one at a time while the player and camera state are recorded.
The same inputs are then run through native/build/core_release/dcwalk.exe starting from the recorded state and the trajectories are compared.
The patched getters are restored afterwards."""
import math
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
import hooks
from hooks import Cave, PHASES
from mips import assemble

ISO = os.path.join(HERE, '..', '..', 'iso', 'Dark Cloud (USA).iso')
EXE = os.path.join(HERE, '..', '..', 'native', 'build', 'core_release', 'dcwalk.exe')
NOW_CAMERA, CHARA_FRAME = 0x002A3498, 0x002A3500
REF_OFF, VY, SELECT_MAP, GAME_TASK = 0x01F068A0, 0x01DC2554, 0x002A3594, 0x002A35EC
GETTERS = {'LX': 0x0012B310, 'LY': 0x0012B340, 'RX': 0x0012B2B0, 'RY': 0x0012B2E0}
INJECT = 0x01F7B000   # LX, LY, RX, RY as ints
SLOT = {'LX': 0, 'LY': 4, 'RX': 8, 'RY': 12}


def f32(p, a):
    return struct.unpack('<f', struct.pack('<I', p.read(a)))[0]


def vec(p, a):
    return struct.unpack('<3f', p.read_bytes(a, 12))


def axis_cal(raw):
    v = raw - 0x80
    if v < 0x32 and -0x32 < v:
        return 0
    if v < 1:
        n = (raw - 0x4e) * 0x80
        return -((-n) // 0x4e) if n < 0 else n // 0x4e   # C division truncates toward zero
    return ((raw - 0xb1) * 0x80) // 0x4e


def snapshot(p):
    cam = p.read(NOW_CAMERA)
    fr = p.read(CHARA_FRAME)
    return dict(
        ppos=vec(p, fr + 0x220), pyaw=f32(p, fr + 0x234), pvy=f32(p, VY), pdisp=vec(p, REF_OFF),
        cpos=vec(p, cam + 0x260), cref=vec(p, cam + 0x270), cnext_pos=vec(p, cam + 0x280), cnext_ref=vec(p, cam + 0x290),
        cspeed=f32(p, cam + 0x2a8), cangle_h=f32(p, cam + 0x2ac), cfollow=vec(p, cam + 0x2c0), cdist=f32(p, cam + 0x2d0), cheight=f32(p, cam + 0x2d4),
        cangle=f32(p, cam + 0x2d8), cangle_cur=f32(p, cam + 0x2dc))


def scenario(name, n):
    out = []
    for t in range(n):
        lx, ly, rx, ry = 128, 128, 128, 128
        if name == 'up':
            ly = 0
        elif name == 'circle':
            a = t * 0.05
            lx, ly = int(128 + 127 * math.sin(a)), int(128 - 127 * math.cos(a))
        elif name == 'wall':
            ly = 0 if t < n // 2 else 255
        elif name == 'cam':
            rx = 255 if t % 120 < 60 else 0
            ly = 60 if t % 200 < 100 else 128
        elif name == 'mixed':
            a = t * 0.11
            lx, ly = int(128 + 127 * math.sin(a)), int(128 - 100 * math.cos(a * 0.7))
            rx = 128 + int(80 * math.sin(t * 0.03))
        out.append((max(0, min(255, lx)), max(0, min(255, ly)), max(0, min(255, rx)), max(0, min(255, ry)), 0))
    return out


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'up'
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    script = scenario(name, n)
    with Pine() as p:
        cave = Cave(p)
        cave.uninstall()
        time.sleep(0.3)
        cave.install()
        gate = PHASES['update']['mailbox'] + 8
        hb_addr = PHASES['update']['mailbox'] + 4
        # patch the four stick getters
        originals = {}
        for key, addr in GETTERS.items():
            originals[key] = p.read_bytes(addr, 20)
        for key, addr in GETTERS.items():
            code = assemble('li v0, %#x\n lw v0, 0(v0)\n jr ra\n nop\n' % (INJECT + SLOT[key]), addr)
            p.write_bytes(INJECT + SLOT[key], struct.pack('<i', 0))
            p.write_bytes(addr, code)
        try:
            # pause at the end of a frame
            p.write(gate + 4, 0)
            p.write(gate, 1)
            h0 = p.read(hb_addr)
            for _ in range(200):
                time.sleep(0.01)
                if p.read(hb_addr) != h0:
                    break
            time.sleep(0.1)
            start = snapshot(p)
            seed, rooms, mapno = p.read(p.read(0x002A34B8) + 0x468), p.read(p.read(0x002A34B8) + 0xb650), p.read(SELECT_MAP)
            states = []
            for t, (lx, ly, rx, ry, btn) in enumerate(script):
                for key, raw in (('LX', lx), ('LY', ly), ('RX', rx), ('RY', ry)):
                    p.write(INJECT + SLOT[key], axis_cal(raw) & 0xFFFFFFFF)
                hb = p.read(hb_addr)
                p.write(gate + 4, 1)      # one step token
                for _ in range(400):
                    if p.read(hb_addr) != hb:
                        break
                    time.sleep(0.002)
                else:
                    raise RuntimeError('the game did not advance a frame')
                states.append(snapshot(p))
        finally:
            for key, addr in GETTERS.items():
                p.write_bytes(addr, originals[key])
            p.write(gate, 0)
    state_file = os.path.join(HERE, '..', '..', 'work', 'walk_state.txt')
    with open(state_file, 'w') as f:
        for k, v in start.items():
            f.write('%s %s\n' % (k, ' '.join('%.9g' % x for x in (v if isinstance(v, tuple) else (v,)))))
    script_file = os.path.join(HERE, '..', '..', 'work', 'walk_script.txt')
    with open(script_file, 'w') as f:
        for s in script:
            f.write('%d %d %d %d %d\n' % s)
    out = subprocess.run([EXE, ISO, 'd01main_a', str(seed), str(rooms), str(mapno), '--state', state_file, script_file], capture_output=True, text=True).stdout.split(chr(10))
    worst = dict(ppos=0, cpos=0, cref=0)
    first_bad = None
    first_extra = None
    for t, line in enumerate(out):
        if not line.strip():
            continue
        parts = line.replace('|', ' ').split()
        mine = dict(ppos=tuple(map(float, parts[1:4])), cpos=tuple(map(float, parts[5:8])), cref=tuple(map(float, parts[8:11])))
        mine_extra = (float(parts[11]), float(parts[12]), float(parts[13]))   # dist, height, angle
        g0 = states[t]
        if first_extra is None and (abs(mine_extra[0] - g0['cdist']) > 1e-3 or abs(mine_extra[1] - g0['cheight']) > 1e-3 or abs(mine_extra[2] - g0['cangle']) > 1e-3):
            first_extra = (t, mine_extra, (g0['cdist'], g0['cheight'], g0['cangle']))
        g = states[t]
        for k in worst:
            d = max(abs(a - b) for a, b in zip(mine[k], g[k]))
            worst[k] = max(worst[k], d)
            if d > 0.05 and first_bad is None:
                first_bad = (t, k, d, mine[k], g[k])
    print('start', start['ppos'], 'cam', start['cpos'], 'end game', states[-1]['ppos'], states[-1]['cpos'], 'moved', max(abs(a - b) for a, b in zip(start['ppos'], states[-1]['ppos'])))
    print('scenario %s, %d ticks: worst difference player %.4f camera pos %.4f camera ref %.4f' % (name, len(states), worst['ppos'], worst['cpos'], worst['cref']))
    if first_extra:
        print('first camera parameter deviation at tick %d: port dist/height/angle %s, game %s' % first_extra)
        t = first_extra[0]
        for k in range(max(0, t - 2), t + 1):
            print('   tick %d game: dist %.4f height %.4f angle %.4f cam %s | player %s' % (k, states[k]['cdist'], states[k]['cheight'], states[k]['cangle'], states[k]['cpos'], states[k]['ppos']))
            print('   tick %d port: %s' % (k, out[k]))
    if first_bad:
        print('first deviation > 0.05 at tick %d (%s, %.3f): port %s game %s' % first_bad)
        t = first_bad[0]
        print('  game  tick %d: pos %s cam %s ref %s dist %.3f height %.3f angle %.4f' % (t, states[t]['ppos'], states[t]['cpos'], states[t]['cref'], states[t]['cdist'], states[t]['cheight'], states[t]['cangle']))
        print('  port  ', out[t])


main()
