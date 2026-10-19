"""Co-op sidecar: connects one PCSX2 (via PINE) to the other players (via the network).

It reads the local player's position, facing, animation and floor, sends them to the other players, and draws a puppet
(a second Toan, in an NPC slot) for each remote player standing on the same floor.

  python -I coop/sidecar.py --pine 28011 --host 5000 --name Ann
  python -I coop/sidecar.py --pine 28012 --join 127.0.0.1:5000 --name Bob [--demo-circle 90]

--demo-circle drives the LOCAL player around in a circle (teleporting it each frame), to show movement without a human.
"""
import argparse
import math
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import memmap as M  # noqa: E402
from hooks import SCRATCH, Cave  # noqa: E402
from net import Net  # noqa: E402
from pine import Pine, PineError  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TWO_PI = 2 * math.pi


def wrap_angle(a):
    """The game wraps angles only once; anything outside -pi..pi corrupts rendering. Always wrap."""
    return (a + math.pi) % TWO_PI - math.pi


def f2i(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def i2f(x):
    return struct.unpack("<f", struct.pack("<I", x & 0xFFFFFFFF))[0]


class Puppet:
    def __init__(self, slot):
        self.slot = slot
        self.pos = None            # smoothed position being drawn
        self.motion = None


class Sidecar:
    SEND_EVERY = 2                 # network state every 2nd frame (~30 Hz)

    def __init__(self, pine_slot, net, demo_circle=None, log=print):
        self.pine_slot, self.net, self.demo, self.log = pine_slot, net, demo_circle, log
        self.p = None
        self.cave = None
        self.pack = None
        self.remotes = {}          # id -> {"name", "state", "t"}
        self.puppets = {}          # id -> Puppet
        self.place = None
        self.tickn = 0
        self.demo_origin = None
        self.t0 = time.time()

    # --- connection to the emulator ----------------------------------------------------------------
    def connect(self):
        self.p = Pine(self.pine_slot)
        self.cave = Cave(self.p)
        if self.p.crc() != M.GAME_CRC:
            raise RuntimeError(f"wrong game build in PCSX2 (crc {self.p.crc()}, need {M.GAME_CRC})")
        self.log(f"connected to PCSX2 {self.p.version()} on PINE {self.pine_slot}: {self.p.title()}")

    def in_dungeon(self):
        return self.cave.overlay_is_dungeon() and self.p.read(M.BT_GAME_MODE_FLAG) == 1

    # --- local state -------------------------------------------------------------------------------
    def read_local(self):
        c = M.CHARA_MAIN
        r = self.p.read_many([(c + M.CHARA_POS, 4), (c + M.CHARA_POS + 4, 4), (c + M.CHARA_POS + 8, 4),
                              (c + M.CHARA_YAW, 4), (c + M.CHARA_MOTION, 4), (M.SELECT_MAP_NO, 4),
                              (M.BT_URA_DONGEON, 4), (M.USER_STATUS_PTR, 4), (M.NOW_DNG_MAP, 4)])
        floor = self.p.read(r[7] + 2, 1)
        return dict(pos=[i2f(r[0]), i2f(r[1]), i2f(r[2])], yaw=i2f(r[3]), motion=r[4],
                    place=[r[5], floor, r[6]], map=r[8])

    # --- puppets -----------------------------------------------------------------------------------
    def puppet_slot_free(self, dng, slot):
        return self.p.read(dng + slot * M.SLOT_STRIDE + M.SLOT_ACTIVE) == 0

    def spawn_puppet(self, rid, dng):
        used = {pp.slot for pp in self.puppets.values()}
        slot = next((s for s in (1, 2, 3) if s not in used and self.puppet_slot_free(dng, s)), None)
        if slot is None:
            self.log("no free NPC slot for a puppet")
            return None
        base, _, usedu, cap = struct.unpack("<4I", self.p.read_bytes(M.MONSTER_POOL, 16))
        if cap - usedu < 170000:                      # SetNPC copies about 2.3 MB; the allocator freezes the game on overflow
            self.log(f"not enough free memory for another puppet ({(cap - usedu) * 16 / 1e6:.1f} MB)")
            return None
        if self.pack is None:
            self.pack = (ROOT / M.PLAYER_PACK).read_bytes()
        ptr = self.p.read(M.READ_BUFFER_PTR)
        self.p.write_bytes(ptr, self.pack)            # the loader copies what it needs out of the pack
        self.p.write_bytes(SCRATCH, struct.pack("<8f", 0, 0, 0, 1, 0, 0, 0, 0))
        old = self.p.read_bytes(M.NPC_CFG_NAME, 9)
        self.p.write_bytes(M.NPC_CFG_NAME, b"base.cfg\0")
        try:
            self.cave.call(M.SET_NPC, a=[dng, slot, ptr, 0], t=[SCRATCH, SCRATCH + 0x10, 1, 0],
                           stack9=M.MONSTER_POOL, timeout=10)
        finally:
            self.p.write_bytes(M.NPC_CFG_NAME, old)
        self.log(f"spawned a puppet for {self.remotes[rid]['name']} in NPC slot {slot}")
        return Puppet(slot)

    def drop_puppets(self):
        self.puppets.clear()

    # --- main loop ---------------------------------------------------------------------------------
    def handle_net(self):
        for m in self.net.poll():
            t, rid = m.get("t"), m.get("id")
            if t == "state" and rid is not None:
                r = self.remotes.setdefault(rid, {"name": self.net.names.get(rid, f"player{rid}")})
                r["state"], r["t"] = m, time.time()
            elif t == "join":
                self.remotes.setdefault(rid, {"name": m.get("name", f"player{rid}")})
                self.log(f"{m.get('name')} joined")
            elif t == "leave":
                self.remotes.pop(rid, None)
                self.puppets.pop(rid, None)
                self.log(f"{m.get('name')} left")

    def tick(self):
        self.tickn += 1
        self.handle_net()
        if not self.in_dungeon():
            if self.puppets:
                self.drop_puppets()
            self.place = None
            time.sleep(0.25)
            return
        if not self.cave.installed():                 # first time, or a state load restored RAM
            self.cave.install()
            self.drop_puppets()
            self.log("hooks installed")
        me = self.read_local()
        if me["place"] != self.place:
            self.place = me["place"]
            self.drop_puppets()                       # a new floor resets the pools and NPC slots
            self.log(f"on floor {me['place']}")
        if self.tickn % self.SEND_EVERY == 0:
            self.net.send({"t": "state", "place": me["place"], "pos": me["pos"], "yaw": me["yaw"], "motion": me["motion"]})

        update = []
        if self.demo:                                  # scripted movement of the local player (demo only)
            if self.demo_origin is None:
                self.demo_origin = tuple(me["pos"])
            ox, oy, oz = self.demo_origin
            ang = (time.time() - self.t0) * 0.8
            update.append(dict(func=M.SET_POSITION, a=[M.CHARA_MAIN],
                               f=[ox + self.demo * math.cos(ang), oy, oz + self.demo * math.sin(ang)]))
            update.append(dict(func=M.SET_ROTATION, a=[M.CHARA_MAIN], f=[0.0, wrap_angle(-ang), 0.0]))
        if update:
            self.cave.call_batch(update, phase="update")

        # puppets for remote players on this floor
        dng, now = me["map"], time.time()
        for rid, r in list(self.remotes.items()):
            st = r.get("state")
            here = st is not None and st["place"] == me["place"] and now - r["t"] < 3.0
            pup = self.puppets.get(rid)
            if here and pup is None:
                pup = self.spawn_puppet(rid, dng)
                if pup:
                    self.puppets[rid] = pup
            elif not here and pup is not None:
                self.puppets.pop(rid)                  # hidden: no draw entries are queued for it any more
        writes, draw = [], []
        for rid, pup in self.puppets.items():
            st = self.remotes[rid]["state"]
            base = dng + pup.slot * M.SLOT_STRIDE
            tx, ty, tz = st["pos"]
            if pup.pos is None or math.dist(pup.pos, (tx, ty, tz)) > 300:
                pup.pos = [tx, ty, tz]
            else:                                      # smooth the 30 Hz updates
                pup.pos = [a + (b - a) * 0.4 for a, b in zip(pup.pos, (tx, ty, tz))]
            writes.append((base + M.SLOT_YAW, f2i(wrap_angle(st["yaw"])), 4))
            if st["motion"] != pup.motion:
                pup.motion = st["motion"]
                writes.append((base + M.SLOT_MOTION, st["motion"] & 0xFFFFFFFF, 4))
                writes.append((base + M.SLOT_MOTION - 4, 0, 4))      # restart the motion clock
                writes.append((base + M.SLOT_MOTION - 8, 0xBF800000, 4))   # blend = -1.0, as NPCSetMotion does
            draw.append(dict(func=M.MEMSET, a=[base + M.SLOT_DRAWCOUNT, 0, 4]))   # drop the map's own placements
            draw.append(dict(func=M.RESERV_NPC_DRAW, a=[dng, pup.slot, 0], f=[pup.pos[0], pup.pos[1], pup.pos[2]]))
        if writes:
            self.p.write_many(writes)
        if not draw:
            draw = [dict(func=M.MEMSET, a=[SCRATCH + 0x80, 0, 4])]      # harmless call that paces us at one tick per frame
        self.cave.call_batch(draw, phase="draw")

    def run(self):
        self.connect()
        while True:
            try:
                self.tick()
            except (PineError, OSError, TimeoutError) as e:
                self.log(f"emulator connection problem ({e}); retrying")
                self.drop_puppets()
                time.sleep(1.0)
                try:
                    self.connect()
                except OSError:
                    pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pine", type=int, default=28011)
    ap.add_argument("--name", default="player")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--host", type=int, metavar="PORT")
    g.add_argument("--join", metavar="IP:PORT")
    ap.add_argument("--demo-circle", type=float, metavar="RADIUS")
    args = ap.parse_args()
    from memmap import GAME_CRC
    net = Net.host(args.host, args.name, GAME_CRC) if args.host else Net.join(args.join, args.name, GAME_CRC)
    print(f"[{args.name}] {'hosting on port ' + str(args.host) if args.host else 'joined ' + args.join}", flush=True)
    Sidecar(args.pine, net, args.demo_circle, log=lambda s: print(f"[{args.name}] {s}", flush=True)).run()


if __name__ == "__main__":
    main()
