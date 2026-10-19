"""Experiment: spawn one puppet (Toan NPC model) next to the player in a dungeon save state.

Usage: python -I coop/test_puppet.py [state_slot] [seconds]
Steps: install the hook, allocate the NPC pack from the dungeon's NPC pool, copy chara/c01d.chr in, SetNPC on a
free slot, then queue the puppet's position every frame (ReservNPC_Draw) while it walks in a circle.
"""
import math
import os
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from hooks import Cave, f32  # noqa: E402
from pine import Pine  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
NOW_DNG_MAP = 0x002A34B8
NPC_POOL = 0x01F06650         # overlaps the monster pool's free space: do NOT allocate from it
POOL = 0x01F066D0             # MonstorModelBuffer pool: its free space is real, free memory
READ_BUFFER_PTR = 0x002A2384  # game's temporary file-load buffer (idle during play)
ALLOC = 0x001278A0            # CDataAlloc2<1>::Alloc(this, units) -> pointer (hangs the game if over capacity!)
SET_NPC = 0x001C1C00          # CDungeonMap::SetNPC(slot, pack, partsIdx, pos*, rot*, enable, motion, pool)
CLEAR_NPC = 0x001C1E70        # CDungeonMap::ClearNPC_Cash(): sets every slot's draw-list count to 0
RESERV_DRAW = 0x001C1EB0      # CDungeonMap::ReservNPC_Draw(slot, x, y, z, dirCode)
NPC_SET_MOTION = 0x001C22B0   # CDungeonMap::NPCSetMotion(slot, motion)
MAIN_CAMERA = 0x01DC45E0      # CCameraFollow: pos +0x260, ref +0x270, follow target +0x2C0, dist +0x2D0, height +0x2D4, angle +0x2D8
AMBIENT = 0x01C756B0          # global ambient colour (MGGetAmbient/MGSetAmbient)
MAIN_AMBIENT = 0x01DF8730     # main_ambientlight
MAIN_LIGHT = 0x01DF86B0       # main_light
LIGHTING_MODE = 0x002A34CC
SLOT_STRIDE = 0x1330
SCRATCH = 0x01F60F00
PLAYER = 0x01EA1D20


NPC_CFG_NAME = 0x0029BDF8     # "info.cfg": the file name SetNPC asks the pack for (same length as "base.cfg")


