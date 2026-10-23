"""Start a boss floor in the LIVE game (PCSX2) and record where the hero and the monsters are, to compare with the native port.
usage: boss_live.py <dungeon 0..6> <floor> [--flag a,b,..] [--int i=v,..] [--seconds N] [--shots out_prefix] [--poll S] [--then EVENT:SECONDS]
The game must be running in a dungeon in free-move (tools/dc/reset_game.sh loads save state 1 for that). The rig
  * sets the save flags / ints that the floor-start script reads (the key flags etc.),
  * sets UserStatus floor (+2) / old floor (+3) so that the script of the dungeon (function 100) builds the boss floor (floor = boss floor - 1: the script adds one),
  * starts the floor-start function through gameTask 0xde (reloads the dungeon's event script, then request 100 + gameTask 400),
  * then prints one line per poll: seconds, gameTask, hero position / yaw, and the monster units that are alive (name, position, hp).
The same numbers come from the port with `play.tscn --dungeon D --floor F --events 1 --shots T` (see notes/native-plan.md, "Live comparison").
Boss floors (0-based floor, the number to pass): d01 14, d02 16, d03 17, d04 17, d05 14, d06 24 (stage 1) / 25, d07 99."""
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine

USER_STATUS, SAVE_DATA, SELECT_MAP, GAME_TASK, CHARA_FRAME = 0x002A3468, 0x002A250C, 0x002A3594, 0x002A35EC, 0x002A3500
EVENT_REQUEST = 0x01D5676C            # DAT_01d5676c: the id of the event function to run (100 = the floor start)
GAME_FLAGS, GAME_INTS = 0x10268, 0x10668   # CSaveData offsets (savedata.h)
SHOT = os.path.join(HERE, '..', '..', 'scripts', 'shot_pcsx2.ps1')


def f3(p, a):
    return struct.unpack('<3f', p.read_bytes(a, 12))


NOW_MONSTOR = 0x002A34D0


def monsters(p, slots=16):
    """The living monster slots of the live game (layout as in validate_monster.py slot_state): (slot, state, hp, pos, motion id, motion time)."""
    unit = p.read(NOW_MONSTOR)
    out = []
    if not unit:
        return out
    for k in range(slots):
        U = unit + 0x1e3d0 + k * 400
        C = unit + 0x1fcd0 + k * 0x3510
        state = struct.unpack('<i', struct.pack('<I', p.read(U)))[0]
        hp = struct.unpack('<i', struct.pack('<I', p.read(U + 0x24)))[0]
        if state < 0 or hp < 1:
            continue
        out.append((k, state, hp, f3(p, C + 0x10), struct.unpack('<i', struct.pack('<I', p.read(C + 0x304)))[0], struct.unpack('<f', struct.pack('<I', p.read(C + 0x2f0)))[0]))
    return out


def set_flag(p, i, on=True):
    base = p.read(SAVE_DATA) + GAME_FLAGS + (i >> 5) * 4
    v = p.read(base)
    v = (v | (1 << (i & 31))) if on else (v & ~(1 << (i & 31)))
    p.write(base, v & 0xFFFFFFFF, 4)


def set_int(p, i, v):
    p.write(p.read(SAVE_DATA) + GAME_INTS + i * 4, v & 0xFFFFFFFF, 4)


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        return 1
    dungeon, floor = int(a[0]), int(a[1])
    flags, ints, seconds, poll, shots, then, tele = [], [], 60.0, 1.0, None, None, None
    i = 2
    while i < len(a):
        if a[i] == '--flag':
            flags = [int(x) for x in a[i + 1].split(',')]
            i += 2
        elif a[i] == '--int':
            ints = [tuple(int(y) for y in x.split('=')) for x in a[i + 1].split(',')]
            i += 2
        elif a[i] == '--seconds':
            seconds = float(a[i + 1])
            i += 2
        elif a[i] == '--poll':
            poll = float(a[i + 1])
            i += 2
        elif a[i] == '--then':   # --then 500:12: after 12 seconds post the event function 500 (the boss start) the way a marker does
            then = tuple(int(x) for x in a[i + 1].split(':'))
            i += 2
        elif a[i] == '--teleport':   # --teleport x,y,z:SECONDS: put the hero there after that many seconds (walking onto an auto marker starts its event the way the game does)
            xyz, sec = a[i + 1].split(':')
            tele = (tuple(float(v) for v in xyz.split(',')), float(sec))
            i += 2
        elif a[i] == '--shots':
            shots = a[i + 1]
            i += 2
        else:
            raise SystemExit('unknown option ' + a[i])
    with Pine(28011) as p:
        for f in flags:
            set_flag(p, f)
        for k, v in ints:
            set_int(p, k, v)
        us = p.read(USER_STATUS)
        p.write(us + 0, dungeon, 1)
        p.write(us + 2, floor - 1, 1)    # the script's _GET_FLOOR_LEVEL + 1 is the floor that gets built
        p.write(us + 3, floor - 1, 1)    # _GET_OLD_FLOOR_LEVEL != -1: not the entrance picker
        p.write(SELECT_MAP, dungeon, 4)
        # gameTask 0xde is the game's own "start a floor script" step: it reloads the event.stb of selectMapNo (BtSystemScriptLoad; without it the script of the dungeon that was running stays in
        # memory and the boss floors of other dungeons hang at a black screen), takes the function id from DAT_01d5648c (100 = the floor start) and the heavy flag from DAT_01d56490, then runs gameTask 400
        p.write(0x01D5648C, 100, 4)
        p.write(0x01D56490, 0, 4)
        p.write(GAME_TASK, 0xDE, 4)
        t0 = time.time()
        n = 0
        while time.time() - t0 < seconds:
            time.sleep(poll)
            if then and time.time() - t0 >= then[1]:
                p.write(EVENT_REQUEST, then[0], 4)
                p.write(GAME_TASK, 400, 4)
                print('        >>> event %d requested' % then[0], flush=True)
                then = None
            if tele and time.time() - t0 >= tele[1]:
                fr0 = p.read(CHARA_FRAME)
                for k in range(3):
                    p.write_float(fr0 + 0x220 + 4 * k, tele[0][k])
                print('        >>> hero put at', tele[0], flush=True)
                tele = None
            fr = p.read(CHARA_FRAME)
            pos = f3(p, fr + 0x220) if fr else (0, 0, 0)
            yaw = struct.unpack('<f', struct.pack('<I', p.read(fr + 0x234)))[0] if fr else 0
            task = p.read(GAME_TASK)
            print('%6.1fs task %4d hero (%.2f, %.2f, %.2f) yaw %.2f floor %d' % (time.time() - t0, task, pos[0], pos[1], pos[2], yaw, p.read_bytes(us + 2, 1)[0]), flush=True)
            for m in monsters(p):
                print('        mon %d state %d hp %d pos (%.1f, %.1f, %.1f) motion %d time %.1f' % (m[0], m[1], m[2], m[3][0], m[3][1], m[3][2], m[4], m[5]), flush=True)
            if shots:
                subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', SHOT, '%s_%03d.png' % (shots, n)], capture_output=True)
            n += 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
