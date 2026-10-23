"""Compare the C++ floor generator (dcdungeon.exe) with the dungeon that is live in PCSX2.
usage: dungeon_live.py [map_addr]   (default MainDungeonMap 0x01DC4BE0; UraDungeonMap 0x01DD56F0 = the back floor: built after the main floor without clearing the map, `dcdungeon --old`)"""
import struct, subprocess, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'coop'))
from pine import Pine

EXE = os.path.join(os.path.dirname(__file__), '..', '..', 'native', 'build', 'core_release', 'dcdungeon.exe')

def live(p, base):
    seed = struct.unpack('<I', p.read(base + 0x468, 4))[0] if isinstance(p.read(base + 0x468, 4), bytes) else p.read(base + 0x468, 4)
    return seed

def rd32(p, a):
    v = p.read(a, 4)
    return struct.unpack('<I', v)[0] if isinstance(v, (bytes, bytearray)) else v

def main():
    base = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x01DC4BE0
    with Pine() as p:
        seed = rd32(p, base + 0x468)
        nrooms = rd32(p, base + 0xb650)
        rooms = [tuple(rd32(p, base + 0xb550 + i * 16 + k * 4) for k in range(4)) for i in range(nrooms)]
        grid = []
        for i in range(400):
            part = struct.unpack('<i', struct.pack('<I', rd32(p, base + 0x9c50 + i * 16)))[0]
            rot = rd32(p, base + 0x9c54 + i * 16)
            grid.append((part, rot))
        selmap = rd32(p, 0x002A3594)
    print('live seed', seed, 'rooms', nrooms, rooms, 'selectMapNo', selmap)
    out = subprocess.run([EXE, str(seed), str(nrooms), str(selmap)], capture_output=True, text=True).stdout.split('\n')
    n = int(out[0].split()[1])
    mine_rooms = [tuple(map(int, out[1 + i].split())) for i in range(n)]
    cells = [tuple(map(int, out[1 + n + i].split())) for i in range(400)]
    print('rooms equal:', mine_rooms == rooms, mine_rooms)
    diff = [(i % 20, i // 20, grid[i], cells[i][:2]) for i in range(400) if grid[i] != cells[i][:2]]
    print('cell diffs:', len(diff))
    for d in diff[:40]:
        print('  (x=%d,y=%d) live=%s mine=%s' % d)
    for y in range(20):
        print(''.join('%3s' % ('.' if grid[y * 20 + x][0] < 0 else grid[y * 20 + x][0]) for x in range(20)))

main()