def main(state=1, seconds=8, mode="circle", pack_kind="player"):
    with Pine() as p:
        p.load_state(state)
        time.sleep(3)
        cave = Cave(p)
        cave.install()

        dng = p.read(NOW_DNG_MAP)
        slot = int(os.environ["DC_SLOT"]) if os.environ.get("DC_SLOT") else \
            next(s for s in (3, 2, 1) if p.read(dng + s * SLOT_STRIDE + 0xCFC4) == 0)
        if pack_kind == "player":      # the playable character's own pack: same textures + full motion set
            pack = (ROOT / "data" / "dun" / "mainchara" / "c01d.chr").read_bytes()
        else:                          # the NPC version: own costume textures, basic motions
            pack = (ROOT / "data" / "chara" / "c01d.chr").read_bytes()
        units = (len(pack) + 15) // 16
        # The game's "NPC pool" (0x1F06650) is NOT free memory: its free part overlaps the monster pool, where the
        # floor's collision data lives. Use the MONSTER pool's own free space instead.
        base, _, used, cap = struct.unpack("<4I", p.read_bytes(POOL, 16))
        free = cap - used
        need = 150000 if pack_kind == "player" else 100000      # units of 16 B: what SetNPC copies out of the pack
        print(f"pool 0x{POOL:08X}: {free * 16 / 1e6:.2f} MB free, SetNPC needs about {need * 16 / 1e6:.2f} MB")
        if free < need + 20000:
            raise SystemExit("not enough room in the pool; refusing (the game freezes on overflow)")

        # The loader copies what it needs out of the pack, so the pack only has to sit in the temporary load buffer.
        ptr = p.read(READ_BUFFER_PTR)
        print(f"pack placed in the load buffer at 0x{ptr:08X} ({len(pack) / 1e6:.2f} MB)")
        p.write_bytes(ptr, pack)
        p.write_bytes(SCRATCH, struct.pack("<8f", 0, 0, 0, 1, 0, 0, 0, 0))   # pos vec4 at +0, rot at +0x10
        old_name = p.read_bytes(NPC_CFG_NAME, 9)
        if pack_kind == "player":
            p.write_bytes(NPC_CFG_NAME, b"base.cfg\0")
        try:
            r = cave.call(SET_NPC, a=[dng, slot, ptr, 0], t=[SCRATCH, SCRATCH + 0x10, 1, 0], stack9=POOL,
                          timeout=10)
        finally:
            p.write_bytes(NPC_CFG_NAME, old_name)
        print(f"SetNPC slot {slot} returned 0x{r:08X}")
        base, _, used2, _ = struct.unpack("<4I", p.read_bytes(POOL, 16))
        print(f"pool used {used} -> {used2} units (+{(used2 - used) * 16 / 1e6:.2f} MB)")
        print("NPC active flag:", p.read(dng + slot * SLOT_STRIDE + 0xCFC4),
              "frame ptr:", hex(p.read(dng + slot * SLOT_STRIDE + 0xBEAC)))

        if os.environ.get("DC_NOSHADOW"):
            # CCharacter::DrawShadow only runs when the object's second frame pointer (+0xC0) is set
            p.write(dng + slot * SLOT_STRIDE + 0xBDF0 + 0xC0, 0)
            print("puppet shadow disabled")
        px, py, pz = struct.unpack("<3f", p.read_bytes(PLAYER + 0x10, 12))
        print(f"player at ({px:.1f}, {py:.1f}, {pz:.1f}); drawing puppet around them for {seconds}s", flush=True)
        t0, n = time.time(), 0
        while time.time() - t0 < seconds:
            if mode == "none":                       # spawned but never drawn
                time.sleep(0.05)
                continue
            radius = float(mode.split(":")[1]) if mode.startswith("circle:") else 70.0
            ang = (time.time() - t0) * 1.5 if mode.startswith("circle") else 0.0
            x, z = px + radius * math.cos(ang), pz + radius * math.sin(ang)
            if mode.startswith("fixed"):           # fixed or fixed:dx:dz (offset from the player)
                _, *off = mode.split(":")
                dx, dz = (float(off[0]), float(off[1])) if off else (150.0, 0.0)
                x, z = px + dx, pz + dz
            # The game wraps angles only once, so anything outside -pi..pi corrupts rendering (this was the "floor fades to
            # black" bug: an ever-growing angle). Always keep the yaw inside -pi..pi.
            yaw = (-ang + math.pi) % (2 * math.pi) - math.pi
            parts = os.environ.get("DC_PARTS", "yaw,clear,reserve").split(",")   # bisecting tool
            if "yaw" in parts:
                p.write(dng + slot * SLOT_STRIDE + 0xCFB4, f32(yaw))
            # the game only clears the draw list for NPC slots its own events use, so clear it ourselves, then queue
            # this frame's single position (otherwise the list grows every frame and the puppet is drawn 16 times)
            calls = []
            if "clear" in parts:
                calls.append(dict(func=CLEAR_NPC, a=[dng]))
            if "reserve" in parts:
                calls.append(dict(func=RESERV_DRAW, a=[dng, slot, 0],
                                  f=[x, py + float(os.environ.get("DC_YOFF", "0")), z]))
            if calls:
                cave.call_batch(calls, phase="draw")   # runs right before DrawNPCDraw, after the game added its own entries
            else:
                time.sleep(0.016)
            n += 1
            if n % 60 == 0:                          # light log, once a second
                fl = lambda a, k: [round(v, 2) for v in struct.unpack(f"<{k}f", p.read_bytes(a, 4 * k))]
                npc = dng + slot * SLOT_STRIDE + 0xBDF0
                print(f"t={time.time() - t0:5.1f} ambient={fl(AMBIENT, 4)} main_amb={fl(MAIN_AMBIENT, 4)} "
                      f"main_light={fl(MAIN_LIGHT, 4)} lightingMode={p.read(LIGHTING_MODE)} "
                      f"npc.alpha={fl(npc + 0xCEC, 1)} npc.amb+={fl(npc + 0xCE0, 3)} npc.appear={p.read(npc + 0x1474)} "
                      f"drawcount={p.read(dng + slot * SLOT_STRIDE + 0xD110)}", flush=True)
        print(f"{n} frames queued ({n / seconds:.0f}/s); reservation count:",
              p.read(dng + slot * SLOT_STRIDE + 0xD110))


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 1, float(sys.argv[2]) if len(sys.argv) > 2 else 8,
         sys.argv[3] if len(sys.argv) > 3 else "circle", sys.argv[4] if len(sys.argv) > 4 else "player")
