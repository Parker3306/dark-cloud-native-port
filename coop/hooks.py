"""Code cave + "remote call" mailboxes for the dungeon overlay.

Two hooks, each redirecting one `jal` to a stub in unused RAM. A stub runs the original function and executes every
call queued in its mailbox: the sidecar writes a function address + arguments into a slot, marks it valid, and the
stub calls the function on the EE thread and writes back the result.

  phase "update": the `jal MoveChara` in GameLoop (once per frame, game logic). The stub runs MoveChara FIRST, then the
                  mailbox. Use it for loading things (SetNPC) and anything that changes game state.
  phase "draw":   the `jal DrawNPCDraw` in the dungeon's main draw routine. The stub runs the mailbox FIRST, then
                  DrawNPCDraw. Use it for the NPC draw lists: the game's map code appends its own placements to every active
                  NPC slot, so ClearNPC_Cash + ReservNPC_Draw here leaves exactly our entries.

Layout (all in free RAM, see notes/coop.md):
  MAILBOX_x  header (0x40 bytes): +0 enabled, +4 frame heartbeat, +8 GATE, +0xC step tokens (GATE non-zero: the stub keeps serving the mailbox and only returns to the game
             when a step token is available, which it consumes; tools use it to step the game in lock-step); 16 slots of 0x50 bytes from +0x40:
               +00 valid  +04 func  +08 a0 +0C a1 +10 a2 +14 a3 +18 t0 +1C t1 +20 t2 +24 t3
               +28 f12 +2C f13 +30 f14 +34 f15 (raw float bits)  +38 9th arg (stack)  +3C result (v0)  +40 completed-call counter
  update: mailbox 0x01F60000, stub 0x01F61000      draw: mailbox 0x01F60800, stub 0x01F61300      scratch: 0x01F60F00
Both hooks live in the DUN overlay: install after the overlay is loaded and again after every save-state load (a state
restores RAM). The title/other overlays share the address range, so never leave them installed outside a dungeon.
"""
import struct
import time
from pathlib import Path

from mips import assemble, selftest
from pine import Pine

DUN_BIN = Path(__file__).resolve().parent.parent / "extracted" / "DUN.BIN"

SLOT_SIZE = 0x50
NSLOTS = 16
MOVE_CHARA = 0x01DB08A0
DRAW_NPC_DRAW = 0x001C1F80
OVERLAY_BASE = 0x01DABD00
SCRATCH = 0x01F60F00

PHASES = {
    "update": dict(mailbox=0x01F60000, stub=0x01F61000, hook=0x01DAD9AC, target=MOVE_CHARA, original_first=True),
    "draw": dict(mailbox=0x01F60800, stub=0x01F61300, hook=0x01DAE988, target=DRAW_NPC_DRAW, original_first=False),
}
for _ph in PHASES.values():
    _ph["slot0"] = _ph["mailbox"] + 0x40
    _ph["hook_word"] = 0x0C000000 | (_ph["stub"] >> 2)          # jal stub
    _ph["original"] = 0x0C000000 | (_ph["target"] >> 2)         # jal original function


def stub_source(ph):
    original = ("    jal   {t:#x}\n    nop\n".format(t=ph["target"]))
    restore_a0 = "    ld    a0, 0x38(sp)\n"
    return f"""
entry:
    addiu sp, sp, -0x50
    sd    ra, 0x20(sp)
    sd    s0, 0x28(sp)
    sd    s1, 0x30(sp)
    sd    a0, 0x38(sp)
{original if ph["original_first"] else ""}    li    s0, {ph["mailbox"]:#x}
    lw    v0, 4(s0)
    addiu v0, v0, 1
    sw    v0, 4(s0)
poll:
    li    s0, {ph["mailbox"]:#x}
    lw    v0, 0(s0)
    beq   v0, zero, out
    nop
    addiu s0, s0, 0x40
    li    s1, {NSLOTS}
loop:
    lw    v0, 0(s0)
    beq   v0, zero, next
    nop
    lw    t9, 4(s0)
    lw    a0, 8(s0)
    lw    a1, 0xC(s0)
    lw    a2, 0x10(s0)
    lw    a3, 0x14(s0)
    lw    t0, 0x18(s0)
    lw    t1, 0x1C(s0)
    lw    t2, 0x20(s0)
    lw    t3, 0x24(s0)
    lwc1  f12, 0x28(s0)
    lwc1  f13, 0x2C(s0)
    lwc1  f14, 0x30(s0)
    lwc1  f15, 0x34(s0)
    lw    v1, 0x38(s0)
    sd    v1, 0(sp)
    jalr  ra, t9
    nop
    sw    v0, 0x3C(s0)
    lw    v1, 0x40(s0)
    addiu v1, v1, 1
    sw    v1, 0x40(s0)
    sw    zero, 0(s0)
next:
    addiu s0, s0, {SLOT_SIZE:#x}
    addiu s1, s1, -1
    bne   s1, zero, loop
    nop
    li    s0, {ph["mailbox"]:#x}
    lw    v0, 8(s0)
    beq   v0, zero, out
    nop
    lw    v0, 0xC(s0)
    beq   v0, zero, poll
    nop
    addiu v0, v0, -1
    sw    v0, 0xC(s0)
out:
{"" if ph["original_first"] else restore_a0 + original}    ld    ra, 0x20(sp)
    ld    s0, 0x28(sp)
    ld    s1, 0x30(sp)
    addiu sp, sp, 0x50
    jr    ra
    nop
"""


