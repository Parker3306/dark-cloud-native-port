# Co-op project notes

Plan: `~\.claude\plans\recursive-sprouting-honey.md` (staged: Stage A presence + scaling, Stage B shared monsters).
Architecture: each PCSX2 <-PINE (localhost TCP)-> sidecar <-network-> other sidecars. Only small code stubs go into the game.

## Done
- PINE enabled (`tools/pcsx2/inis/PCSX2.ini`: `EnablePINE = true`, `PINESlot = 28011`).
- `coop/pine.py`: PINE client (read/write 8/16/32/64, batched reads, byte blocks, floats, title/id/crc/status, save/load state).
  Tested live: PCSX2 v2.8.2, SCUS-97111, crc a5c05c78, memory reads work.
- `scripts/ghidra/AddOverlay.java` + Ghidra program `SCUS_dun`: the ELF has the dungeon overlay (DUN.BIN, code at 0x01DABD00)
  empty; this maps DUN.BIN in and labels its 62 functions, so they decompile. Use `-process SCUS_dun` with `DecompileFunction.java`.
  (TITLE.BIN lives at the same address; not mapped. `SCUS_971.11` stays pristine.)
- Overlay format: `MWo3`, version, base 0x01DABD00, size, ... code starts at +0x80, then relocation data (unused at the link address).

## Memory facts (EE addresses)
| What | Address | Notes |
|---|---|---|
| Free RAM at title screen | 0x01F07B00-0x01FB3B00 (688 KB zeros) | heap `_end` = 0x01F06B00, stack 0x80000 at the top. **Re-check inside a dungeon** |
| `CharaMain` | 0x01EA1D20 (0x11B0) | the player, class CMainChara, vtable 0x002A11E0 |
| `CharaHand` | 0x01EA2ED0 | NOT a second player: auxiliary held-item model, loaded from `hand_up.cfg` only for characters 1 and 3 |
| `MainMonstorUnit` | 0x01DF87D0 (395,088) | `NowMonstorUnit` global points at the active one (main or "ura" dungeon) |
| `NPCUnit` / NPC slots | 0x01EA8460; slots inside CDungeonMap at +0xCFC4 + slot*0x1330 | 4 slots, `SetNPC` @0x001C1C00 loads a pack's `info.cfg` |
| `MonstorTable` | 0x0027FB00 (26,052) | base monster stats |
| `CDungeonMap` seed | object + 0x468 | `buildRandomMap` @0x001CB670 rolls it when arg3 != 0 and `srand`s it |
| `srand` / `rand` | 0x001046E8 / 0x001046F8 | libc, one global state |
| `UserStatus` (CDngStatusData) | global ptr; floor at +2 (char) | selects the enemy layout row |
| `BtGameModeFlag` | dun overlay | 1 = playing (calls `MoveChara`), 2 = battle menu, >7 = leaving |

## Dungeon overlay function map (0x01DABD00+)
- `GameInit` 0x01DAC1C0 (6 KB), `GameLoop` 0x01DAD980 (called every frame; mode 1 -> `MoveChara`), `MainDraw` 0x01DAE2A0.
- `MoveChara` 0x01DB08A0 (27 KB: player control and the main per-frame update) -> **best hook point for the co-op stub**.
- `LoadChara2(charIndex, ?, pack, ...)` 0x01DBA3C0: loads `base.cfg` of the playable character into CharaMain, `hand_up.cfg` into CharaHand for chars 1/3,
  weapon via `LoadWeapon2`. Character index 0..5 (5 and 3 load extra Osmond effects).
- Monsters: `BtLoadMonstor(ura)` 0x01DB9330 (loads layout `BtEnemyLayoutList[selectMapNo]` row `UserStatus[2]*0x70`, up to 9 types via
  `CMonstorUnit::SetupBaseModel`), `BtArrengeMonstor` 0x01DB92E0 (`ArrangementPos` places them), `BtCheckDamageProc` 0x01DBAFD0 (damage).
- Map: `BtCleatRandomMap` 0x01DB8790 (calls `buildRandomMap`), `BtCleatFreeMap` 0x01DB91C0.
- Per-character abilities: `BattleActionPlay_Jinn / _Ruby / _Ozumond(_H/_F)`, `BattleActionThlow`, `BattleActionDrink`.

## Stage 0/1 results (live, using the user's two dungeon save states: first floor of the first dungeon)
- **Hook works.** `coop/hooks.py` redirects the `jal MoveChara` at 0x01DAD9AC (inside GameLoop) to a stub at 0x01F61000 that runs
  MoveChara, then executes calls queued in a mailbox at 0x01F60000 (16 slots: func, a0-a3, t0-t3, f12-f15, 9th arg, result). Tested:
  heartbeat = 60/s, `MGGetVSyncCount` through the mailbox returns rising values, game unaffected. `coop/mips.py` is a small assembler checked
  against 13 real instruction words from the game. The hook must be reinstalled after every save-state load (a state restores RAM).
- **Free RAM:** 0x01F07B00-0x01FB3B00 (688 KB of zeros) in title and dungeon alike. The cave uses 0x01F60000+.
- **POOLS OVERLAP (important, cost a lot of time):** the "NPC pool" 0x01F06650 reports ~6 MB free, but its free area is the same memory as the
  monster pool (0x01F066D0, base 0x01049240), which holds the floor collision data. Allocating from the NPC pool corrupted collision.
  Use the *monster pool's* free space (about 4.5 MB after the floor is loaded). `EdNPCBuffer` also overlaps it. Allocation is a bump allocator
  (`CDataAlloc2::Alloc` 0x001278A0) that hangs the game if capacity is exceeded. It resets when a new floor loads, so puppets must be respawned.
