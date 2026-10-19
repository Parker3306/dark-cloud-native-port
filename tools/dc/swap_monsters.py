"""Replaces the monsters of the live floor by the given monster ids (rows of MonstorTable): rewrites the enemy layout row of (dungeon, floor) in RAM, calls the game's own
BtLoadMonstor (loads the models, scripts and shot effects from the disc) and BtArrengeMonstor. Used by the shot / ranged-attack rigs.
usage: swap_monsters.py <srand seed> <id> [<id> ...]      Needs PCSX2 in a dungeon with the hook installed."""
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'coop'))
from pine import Pine
from hooks import Cave, PHASES

BT_LOAD, BT_ARRENGE, SRAND = 0x01DB9330, 0x01DB92E0, 0x001046E8
LAYOUT_LIST = 0x002917B0   # BtEnemyLayoutList: [map] -> rows of 0x70 bytes (9 slots of {kind, monster, weight})
SELECT_MAP, USER_STATUS = 0x002A3594, 0x002A3468


def swap(p, cave, seed, ids):
    mapno = p.read(SELECT_MAP)
    us = p.read(USER_STATUS)
    floor = p.read_bytes(us, 4)[2]
    row = p.read(LAYOUT_LIST + 4 * mapno) + floor * 0x70
    for k in range(9):
        monster = ids[k] if k < len(ids) else -1
        p.write(row + k * 12, 0)
        p.write(row + k * 12 + 4, monster & 0xFFFFFFFF)
        p.write(row + k * 12 + 8, 100)
    cave.call_batch([dict(func=BT_LOAD, a=(0,))], timeout=60.0)
    cave.call_batch([dict(func=SRAND, a=(seed,))])
    cave.call_batch([dict(func=BT_ARRENGE)], timeout=20.0)
    return mapno, floor


def main():
    seed = int(sys.argv[1])
    ids = [int(x) for x in sys.argv[2:]]
    with Pine() as p:
        cave = Cave(p)
        if not cave.installed():
            cave.install()
        gate = PHASES['update']['mailbox'] + 8
        hb_addr = PHASES['update']['mailbox'] + 4
        p.write(gate + 4, 0)
        p.write(gate, 1)
        h0 = p.read(hb_addr)
        for _ in range(200):
            time.sleep(0.01)
            if p.read(hb_addr) != h0:
                break
        time.sleep(0.2)
        print('map/floor', swap(p, cave, seed, ids))
        unit = p.read(0x002A34D0)
        for k in range(16):
            base = unit + 0x1e3d0 + k * 400
            alive = p.read(base)
            if alive != -1 and alive & 0xFFFFFFFF != 0xFFFFFFFF:
                pos = struct.unpack('<3f', p.read_bytes(unit + 0x1fcd0 + k * 0x3510 + 0x220 - 0x210, 12)) if False else None
                print('slot', k, 'state word', hex(alive & 0xFFFFFFFF))
        p.write(gate, 0)


if __name__ == '__main__':
    main()
