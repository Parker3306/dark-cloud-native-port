"""Checks the water-gauge drain (CUserStatus::Step) of the port against the live game: freezes the game with the hook GATE, steps N frames with step tokens and compares the
current water value of the character with the arithmetic of the port (EE adder with one guard bit, float32).
usage: validate_thirst.py [frames]        Needs PCSX2 in a dungeon with the hook installed (tools/dc/reset_game.sh)."""
import math
import os
import struct
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave, PHASES

f32 = np.float32


def rz(v):
    r = f32(v)
    if abs(float(r)) > abs(v):
        r = np.nextafter(r, f32(0))
    return r


def ee_add(a, b):
    a, b = float(a), float(b)
    if a == 0.0:
        return f32(b)
    if b == 0.0:
        return f32(a)
    big, small = (a, b) if abs(a) >= abs(b) else (b, a)
    e = math.frexp(big)[1] - 1
    grid = 2.0 ** (e - 24)
    cut = math.copysign(math.floor(abs(small) / grid) * grid, small)
    return rz(big + cut)


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 600
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        ustat = p.read(0x002A3468)
        gate = PHASES['update']['mailbox'] + 8
        hb_addr = PHASES['update']['mailbox'] + 4
        p.write(gate + 4, 0)
        p.write(gate, 1)
        h0 = p.read(hb_addr)
        for _ in range(200):
            time.sleep(0.01)
            if p.read(hb_addr) != h0:
                break
        time.sleep(0.3)
        ch = struct.unpack('<b', p.read_bytes(ustat + 4, 1))[0]
        dungeon = struct.unpack('<b', p.read_bytes(ustat + 0, 1))[0]
        flags = struct.unpack('<H', p.read_bytes(ustat + 0x450c + ch * 0xaa8 + struct.unpack('<b', p.read_bytes(ustat + 0x4340 + ch, 1))[0] * 0xf8 + 0xee, 2))[0]
        old8b08 = p.read(ustat + 0x8b08)
        p.write(ustat + 0x8b08, 0)      # the test state has the drain switched off (+0x8b08 = 1)
        cur0 = struct.unpack('<f', p.read_bytes(ustat + 0x4304 + ch * 4, 4))[0]
        print('character', ch, 'dungeon', dungeon, 'weapon flags', hex(flags), 'water', cur0)
        drain = rz(float(f32(0.003)) * float(ee_add(rz(float(f32(0.2)) * dungeon), f32(1.0))))
        if flags & 8:
            drain = rz(float(drain) * float(f32(0.8)))
        if flags & 0x10:
            drain = rz(float(drain) * 2.0)
        mine = f32(cur0)
        for _ in range(frames):
            hb = p.read(hb_addr)
            p.write(gate + 4, 1)
            for _ in range(400):
                if p.read(hb_addr) != hb:
                    break
                time.sleep(0.002)
            mine = ee_add(mine, -float(drain)) if mine > 0 else f32(0)
        time.sleep(0.2)
        live = struct.unpack('<f', p.read_bytes(ustat + 0x4304 + ch * 4, 4))[0]
        p.write(gate, 0)
        p.write(ustat + 0x8b08, old8b08)
        print('after', frames, 'frames: live %.9g  port %.9g  difference %.3g' % (live, float(mine), live - float(mine)))


if __name__ == '__main__':
    main()