- **Puppet = NPC slot.** `SetNPC(map, slot, pack, partsIdx, &pos, &rot, enable, motion, pool)` @0x001C1C00 works from the mailbox (pool via 9th arg).
  Slot 0 is already used by the floor's NPC, slots 1-3 are free. NPC pack `chara/c01d.chr` (info.cfg, own costume, basic motions) or the player's
  own pack `dun/mainchara/c01d.chr` (base.cfg: same textures as the player and the full motion set; SetNPC asks for the string "info.cfg"
  at 0x0029BDF8, which can be swapped for "base.cfg" (same length) during the call). The pack only needs to sit in the temporary load buffer
  (`read_buffer`, pointer at 0x002A2384): the loader copies what it needs. SetNPC then costs 1.46 MB (NPC pack) or 2.23 MB (player pack) of pool.
- NPC slot layout (stride 0x1330 in CDungeonMap): CCharacter object at +0xBDF0, frame ptr +0xBEAC, base position +0xCFA0, base rotation
  +0xCFB0 (yaw at +0xCFB4), active +0xCFC4, enable +0xCFC8, +0xCFCC (-1 hides), draw list: 16 positions at +0xCFD0 (16 B each), 16 ints at
  +0xD0D0, count at +0xD110. `ReservNPC_Draw(map, slot, dirCode)` with x,y,z in f12-f14 appends to it; `ClearNPC_Cash(map)` @0x001C1E70 zeroes every
  slot's count. DrawNPCDraw @0x001C1F80 draws each entry (position = entry + base pos; facing = dirCode quarter-turns + base yaw).
- **Texture memory is not the problem.** All character/NPC/monster texture blocks time-share VRAM at 0x1A40 and reload on demand.
- **The "floor fades to black" bug is SOLVED.** It was never the camera, lighting, shadows, textures or the HD pack. The cause was my own yaw
  value: I wrote `-ang` (growing without bound) to the slot's base rotation (+0xCFB4). The game wraps angles only once, so anything outside
  -pi..pi corrupts rendering (draw-packet vertex alphas drifted down, so the floor faded to black over ~5 s). **Always wrap yaw into -pi..pi**
  (`coop/sidecar.py: wrap_angle`). Found by bisecting the per-frame actions (yaw write / ClearNPC / ReservNPC): only the yaw write broke it.
- **Map placements:** once a slot is active the game's own map code (DrawMap) appends 4 draw entries to it (cell centres, e.g. (1600,0,800..1280)).
  Those are drawn as extra copies. Fix: a second hook in the draw phase (`jal DrawNPCDraw` at 0x01DAE988) that runs right BEFORE the draw:
  `memset(slot+0xD110, 0, 4)` for our slots (NOT ClearNPC_Cash: it would also wipe slot 0, the floor's real NPC), then `ReservNPC_Draw` per puppet.
- **Two hook phases** (`coop/hooks.py`): "update" (after `MoveChara`, game logic: SetNPC, SetPosition...) and "draw" (before `DrawNPCDraw`).
  Each has its own mailbox (0x01F60000 / 0x01F60800) and stub (0x01F61000 / 0x01F61300); scratch at 0x01F60F00.
- Puppet look: the NPC loader gives Toan's face/hair/body without the cloth poncho (cloth objects aren't attached). Motion ids are shared with the
  player (same pack), so copying `CharaMain+0xC68` to the puppet's `slot+0xCA58` should mirror animations (not yet verified live).

## Co-op sidecar (working end to end on one PC)
- `coop/net.py`: host/guest over TCP, JSON lines, handshake checks protocol version and game CRC, host relays and stamps sender ids.
- `coop/sidecar.py`: reads the local player (pos, yaw, motion id, floor), sends ~30 Hz, spawns one puppet per remote player on the same floor
  (NPC slots 1-3, memory from the monster pool, checked first), smooths positions, drops puppets on floor change/leave, reinstalls hooks after a state load.
  `--demo-circle R` teleports the local player in a circle (demo only; it upsets that game's camera, which is a demo artefact).
- `coop/memmap.py`: all addresses/offsets in one place.
- Test rig: second portable PCSX2 at `tools/pcsx2_b` (PINE 28012, 1x, no HD pack). Run both, load state 1 in each (PINE `load_state`), start
  `sidecar --pine 28011 --host 5000` and `sidecar --pine 28012 --join 127.0.0.1:5000 --demo-circle 90`.
  Result: both sidecars connected, each spawned a puppet for the other; Ann's game showed Bob's puppet moving and stayed healthy for 60+ s.
- Not done yet: shared floor seed, difficulty scaling, shared monsters, a human-played test, a launcher, NAT/VPN instructions.
- Tools made along the way: `scripts/ghidra/{AddOverlay,Callers,ListBlocks,FindInDecomp}.java`, `scripts/pack_list.py`, `coop/{pine,mips,hooks,probe,
  test_cave,test_puppet,dump_texblocks}.py`, `scripts/shot_pcsx2.ps1` (now captures only the PCSX2 window via PrintWindow).

## Next (needs the user)
Stage 0 step 3: the user plays New Game to the first dungeon and makes PCSX2 save states (File > Save State, slot 1-3;
`tools/pcsx2/sstates`). Then: map free RAM inside the dungeon, read live `CharaMain` position, and start Stage 1
(player/NPC structure layout, how to give an NPC slot the player's model: player pack uses `base.cfg`, NPC slots use `info.cfg`).