def f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


class Cave:
    def __init__(self, pine: Pine):
        self.p = pine
        for ph in PHASES.values():
            ph["code"] = assemble(stub_source(ph), ph["stub"])

    def overlay_is_dungeon(self):
        return self.p.read_bytes(OVERLAY_BASE + 0x80, 256) == DUN_BIN.read_bytes()[0x80:0x180]

    def installed(self):
        return all(self.p.read(ph["hook"]) == ph["hook_word"] for ph in PHASES.values())

    def install(self):
        selftest()
        if not self.overlay_is_dungeon():
            raise RuntimeError("the dungeon overlay is not loaded; refusing to patch")
        for name, ph in PHASES.items():
            cur = self.p.read(ph["hook"])
            if cur not in (ph["original"], ph["hook_word"]):
                raise RuntimeError(f"{name}: unexpected instruction 0x{cur:08X} at 0x{ph['hook']:08X}")
        for ph in PHASES.values():
            self.p.write_bytes(ph["mailbox"], bytes(0x40 + NSLOTS * SLOT_SIZE))   # clear mailbox
            self.p.write_bytes(ph["stub"], ph["code"])
            self.p.write(ph["mailbox"], 1)                                         # enabled
        for ph in PHASES.values():
            self.p.write(ph["hook"], ph["hook_word"])                              # activate last

    def uninstall(self):
        for ph in PHASES.values():
            if self.p.read(ph["hook"]) != ph["original"]:
                self.p.write(ph["hook"], ph["original"])

    def heartbeat(self, phase="update"):
        return self.p.read(PHASES[phase]["mailbox"] + 4)

    def _free_slots(self, phase, n, timeout):
        ph = PHASES[phase]
        end = time.time() + timeout
        while True:
            valid = self.p.read_many([(ph["slot0"] + i * SLOT_SIZE, 4) for i in range(NSLOTS)])
            free = [i for i, v in enumerate(valid) if v == 0]
            if len(free) >= n:
                return free[:n]
            if time.time() > end:
                raise RuntimeError("not enough free mailbox slots")
            time.sleep(0.01)

    @staticmethod
    def _words(c):
        a = list(c.get("a", ())) + [0] * (4 - len(c.get("a", ())))
        t = list(c.get("t", ())) + [0] * (4 - len(c.get("t", ())))
        fl = [f32(x) for x in c.get("f", ())] + [0] * (4 - len(c.get("f", ())))
        return [c["func"]] + a + t + fl + [c.get("stack9", 0) & 0xFFFFFFFF]

    def call_batch(self, calls, phase="update", timeout=2.0):
        """Queue several calls so they run in order within ONE frame (the stub walks the slots in index order).
        calls: list of dicts with keys func, a, t, f, stack9. Returns the list of results (v0)."""
        ph = PHASES[phase]
        slots = self._free_slots(phase, len(calls), timeout)
        bases = [ph["slot0"] + i * SLOT_SIZE for i in slots]
        done0 = self.p.read_many([(b + 0x40, 4) for b in bases])
        items = []
        for b, c in zip(bases, calls):
            items += [(b + 4 + 4 * k, w & 0xFFFFFFFF, 4) for k, w in enumerate(self._words(c))]
        self.p.write_many(items)                                           # all arguments first
        self.p.write_many([(b, 1, 4) for b in bases])                      # then every valid flag, in order
        end = time.time() + timeout
        while time.time() < end:
            now = self.p.read_many([(b + 0x40, 4) for b in bases])
            if all(n != d for n, d in zip(now, done0)):
                return [self.p.read(b + 0x3C) for b in bases]
            time.sleep(0.003)
        raise TimeoutError(f"the game did not run the queued calls (phase {phase!r}: paused, or not in a dungeon frame?)")

    def call(self, func, a=(), t=(), f=(), stack9=0, phase="update", timeout=2.0):
        """Run func(a0..a3, t0..t3, f12..f15, 9th arg) on the game thread; returns v0 (unsigned 32-bit)."""
        return self.call_batch([dict(func=func, a=a, t=t, f=f, stack9=stack9)], phase, timeout)[0]
