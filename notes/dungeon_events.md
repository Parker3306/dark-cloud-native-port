# Dungeon story rooms, boss rooms and boss fights (map kinds 0 and 2)

Scope: how the dungeon event scripts (`dun/script/d0N/event.stb`) drive story rooms (kind 0), boss rooms (kind 2) and the boss fights, which external functions of the DUNGEON table (ids 1010..1101) exist, what state a cleared dungeon leaves behind, and what to implement first to play the boss floor of dungeon 1 (Divine Beast Cave, Dran) end to end.

Tags (as in notes/menus.md): **READ** = read from `work/decomp/all_dun.c` / the disassembly, **VERIFIED-DATA** = checked against the disc files or by running the scripts in the Python VM (`tools/dc/dungeon_trace.py`), **INFERRED** = deduced, **OPEN** = not resolved (section 8). Nothing here was compared with the running game (no PCSX2 session).
Companion notes: `notes/events.md` (script VM, town runtime, section 6 is the first sketch of the dungeon side), `notes/event_ext.csv` (all 373 ext functions), `notes/native-plan.md` (M4: floors, markers, doors).
Tools written for this note (all read-only, `python -I`): `tools/dc/dungeon_events.py` (usage table, function ids, markers, closures, `ext` table), `tools/dc/dungeon_trace.py` (run a dungeon function headless with floor/flag/item answers), `tools/dc/boss_layouts.py` (monster layout rows of story/boss floors). Disassembly came from `work/disasm.ps1` (Ghidra headless, `-readOnly`).

Dungeon numbering: script files `d01..d07` = dungeon 1..7 = `selectMapNo` 0..6. Floors are 0-based everywhere in the scripts (`_GET_FLOOR_LEVEL`; "floor 14" below = the 15th floor).

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 1. Files and how the engine starts a script function

### 1.1 Files (VERIFIED-DATA)
| file | role |
|---|---|
| `dun/script/d0N/event.stb` (N = 1..7) | the ONLY script of a dungeon (loaded by `BtSystemScriptLoad(selectMapNo)`, format string `dun/script/d0%d/event.stb` at ELF 0x19bdb0). Same bytecode and same VM as the town scripts (`dccore/script.*`), ext tables EVENT (0..1000) + DUNGEON (1010..1101). The message bank is embedded in the stb (header word +0x14 = file offset of a `.mes` image running to the end of the file; there is no per-language file for dungeons). Ids: 30..51 trap-chest texts, 100..166 story dialogue, 1000+ duel, 1900..1905 jump-gap texts ("<name0> cannot jump this!"), 200/250/300/400.. keyhole texts. Dump with `mesdump.py` after cutting the tail out. |
| `dun/d0N<main_a|main_b|main_c|main_d..|inter|inter2|boss|boss2>.cfg` | part lists. `PT_MARKER "name", radius, event_id, item_use, auto, switch, ext` (see 1.5). Boss cfgs have no random parts. |
| `dun/mpd_pack/d0N*.mpd` | models/collision of the parts of those cfgs. |
| `dun/monstor/<stem>.chr/.stb` | monster/boss models and scripts (boss scripts c12a c13a c14a c15a c16a c17a c21a c23a, helpers c13_*, c17_*, c23_*). |
| `gedit/s98/chara/*` (e15c12a.chr, e24c12a.chr, e16.chr, boss.pak, img.pak ...), `dun/d01/event/*`, `dun/d01/stair_mos/*` | cut-scene models used by the dungeon scripts (the scripts `_SET_CURRENT_DIR` to these). `s98` is a scene-asset-only map. |

### 1.2 Function ids (VERIFIED-DATA, `dungeon_events.py funcs`)
Generic ids (all seven scripts follow the same scheme):

| id | meaning | started by |
|---|---|---|
| 100 | floor start (choose map, build floor, spawn, arrival scene, `_GO_DUNGEON`) | engine, right after the dungeon is entered (`GameInit`: `DAT_01d5676c = 100`, `gameTask = 400`, READ) and after each floor change via `_RUN_SCRIPT_NO(100)` |
| 90 | pre-built floor start (`_LOAD_DUNGEON_MAP2; _SET_RANDOM_MAP; _LOAD_MONSTOR; _GO_DUNGEON`) | not started by anything found (dead code in d01; present in all seven) |
| 150 | stair-arrival cut-scene (`in01.chr`: hero walks out of the stairs) | **called as an internal function by 100** (record 0x34e14 is both function 150 and the helper `fn_34e14` that 100 calls), not by the engine. Solves the "unlocated trigger" of events.md 9.9 for 150. |
| 198 / 199 / 197 | first-visit scenes and boss/mini-boss spawn scripts | `_RUN_SCRIPT_NO(199, 1, 0)` from 100 (d01 floor 0 first time; d03 boss floor `_RUN_SCRIPT_NO(199,0)`; d06/d07 boss floors) |
| 160 | exit stairs marker | marker (auto OFF, OK) |
| 161 | gate (gate key door) marker | marker |
| 200 / 201 | back-floor ("ura") entrances: trap door, tram, coffin (`hitsugi`, d04), plank (`ita`, d05) | markers (item use) |
| 250 / 251 | locked door (key item 216 = 0xd8 in d01) | marker |
| 350 / 351, 352 | jump-gap / character-limited passages ("<name> cannot jump this!"); 352 has a marker but no function (d02, d03 inter) | markers (character-gated, `+0x2c`) |
| 360/361, 365/366, 370, 375 | more character-specific rooms (`_CHECK_CHR_HELP`, `_GET_RUBY_ELEMENT`, `_GET_NOW_USER_ID`); no cfg has a `PT_MARKER` with ids 360-370, so their slots are created by code (INFERRED: `CDungeonEventMan::SetupEvent` / `BuildCharaSpecialParts` for the character special parts); 375 has cfg markers (`chr5_*`, d05-d07) but d07 has no function | code-created slots / markers |
| 5 | player died (death motion 32 + voice SE `1739 + character`, fade) | engine, `gameTask 200` death flow ends in `DAT_01d5676c = 5` (READ) |
| 15 / 16 | trapped treasure box (15 normal, 16 for kind 5 = the "Happy Clown" big/small box) | engine (`iventActive`, `DAT_01d5676c = 0xf/0x10`) |
| 300..302, 102, 105, 110, 111 | story-room events of the `inter` maps (d01: 102 duel start at inter2, 110 duel body, 300 inter scene / tram, 301/302 tram; d02: 111, 105, 300) | markers or `_CLEAN_MONSTOR_SCRIPT_NO` |
| 400..405, 500, 520, 800 | boss floor: 400/401 boss-key doors, 402..405 other boss-floor triggers, **500 boss start**, 520/800 d02 post-boss exit | markers |
| **510** | **boss defeated** | the boss monster script (function 120 of every boss: `_RUN_SCRIPT(510)`) |

Per-dungeon sets (all verified):
```
d01 5 15 16 90 100 102 103 110 150 160 161 199 200 250 300 301 302 350 351 400 500 510
d02 15 16 90 100 105 111 150 160 161 170 200 250 300 350 351 360 361 400 500 510 520 800
d03 15 16 90 100 160 161 195 196 197 198 199 200 250 350 351 360 361 365 366 400 510
d04 15 16 90 100 101 160 161 196 197 198 200 201 250 350 351 360 361 365 366 370 400 401 402 403 510
d05 15 16 90 100 101 150 160 161 199 200 201 250 251 350 351 360 361 365 366 370 375 404 405 500 510
d06 15 16 90 100 101 160 197 198 199 200 250 350 351 360 361 365 366 370 375 510
d07 15 16 90 100 101 160 199 200 250 251 510
```
Function 5 (death scene) and 103/110/300-302/102 exist only in d01; for the other six dungeons the engine's `DAT_01d5676c = 5` finds no function (`EdEventInit` returns -1, the event just does not start). 195..198 in d03/d04 are **monster death events** (section 4.3). d07 has no 400/500/350..375 at all: its boss is spawned by 199 from 100, its chara markers have no script.

### 1.3 How the engine raises an event (READ, `MoveChara` 0x01db08a0, `gameTask`)
State `DAT_01d5676c` = function id to run (-1 none), `DAT_01d56774` = "heavy" flag, `DAT_01d56764` = action mode (0 stepped on, 1 OK, 2 item-use button), `DAT_01d56740/50` = marker position/rotation (`_GET_EVENT_POS/ROT`), `DAT_01d567d0` = "EdFadeInit at the start" flag (default 1), `DAT_01d567d8` = pending request (1.4).

Triggers, checked each frame at `gameTask 0` while the player is free (INFERRED meaning of the guard `DAT_01dc44e4 != 0 && DAT_01dc4518 == 0`: not in an attack/throw/drink action and not frozen):
1. **Marker slot**: `CDungeonEventMan::SearchDataSlotPos2`: first of 0x60 slots with switch on, `|marker.y - player.y| < 40` and distance <= radius + 10. Then: marker needs a character (`+0x2c`) -> OK press runs it (`DAT_01d567f4 = 1`); **auto marker (`+0x24`, cfg 5th flag)** -> `EdFadeOut(0,0,0,120)`, `gameTask = 500` (next frame 400); item-use marker (`+0x30`) + button 0x80 -> mode 2; plain OK -> mode 1. Walking on a non-auto marker does nothing. `DAT_01d56774 = def+0x38` (cfg 7th flag, "ext" = heavy). A second, exact-circle test (`SearchDataSlotPos`, no y test) handles character-gated slots (always mode 1).
2. **Monster-requested** (`CMonstorUnit +0x94`, set by monster ext 107 `_STATUS_SET_EVENT(id)` and 111 `_RUN_SCRIPT(id)`): `DAT_01d56774 = 1` (heavy), `ResetStatusInfo`, `driveStepHold = 1` (monsters/effects frozen), `EdFadeInit; EdFadeOut(120 frames)`, `DAT_01d567d0 = 0` (the event starts from black), `gameTask = 0x226`. At 0x226, when the fade is done: reset all 5 shot effects, **`CleanViewMonstor` (every monster of the floor is removed)**, `+0x94 = -1`, `driveStepHold = 0`, `gameTask = 400`. This is the boss death path.
3. **Per-monster death event** (`CMonstorUnit::CheckEventFlag2`, 0x1d7f30, disassembled): scans the 16 view slots for state 2 with `slot+0x1e4c8 != 0`; clears it and returns `slot+0x1e4c6`. `+0x1e4c6` is the 6th argument of `_SET_MONSTOR_ID` (-1 default); `+0x1e4c8` is set to 1 in the damage code when the monster's HP drops below 1 (and `+0x1e4c6 != -1`). Runs as an event at once: heavy 0, no fade (`DAT_01dc44f0 = 0`). dccore already has the fields (`death_event` / `death_event_pending` in `monster_ai.h`; only `monster_combat.cpp:697` touches them: nothing writes `death_event` and nothing consumes the pending flag).
4. **Floor cleared** (`_CLEAN_MONSTOR_SCRIPT_NO(fid [, heavy])` armed `DAT_01d567e0/e4`): when `GetMonstorNum() < 1` (0x1d7a40, disassembled: number of the 16 view slots whose state field `+0x1e3d0` is not -1, i.e. allocated slots, helper monsters included) and the player is alive -> run `fid`, request cleared.
5. Player dead -> `gameTask 200` flow -> function 5. Trap chests -> 15/16.
6. `_RUN_SCRIPT_NO` request (1.4).

`gameTask 400` (READ): `LockOffTargte`, clear message state, `DAT_01d56770 = DAT_01d5676c`, `DAT_01d5676c = -1`, `BtSystemScriptInit` (event camera `SubCamera` seeded from the gameplay camera, `NowCamera = SubCamera`, 6 NPC slots = `NPCUnit`, hero = `CharaMain`, windows 1/2 = `BtEventMes0/1`, `EdFadeInit` unless `DAT_01d567d0 == 0`, object handles cleared, `UserStatus+0x8b0c = 1`). If heavy: delete textures, reset shots, **clean all monsters**, switch `read_buffer` to the big event area (memory only). Then `EdEventInit(fid)`; if the function ends at once -> `BtSystemScriptAfter`, `gameTask = 0`; else `gameTask = 401` (0x191).
`gameTask 0x191` (READ): each frame `EdEventMode(NowCamera)` (the same per-frame step as the town: windows, ASQ, camera, fades; result code > 0 ends the event) and the request dispatcher. On end: `BtSystemScriptAfter` (`BtEventMode = 0`, `EdEventAllClear`, camera back to the follow camera per `_RESET_CAMERA/_RESET_CAMERA_ANGLE`, `BtMapJumpFloor = -1`, `NowCamera = MainCamera`, `UserStatus+0x8b0c = 0`), `gameTask = 0`; result code 8 (`_MAP_JUMP`) sets `existFlag = 1` (leave the dungeon, main loop does `MapJump`). `_SET_RETURN_CODE(1)` is how event scripts end normally.

### 1.4 Request dispatcher `DAT_01d567d8` (VERIFIED, jump table at 0x01dc3010 in DUN.BIN, handlers disassembled)
| request | set by | handler (0x01db...) | effect |
|---|---|---|---|
| 1 | `_ITEM_USE_WINDOW` | 4c4c | clears the request, `BtMiniItemSelect` init (0x1d3400), `gameTask = 0x19a` (item picker; on close back to 0x191, result written through `DAT_01d567c0`) |
| 2 | `_SET_URA_DUNGEON` | 4c6c | clears, `gameTask = 0xa0` (swap main/back floor) |
| 3 | `_OPEN_ENTRANCE_WINDOW` | 4c84 | clears, `InitDunEnterMenu(0x17, selectMapNo, -1)` (floor picker, result through `DAT_01d567c4`), menu mode 4 |
| 4 | `_GO_DUNGEON` | 4ce4 | clears, `BtSystemScriptAfter`, camera follow reset, `gameTask = 0`, battle state reset: **this is what ends the floor-start flow** |
| 5 | `_RUN_SCRIPT_NO` | 4e0c | clears, `BtSystemScriptAfter`, `DAT_01d5676c = DAT_01d567dc`, `gameTask = 400` (next function, heavy = `DAT_01d56774`) |
| 6 | `_OPEN_ESCAPE_WINDOW` | 4cb0 | clears, escape window (0x20ef90), menu mode 7 (result through `DAT_01d567c8`) |

### 1.5 Marker cfg and event ids (VERIFIED-DATA)
`PT_MARKER "name", radius, event_id, ITEM_USE, AUTO, SWITCH, EXT` (the Japanese header comment in the cfgs: "name, range, event id, item-use on/off, auto-start on/off, switch, extended memory"). SWITCH = initially enabled, EXT = heavy. A marker is an empty object of a part's collision mesh (position/rotation from the object). `_SET_EVENT_SW(id, on)` switches every slot with that id.

Event id -> marker (`dungeon_events.py markers` prints all 400 lines):

| id | marker names | flags (item/auto/switch/ext) | meaning |
|---|---|---|---|
| 160 | `out_2` | OFF OFF OFF OFF (d06/d07: item-use ON, switch ON) | exit stairs; enabled by the gate script (`_SET_EVENT_SW(160,1)`) |
| 161 | `gatekey_2` (d03 boss-adjacent floor 16 uses id 400 `gatekey_3`) | ON OFF ON OFF | gate key door |
| 250 / 251 | `ndoorkey`, `ndoorkey1/2` | ON OFF ON OFF | locked doors |
| 200 / 201 | `ura_2`, `hitsugi` (d04), `ita` (d05) | ON OFF ON OFF | back-floor entrances |
| 350 351 | `chr<c>_<k>` | OFF OFF ON OFF | character-gated jump gaps (c = character index) |
| 375, 365.. | `chr5_*` | OFF OFF ON OFF | Osmond/flight rooms (d05+) |
| 102 | d01 `event02` (inter2, floor 3) | OFF ON ON ON | **duel/mini-boss room start**, radius 40 |
| 300 | d01 `event00_2` (inter, floor 7), radius 50 | OFF ON ON ON | story scene of floor 7 |
| 301, 302 | d01 `chr1_1_2`, `chr1_2_2` | OFF OFF ON OFF | tram |
| 111, 300 | d02 `event10_1`, `event10_2` | | story-room events |
| **400** | d01 `bosskey_2` ("horned key"), radius 20 | ON OFF ON OFF | boss key door |
| **500** | d01 `event01_2` ("boss battle start"), radius 20 | OFF **ON** OFF **ON** | boss start: auto + heavy, initially disabled |
| 400 500 800 520 | d02 `bosskey_2`, `event11_2`, `event12`, `out_2` | 400: ON OFF ON ON; 500: ON ON ON ON; 800: all OFF; 520: OFF ON ON OFF | d02 boss floor |
| 400 401 402 403 | d04 `bosskey_moon_2`, `bosskey_sun_2`, `out_2`, `event31_2` | | two keys, exit, boss start |
| 404 405 | d05 `event41_2`, `event42_2` | | boss key, boss start |
| 400 500 | d07 `bosskey_2`, `event01_2` | | **no script function exists** (the markers are dead; boss spawned from 199) |

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 2. Floor start and the three map kinds

Function 100 (READ + VERIFIED with `dungeon_trace.py`): `_GET_FLOOR_LEVEL`, `_GET_OLD_FLOOR_LEVEL`; if old == -1 (first entry) the floor comes from `_OPEN_ENTRANCE_WINDOW(&floor)` (the picker, request 3), else `floor + 1` and `_SET_FLOOR_LEVEL(floor)`. d03/d04/d06/d07 first test `_GET_DUNGEON_FLOOR(&v)` (ext 22 = `BtMapJumpFloor`, set by the story maps' `_SET_DUNGEON_FLOOR(f)` before `_MAP_JUMP(n+200)`; `>= 0` skips the picker; reset to -1 by the first `BtSystemScriptAfter`). Then `_SET_DUNGEON_MAP("name", kind)` (BtLoadMapType; the last call wins), `_ALL_DRAW_FLAG(0)`, `_LOAD_DUNGEON_MAP2()` (BtMapJumpLoad: reads the cfg + mpd, sets `UserStatus+0x8b10 = -1`), `_SET_RANDOM_MAP()`:
* kind **1** (`NowDngMap+0xbdec == 1`): `BtCleatRandomMap` = random floor (ported); then `_LOAD_MONSTOR()` (= `BtLoadMonstor(0)` + `BtArrengeMonstor`, ported).
* kind **0** (story room, `inter`/`inter2`) and **2** (boss): `BtCleatFreeMap` = fixed map, every part of the cfg placed once. Function 100 calls `_LOAD_MONSTOR` only when its local `v2 == 1` (random floors; 0 for kind 0, 2 for kind 2; traced for d01 floors 3, 7, 14), so story rooms and boss rooms start **empty** and every monster there comes from `_SET_MONSTOR_ID` in a scene/marker function (d01 floor 3: function 102; d03 floor 8: function 198 / 100; boss floors: section 4).
Then the per-floor part (door/gate objects by frame name, event switches from game flags, optional story scene), `fn_34e14` (= function 150, the stair arrival scene), flag-gated first-visit scenes, `_GO_DUNGEON()`.

`dcfloormap` already reproduces the map choice. d07 has 100 floors (`d07main_a` 0..19, `_b` 20..39, `_c` 40..59, `_d` 60..79, `_e` 80..97, `_f` 98, boss 99); dcfloormap prints only 40. d06 floors 24/25 come from a different branch of 100 (`d06boss2` / `d06boss`, section 4.4).

**Layout rows of the story/boss floors** (`boss_layouts.py`, `BtEnemyLayoutList`; "k:" = index used by `_SET_MONSTOR_ID(k, ...)`, kind = monster-row kind):
```
d01 floor 3  (inter2) 0:e03a(kind0)                                   d03 floor 8 (inter) 0:e28a 1:e27a 2:e25a (kind 0)
d01 floor 14 (boss)   0:c12a(kind2)
d02 floor 16 (boss)   0:c14a                                          d03 floor 17 (boss) 0:c13a 1:baria 2:korinoya 3:kori 4:i_meteo 5:c13_reiki 6:i_tatumaki
d04 floor 8 (inter)   0:e90a 1:e91a                                   d04 floor 17 (boss) 0:c15a 1:c15b
d05 floor 14 (boss)   0:c16a 1:e85a                                   d06 floor 24: 0:c17a 1:c17b 2:c17c 3:c17_hikari 4:c17_kaze 5:c17_beem 6:c17_beem_s 7:c17_syougeki
d06 floor 25: 0:c23a 1:e124a 2:c23_hasira 3:c23_syougeki 4:c23_beem 5:c23_beem_s                d07 floor 99: 0:c22a 1:c21a
```
So "monster id" in `_SET_MONSTOR_ID` is an index into the models loaded for the floor, in layout order (INFERRED for the multi-stem case: `BtLoadMonstor` advances the index by the number of models `SetupBaseModel` returned).

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 3. External functions

### 3.1 The DUNGEON table (ids 1010..1101; 67 entries, `_TEST` 1000 belongs to the EVENT table; 61 distinct ids are used by the seven scripts)
Generated by `dungeon_events.py ext`; text = `event_ext.csv` unless the row says READ/VERIFIED (corrections made while reading the decompile). Last column = static call sites in d01/d02/d03/d04/d05/d06/d07.

| id | name | args | semantics | sites d01..d07 |
|---|---|---|---|---|
| 1000 | _TEST | `()` | no-op test function. | 0/0/0/0/0/0/0 |
| 1010 | _GET_FLOOR_LEVEL | `(&out)` | out = UserStatus byte +2 = current dungeon floor (0-based). | 3/2/2/3/3/4/3 |
| 1011 | _SET_FLOOR_LEVEL | `(floor)` | CDngStatusData::SetNowFloor: previous floor := current, current := floor, deepest floor updated. | 2/2/3/3/2/5/2 |
| 1012 | _GET_OLD_FLOOR_LEVEL | `(&out)` | out = byte +3 = previous floor (-1 = just entered). | 1/1/1/1/1/1/1 |
| 1020 | _GET_ACTION_MODE | `(&out)` | out = DAT_01d56764 "action mode" of the current event: 0 = triggered by walking onto the marker, 1 = player pressed OK on it, 2 = pressed the 0x80 button (item use) on a marker that accepts it. | 5/6/5/6/5/4/4 |
| 1021 | _ITEM_USE_WINDOW | `(&result, item_id, ...)` | READ. Request 1: opens the item picker (BtMiniItemSelect) restricted to the listed ids (-1 terminated list at DAT_01d5677c; a single argument = no restriction); result = chosen item id or -1. While it is open the event VM is not resumed (gameTask 0x19a). | 4/5/4/5/4/3/3 |
| 1022 | _OPEN_ENTRANCE_WINDOW | `(&result)` | Request 3: floor-select ("entrance") window; result receives the chosen floor. | 1/1/1/1/1/1/1 |
| 1023 | _CHECK_EVENT_FLG | `(idx 0..49, &out)` | ChkEventFlag: run-local event flags byte array UserStatus+0x8aa4. | 1/1/1/1/1/2/2 |
| 1024 | _SET_EVENT_FLG | `(idx, v)` | set that flag. | 1/1/1/1/1/2/2 |
| 1030 | _GET_OBJHDL | `(handle 0..31, part_idx, "frame")` | BtObjHdl[handle] := the frame named "frame" in the floor model: part_idx -1 searches the whole floor (CDungeonMap::GetFrameSearch), else inside placed piece part_idx (CDungeonParts::GetSearchFrame at NowDngMap+0x490+part*0x1d0). On failure prints a message and the handle becomes null. | 59/75/79/122/105/84/21 |
| 1031 | _SET_OBJHDL_POS | `(handle, x,y,z)` | Move the frame (type 0) or the NPC character (type 1) of the handle. | 41/21/36/91/6/30/10 |
| 1032 | _SET_OBJHDL_ROT | `(handle, rx,ry,rz)` | Rotation (SetRotType 2 + angles). | 4/0/4/0/70/4/6 |
| 1033 | _SET_OBJHDL_DRAW_FLAG | `(handle, show)` | Frame draw flag word := 1 (show) / 2 (hide) (+0xb0 and +0). | 15/54/45/59/36/51/11 |
| 1034 | _GET_OBJHDL_POS | `(handle, &x,&y,&z)` | World position of the frame / NPC. | 9/4/4/5/5/6/2 |
| 1035 | _GET_OBJHDL_ROT | `(handle, &rx,&ry,&rz)` | Rotation out: (0, yaw from the frame's world matrix, 0) or NPC rotation. | 9/4/4/5/5/6/2 |
| 1036 | _SET_URA_DUNGEON | `()` | Request 2: switch between main and "ura" (back) dungeon (loop state 0xa0). | 1/1/1/1/1/1/1 |
| 1040 | _GET_EVENT_POS | `(&x,&y,&z)` | Position of the event marker that started the event (DAT_01d56740..). | 2/2/1/2/2/1/1 |
| 1041 | _GET_EVENT_ROT | `(&rx,&ry,&rz)` | Rotation of that marker (DAT_01d56750..). | 1/1/0/1/1/0/0 |
| 1050 | _GO_DUNGEON | `()` | VERIFIED (disassembly of request handler 4 at 0x01db4ce4). Request 4: BtSystemScriptAfter (leave event mode, camera back to the follow camera), gameTask = 0; the floor-start script may still be running its last yields. Last call of 90/100/198/199. | 3/2/5/4/2/4/2 |
| 1051 | _SET_DUNGEON_MAP | `("mapname", kind)` | BtLoadMapFileName = name (e.g. "d01main_a"), BtLoadMapType = kind (1 random-generated map; other = hand-built free map). | 5/4/5/4/4/6/7 |
| 1052 | _LOAD_DUNGEON_MAP2 | `()` | UserStatus+0x8b10 := -1; BtMapJumpLoad(name buffer): load the map chosen by 1051. | 1/1/1/1/1/1/1 |
| 1053 | _LOAD_MONSTOR | `()` | BtLoadMonstor(0) + BtArrengeMonstor: load and place this floor's monster set. | 3/2/2/2/2/2/2 |
| 1054 | _SET_RANDOM_MAP | `()` | BtCleatRandomMap (kind 1) or BtCleatFreeMap: build the floor from the chosen map. | 5/4/4/4/4/4/3 |
| 1055 | _SET_EVENT_SW | `(event_id, on)` | CDungeonEventMan::SearchDataSwitch: enable (1) / disable (0) every marker slot with that event id (e.g. 160/161 stairs/gate). | 16/18/12/24/28/10/2 |
| 1056 | _SET_MONSTOR_ID | `(k [, load [, x,y,z [, death_event]]])` | READ. Spawns a live monster from the k-th base model loaded for this floor (BtEnemyLayoutList row of (dungeon, floor), models in load order; a monster row with extra model stems takes extra indices) into the first free view slot (16 slots, slot number = order of creation, boss scripts address their helpers by that number). `load` != 0 first calls BtLoadMonstor + CleanViewMonstor (needed after a heavy event wiped the monsters). 5/6 args give the position (2 args: the monster script function 1 places it itself, e.g. c12a `_SET_POSITION(200,40,0)`); the 6th argument is the id of an event function run when this monster dies (view slot +0x1e4c6, fired through CheckEventFlag2 as a normal event with heavy=0). Runs the monster script function 1 at once. | 5/1/15/8/4/17/2 |
| 1057 | _CHK_ATRA_HAVE | `(atra_no, &out)` | GetAtraTipNowHave(atra, character 0). | 1/0/0/0/0/0/0 |
| 1058 | _SET_ATRA | `(x,y,z, kind)` | CDungeonMap::SetAtraBoll: place an atra statue. | 2/0/0/0/0/0/0 |
| 1059 | _SET_IBOX | `(x,y,z, item)` | SetTreasureBox: place an item box. | 4/0/1/0/0/0/0 |
| 1060 | _GET_NOW_USER_ID | `(&out)` | current character 0..5 (UserStatus+4). | 7/9/7/10/9/9/1 |
| 1061 | _RUN_SCRIPT_NO | `(fid [, heavy [, keep_fade]])` | READ. Request 5: when the current event ends (or at once if the script yields) the loop runs function `fid` as a new event: DAT_01d56774 = heavy (2nd arg, default 0), DAT_01d567d0 = keep_fade (3rd arg; 0 = do not EdFadeInit at the start of the next event). Used as `_RUN_SCRIPT_NO(100)` after the exit stairs, `_RUN_SCRIPT_NO(199, 1, 0)` for the first-visit scenes. | 2/1/3/2/2/6/2 |
| 1062 | _GET_NPC_OBJHDL | `(handle, npc 0..3)` | Bind handle to dungeon NPC `npc` (CDungeonMap NPC array stride 0x1330, base +0xbdf0), kind 1. | 0/2/6/2/3/3/0 |
| 1063 | _SET_MOTION_OBJHDL | `(npc, motion [, speed, flags])` | CDungeonMap::NPCSetMotion on a dungeon NPC. | 0/7/12/1/1/5/0 |
| 1064 | _GET_GATEKEY_NO | `(&out)` | out = NowDngMap+0x464 gate-key item id of the floor. | 0/1/0/0/0/0/0 |
| 1065 | _USER_WEAPON_DRAW | `(flag)` | DAT_01dc44d8: draw the player's weapon. | 0/0/6/0/2/0/1 |
| 1066 | _CLEAN_MONSTOR_SCRIPT_NO | `(fid [, arg])` | READ. DAT_01d567e0 = fid, DAT_01d567e4 = arg (heavy flag): when CMonstorUnit::GetMonstorNum() < 1 (live monsters of the floor, decremented on each kill) and the player is alive the loop runs fid once as an event and clears the request. | 1/0/0/0/0/0/0 |
| 1067 | _SET_MAIN_CHR2 | `(chara 0..5 [,flag])` | selectChrUnit(chara, flag): force the active character for the event (remembered for ext 1071). | 1/1/1/2/1/1/1 |
| 1068 | _SET_LIMMIT_ZONE | `(zone)` | READ. UserStatus+0x8b10 = zone (-1 none); loads sound set 0x1b. Zone is the "limit zone" of the floor (time limit/dark zone effect), restored by 1080. | 2/2/2/2/2/5/0 |
| 1069 | _SET_DEAD_FLAG | `()` | CDngStatusData::SetDead (kills the player: game over flow). | 0/0/0/0/0/0/0 |
| 1070 | _OPEN_ESCAPE_WINDOW | `(&result)` | Request 6: "escape the dungeon?" window (BtEscape); stops battle BGM. | 1/1/1/1/1/1/1 |
| 1071 | _RESET_MAIN_CHR | `()` | restore the character saved by 1067. | 0/0/0/0/0/0/0 |
| 1072 | _ALL_DRAW_FLAG | `(flag)` | BtAllDrawFlag: hide/show the whole 3D dungeon (used around map swaps). | 4/4/4/6/8/4/4 |
| 1073 | _SET_FLOOR_TITLE | `()` | FloorTitleOn: show the floor name banner. | 1/1/1/1/1/1/1 |
| 1074 | _GET_RUBY_ELEMENT | `(&out)` | BtRubyDoorKey (element needed by the Ruby door). | 0/0/4/4/4/4/0 |
| 1076 | _SET_FLOOR_TITLE_OFF | `()` | DAT_01d567cc = 1: suppress the floor title. | 0/0/1/0/0/2/0 |
| 1077 | _INIT_BEE | `(npc)` | InitBee(frame of NPC, 15): bee swarm effect. | 0/0/0/2/0/0/0 |
| 1078 | _END_BEE | `()` | stop the bee effect. | 0/0/0/2/0/0/0 |
| 1079 | _EASTKING_COMPLETE | `(&out)` | EastKingCheckComplete. | 0/0/0/0/0/1/1 |
| 1080 | _SET_RES_LIMMIT_ZONE | `()` | SetResLimmitZone: restore zone. | 1/1/1/1/1/1/0 |
| 1081 | _GET_ITEM_TRAPID | `(&out)` | trap id of the current item box (IBox index DAT_01d567ec). | 1/1/1/1/1/1/1 |
| 1082 | _RESET_ITEM_TRAP | `()` | reset item trap state. | 3/3/3/3/3/3/3 |
| 1083 | _BOM_SET | `()` | SetBombEffect at the player (bomb trap) and DAT_01dc45c8 = 30. | 1/1/1/1/1/1/1 |
| 1084 | _SET_STATUS_ERR | `(status)` | BtSetStatusErr(status) + damage sphere at the player. | 2/2/2/2/2/2/2 |
| 1086 | _CHECK_MARDAN | `(&out)` | SaveData+0x12f94 (Mardan story flag). | 1/1/1/1/1/1/1 |
| 1087 | _NO_RESET_CHARA_NO | `()` | DAT_01d567f0 = 1: do not reset the character number afterwards. | 0/0/0/0/0/0/0 |
| 1088 | _SET_RUBY_ELEMENT | `(v)` | no-op. | 0/0/1/0/0/0/0 |
| 1089 | _CHECK_CHR_HELP | `(&out)` | DAT_01d567f4: 1 when the event came from a character-room marker pressed with OK. | 0/2/4/5/5/5/0 |
| 1090 | _HOLD_ITEM_EVENT | `(event_id)` | SearchItemEventHold on both event managers. | 1/1/1/0/3/1/1 |
| 1091 | _STOP_BATTLE_BGM | `()` | BtBattleMusic_Stop. | 0/0/0/0/0/0/0 |
| 1092 | _NO_STATUS_RECOVER | `()` | DAT_01d567f8 = 1: skip status-effect recovery. | 0/0/0/2/0/0/0 |
| 1093 | _SET_QUEST_DUNGEON | `(dungeon, v)` | CSaveData::QuestDungeon(dungeon, v). | 0/0/0/0/0/0/0 |
| 1094 | _SET_NPC_ON_OFF | `(npc, on)` | dungeon NPC enable flag (+0xcfc8). | 0/2/0/0/0/0/0 |
| 1095 | _GET_MAP_CODE | `(&out)` | dungeon number 1-based (UserStatus[0]+1). | 0/0/0/1/1/1/0 |
| 1096 | _SET_ACTIVE_ITEM_ICON | `()` | LoadActiveItemIcon. | 0/0/1/1/0/2/0 |
| 1097 | _GET_ITEM_UNIT_NO | `(weapon_id, &chara)` | which character a weapon id belongs to (0..5, -1 none). | 1/1/1/1/1/1/1 |
| 1098 | _SET_IBOX_ANGLE | `(angle_f)` | IBox angle. | 2/2/2/2/2/2/2 |
| 1099 | _SET_IBOX_FINISH | `()` | IBox finish. | 1/1/1/1/1/1/1 |
| 1100 | _GET_PIERO_ITEM | `(&a,&b)` | GetPieroItem(map, ura). | 1/1/1/1/1/1/1 |
| 1101 | _CLEAR_DEAMON_SHAFT | `()` | ClearDeamonShaft. | 0/0/0/0/0/0/0 |

Additional facts (READ):
* All ext functions return at once; waiting is done by polling (`_LOAD_SYNC`, `_CHECK_FADE`, `_ASQ_CHECK`, `_CHECK_MES*`, `_GET_PADDOWN`) inside `while(..){yield;}`. The only things that suspend the VM are the modal requests 1 (item picker) and 3/6 (entrance/escape windows): the loop simply does not resume the VM while their game task is active.
* NPC slots: hero (-1) = `CharaMain` (so `_SET_NPC_POS(-1, ..)` at the end of a scene is how the player is placed in the arena and it persists after the event), slots 0..5 = `NPCUnit`. Windows: 1 = `BtEventMes0`, 2 = `BtEventMes1`; scripts use window 1 through the helper pattern `MES_MAKE; SET_MES_POS; SET_MES_AUTOSET; loop { yield; _CHECK_MES*/_GET_PADDOWN & 0x20 ... }` (events.md 4.2). Camera = `SubCamera`.
* Cut-scenes switch the active character: `fn_8940(0)` -> `_SET_MAIN_CHR2(0)` = `selectChrUnit(0)` (Toan); nothing restores it (`_RESET_MAIN_CHR` is unused and re-selects the character it stored last).
* `_SET_FLOOR_LEVEL` + `_RUN_SCRIPT_NO(100)` is the only floor transition; exit stairs 160 first open `_OPEN_ESCAPE_WINDOW(&r)` (descend / leave); leaving = `_MAP_JUMP(town)` (result code 8).
* Game state touched by the dungeon scripts, all through ext functions that exist in `EventRuntime` already: game flags `_SSET/_SGET_GAME_FLAG` (byte array), `_SSET/_SGET_GAME_INT_FLAG` (int 0 = story stage, int 10 = d06 stage), item `_SITEM_GET/_CHECK/_LOST`, `_SET/_CHECK_EVENT_FLG` (50 run-local bytes at `UserStatus+0x8aa4`).

### 3.2 Event-table functions the dungeon scripts need that `event_runtime.cpp` does not implement (VERIFIED by diffing `case` labels against the call set; 8 of 235)
`_GET_DUNGEON_FLOOR` (22, 4 sites) and the **EB duel mini-game** `_EB_INIT 950`, `_EB_SET_KEY 951`, `_EB_LOOP 952`, `_EB_SET_MOTION 953`, `_EB_INTRO_START 955`, `_EB_INTRO_LOOP 956`, `_EB_INTRO_END 957` (only d01 and d02 use them: d01 function 110 = the floor-3 duel, d02 has the same pattern; keys are `_EB_SET_KEY(frame, pad_mask)` with script-bit masks such as 32768 and 32 (OK)). Every other EVENT function used by a dungeon script has a `case` already (the cut-scene vocabulary: ASQ, camera, objects, sprites, sound...). Caveat: a `case` can exist without being correct for the dungeon variant (NPC array of 6, windows 1/2, `SubCamera`).

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 4. Bosses

### 4.1 What a boss is
A boss is an ordinary monster row (kind 2) with a script in `dun/monstor/<stem>.stb`; the difference is only *how it is created and how it dies*:
* Created by an **event script** with `_SET_MONSTOR_ID(k, 1)` (not by `BtArrengeMonstor`). The boss floor's `_LOAD_MONSTOR` is not called; `load = 1` makes `SetupViewMonstor`'s caller run `BtLoadMonstor` (models of the floor's layout row, 5 shot effects reset) and `CleanViewMonstor`. Position: none given for the boss itself; its script function 1 sets it (c12a: `_SET_POSITION(200, 40, 0)`, lock-on `_STATUS_SET_LOCKON_TRG("spine",3,3)`, clip level 1000, 11 `_SET_MOV_COL(frame, r)` moving body spheres and ~20 `_SET_DMG_COL/_PARA`). `SetupViewMonstor` runs function 1 immediately (`check_program(1)` / `run(1)`).
* Helper models (d03: 7, d04: 2, d05: 2, d06: 6/8, d07: 2) are created by additional `_SET_MONSTOR_ID(k, 0)` calls **in order 0,1,2..** (the view slot number = creation order; boss scripts address their helpers with `_SET_MONSTOR_POS/_MOVE/_MOTION(slot, ..)`, READ at 0x1e4a20..), d03's script even creates slot 6 three times.
* Dies: HP < 1 starts monster script function 120. **Every boss's function 120 is `_SET_MOVE_CANSEL; _STATUS_SET_MUTEKI(100); _RUN_SCRIPT(510)`** (VERIFIED-DATA: c12a c13a c14a c15a c16a c17a c21a c23a; no boss uses `_STATUS_SET_EVENT`). `_RUN_SCRIPT` sets `+0x94`; MoveChara then does the 120-frame fade and runs event 510 (1.3 point 2). dccore currently *ignores* monster ext 111 (`monster_ai.cpp:890`, "misc") and has no consumer for 107.
* d01 and d02 spawn the boss in the 500 event; d03 spawns at floor start (199 from 100); d04/d05 from their key/start markers; d06/d07 from 198/199.

### 4.2 Dungeon 1, floor 14 end to end (VERIFIED-DATA with `dungeon_trace.py`, READ for the engine side)
1. **Arrival** (floor 13 stairs -> `_SET_FLOOR_LEVEL(14)`; or picker). Function 100, floor 14 (`v0 == 14`): `_SET_DUNGEON_MAP("d01boss", 2)`, `_LOAD_DUNGEON_MAP2`, `_SET_RANDOM_MAP` (free map), no `_LOAD_MONSTOR`. Door state from flags (traced):
   * flag 24 = 0 (horned key not used): `boss_door`, `boss_door_a` (collision), `boss_door_v` (camera mesh) frames set to y = -56; event switches stay as the cfg: 400 on, 500 off.
   * flag 24 = 1, int flag 0 < 100 (boss alive): `_SET_EVENT_SW(400,0)`, `_SET_EVENT_SW(500,1)`, doors untouched (y = 0).
   * flag 24 = 1, int flag 0 >= 100 (boss already beaten): 400 off, 500 off, doors at -56.
   Then `_SET_NPC_POS(-1, ...)` and the arrival cut-scene 150 (hero walks out of `in_door`), `_RESET_CAMERA_ANGLE(3.14)`, `_GO_DUNGEON`. `boss_door*` are frames of the d01boss parts that scripts move (INFERRED: a bridge/lift - key use raises it from -56 to 0 in 60 frames with SE 233, the boss start sinks it again; the cfg comments name the part "landing (entrance)").
2. **Marker 400** (`bosskey_2`, item-use): mode 1 (OK): message 400 "There is a horn-shaped keyhole. (Use item with <button>)". Mode 2 (square, item picker `_ITEM_USE_WINDOW(&v, 207)`; 207 = "Horned Key", system bank message 307): if the picked item is 207 -> door rises (`fn_481c0`), `_SSET_GAME_FLAG(24, 1)`, `_SET_EVENT_SW(500, 1)`, `_SET_EVENT_SW(400, 0)`; other item -> a refusal reaction (`fn_1920(31)`: hero motion 31, turn toward the camera); none -> nothing. The key is not consumed (no `_SITEM_LOST`). Source of item 207: **OPEN** (not in any chest list, monster row or script).
3. **Marker 500** (`event01_2`, auto + heavy): the engine fades out 120 frames, event 500 starts (heavy: all monsters cleaned). Script: `_FADE_OUT(30)`, lower the three boss_door frames to -56, hide frame `odoriba` (landing), then if `_SGET_GAME_FLAG(32)` == 0: set it and play the **first-time intro** `fn_179ec` (1739 frames, loads `gedit/s98/chara/e15c01d.chr, e15c03c.chr, e15c12a.chr, f_boll_2.chr, boss.pak`, BGM 150, message 150 "Be careful, <name0>! Seems that Dran is being manipulated..."), else the short intro `fn_1e1f8` (621 frames, `e24c01d.chr, e24c12a.chr`, sprite `boss_1.img` title card). Both end with `_RESET_CAMERA_ANGLE(3.14)`, `_FINISH`, **`_SET_MONSTOR_ID(0, 1)`**, `_FADE_IN(60)`, `_SET_RETURN_CODE(1)`; then (same VM run) `_SET_EVENT_SW(500, 0)`. The last hero placement of the scene persists as the fight start position: (72, 0, -183.42) yaw -1.72 after the first-time intro, (45.61, 0, -180.99) yaw -0.08 after the short one (traced `_SET_NPC_POS(-1, ..)`).
4. **Fight**: normal monster loop with c12a (script functions 1, 100, 110, 120). The arena floor is **Dran's map field** (`CDranMapField`, cfg `DRANS_PARTS mot01..12.chr` + `DRANS_COLS mot01_a..12_a.mds`, only d01boss has them): 12 tiles with collision; every tile starts in state 3; each time `checkCollision`'s ground hit lands on a polygon whose attribute (+0x40 short) is 1..12 the tile's counter is decremented while > 1 (READ, `checkCollision` 0x1ab740); `CDranMapField::Step` (called from MoveChara) turns state 2 into "play SE 0x6c9, state 1", plays the tile's motion until frame >= 59 and then state 0; `AddCollision` only includes tiles with state > 1; `Draw` draws tiles with state != 0 (INFERRED: the floor crumbles under the player).
5. **Death**: boss HP < 1 -> function 120 -> `+0x94 = 510` -> MoveChara: freeze, 120-frame fade, wipe monsters/shots, event 510 (heavy). Script 510: `_SSET_GAME_INT_FLAG(0, 100)` then the clear scene `fn_1c2f8` (667 frames: `gedit/s98/chara/e16.chr`, `e16c12a_boss`, BGM 34, sprites `10.1.img`, `10.2.img`, messages 160..166 "...the Dark Genie..."), `_FADE_OUT(60, white)`, `_FINISH`, **`_MAP_JUMP(1, 300)`** -> main loop leaves the dungeon, Norune (e01) starts event 300.
6. **Rewards/state**: none at the boss itself (no chest, no `_SITEM_GET`, no Atla in 510). e01 function 300 (traced closure): `_SSET_GAME_INT_FLAG(0, 100)` (again) + `_SITEM_GET(238)` = "Worldmap" (system message 338). Deepest floor `UserStatus+0x42bf+0` was already set to 14 by `SetNowFloor` when floor 14 was entered. The Georama part unlock is Atla based (`BtAtraListMake`, floors' Atla statues), not boss based.

### 4.3 Death events of non-boss monsters (d03, d04; VERIFIED-DATA)
d03 floor 8 (`inter`, kind 0, three `e25a/e27a/e28a` guardians): `_SET_MONSTOR_ID(0, load, 50,0,-8, 197)`, `(1, 0, 0,0,-8, 196)`, `(2, 0, -50,0,-8, 195)` - the 6th argument is the event run when that monster dies; the flags 122/123/124 (set by 195..197) say which of the three are already dead (the script re-spawns only the live ones); flag 121 gates the first-visit scene 198 which spawns them with load 1. d04 floor 8 does the same with two monsters (196, 197). d01 floor 3 uses `_CLEAN_MONSTOR_SCRIPT_NO(110, 1)` instead (three `e03a`, `_SET_MONSTOR_ID(0, 1, 0,0,0)` + two at (+-30, 0, -30) with load 0).

### 4.4 Boss summary table (clear paths read from the closures of 100/400..405/500/510; d06/d07 are INFERRED from traces)
| dungeon | boss floor / cfg | boss + helpers | start | death -> clear |
|---|---|---|---|---|
| d01 | 14 `d01boss` | c12a (Dran) | 400 key 207 (flag 24) -> 500 (flag 32 intro) -> `_SET_MONSTOR_ID(0,1)` | 510: int0 = 100, scene, `_MAP_JUMP(1, 300)` |
| d02 | 16 `d02boss` | c14a | 400 (item from variable, flags 16/17), 500 (item-use+auto, flag 31) | 510: flag 34, NPC 0 on, `_SET_EVENT_SW(800,1)`, return to play; exit marker 520 -> `_MAP_JUMP(15)` = s04; 800 is a scene marker (all flags off); flag 48 set elsewhere (d02 300) and read by s01 |
| d03 | 17 `d03boss` (no marker) | c13a + baria korinoya kori i_meteo c13_reiki i_tatumaki (slot 6 x3) | floor start: 100 -> `_RUN_SCRIPT_NO(199, 0)` | 510: int0 = 300, `_MAP_JUMP_BGM_STOP(0)`, `_MAP_JUMP(45, 300)` = s34 event 300; floor 16 (`main_c`) has the `gatekey_3` marker (id 400, item 209 Music Box Key, flag 28, jumps to s34 events 302/303) |
| d04 | 17 `d04boss` | c15a + c15b | markers 400 (item 211 moon signet, sets flag 26) and 401 (item 210 sun signet, sets flag 27); whichever use completes the pair (both flags set; VERIFIED with `dungeon_trace.py d04 400 action=2 use=211 flag=27:1`) runs the intro `fn_99c4` (2894 frames, `_SET_MONSTOR_ID(0,1)`, `(1,0)`, return code 1); 403 (`event31_2`, no-op unless both flags) | 510: int0 = 400, `_SET_EVENT_SW(402,1)`; 402 (auto exit) `_MAP_JUMP(35, -1)` = s24 |
| d05 | 14 `d05boss` | c16a + e85a | 404 (item use, flag 30), 405/500 (flag 29) spawn 0,1 | 510: flag 56, int0 = 500, `_MAP_JUMP(5, 301)` = e05 event 301 |
| d06 (INFERRED) | floor index >= 24 (the last two floors; layout rows 24: c17a c17b c17c c17_hikari c17_kaze c17_beem c17_beem_s c17_syougeki, 25: c23a e124a c23_hasira c23_syougeki c23_beem c23_beem_s), maps `d06boss` / `d06boss2` | stage = game int flag 10 | 100 for index >= 24: int10 == 0 -> int10 = 1, flag 41 = 0, `_MAP_JUMP(26)` (= s16 story scene; s16 comes back with `_SET_DUNGEON_FLOOR(24)` + `_MAP_JUMP(206)`); int10 == 1 -> `d06boss` + `_RUN_SCRIPT_NO(199,1,0)`; == 2 -> `d06boss2` + 198; == 3 -> `_SSET_GAME_INT_FLAG(0, 600)` + 197 (finale) | 510: int10 = 2 (first boss) then 3 (second), flag 41 = 1, `_SET_DUNGEON_FLOOR(24)`, `_RUN_SCRIPT_NO(100,1,0)` re-enters 100 for the next stage; later story jumps to s39 (`_MAP_JUMP(50, 300)`), s28, s15. How the floor index (24 vs 25) and the layout row follow the stage is OPEN |
| d07 | 99 `d07boss` | c22a + c21a | 199 from 100 when int0 < 700 | 510: int0 = 700, `_SITEM_GET(298)`, return to play; later `_MAP_JUMP(61)` (= s50; n + 11 -> story map s<n>) from another function |

Boss scripts' special needs (monster side): `_RUN_SCRIPT` (111) and `_STATUS_SET_EVENT` (107) must set the pending event; `_SET_MOV_COL` (136, handled), `_SET_SND_FRM` (140, ignored), `_STATUS_SET_PALLET` (105, ignored), `_SET_MONSTOR_*` for helper slots (handled by the helper-slot code? not verified for boss floors).

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 5. State after a clear (what the town scripts read)

### 5.1 Story stage = game int flag 0 (VERIFIED-DATA over all 104 town/story scripts)
Town scripts read `_SGET_GAME_INT_FLAG(0)` against 100 (Norune: e01 + its 11 interiors, 12 files), 200 (Matataki e02 + interiors, 10 files), 300 (e03 + interiors + s04, 10 files; `< 300` in 8), 400 (e04 + interiors, 9 files), 500 (e05, s13 + interiors, 6 files). Setters: d01 510 and e01 300 -> 100; d03 510 -> 300; d04 510 -> 400; d05 510 -> 500; d06 100 -> 600; d07 510 -> 700. **Nothing sets 200** (d02 sets game flags 31, 34, 48 and the `inter` events instead); the `>= 200` branches of e02 are therefore only reached after dungeon 3 is cleared (300 >= 200). OPEN whether that is a retail quirk or a missed writer (`SetGameIntFlag` has only those script callers plus the fish counters).

### 5.2 Per dungeon (flags written by the dungeon scripts, read by others)
| dungeon | written | read elsewhere |
|---|---|---|
| d01 | int0 = 100 (510); game flags 24 (key used), 25 (floor-7 scene seen), 32 (boss intro seen), 37, 100..106 (first-visit scene of floors 1,5,0,4,6,2 and the floor-3 duel result 102); item 237 (floor 7 scene), 238 "Worldmap" (e01 300) | only d01 itself and e01 (int0) |
| d02 | flags 16, 17 (key), 31 (boss intro), 34 (boss dead), 48 (d02 300), 111 | 48 read by `s01` + its interior; the rest by d02 itself |
| d03 | int0 = 300; flags 28, 120..124 (120 written by s34 too) | by d03 / s34 |
| d04 | int0 = 400; flags 26, 27, 130..134 | 130/131 read by s32 |
| d05 | int0 = 500; flags 29, 30, 56, 140 | by d05 |
| d06 | int0 = 600, int10 = 1..3, flag 41 | by d06 |
| d07 | int0 = 700, item 298 | |

### 5.3 Everything else the engine already keeps
Deepest floor per dungeon `UserStatus+0x42bf+d` (signed byte, -1 = never entered; the floor picker lists up to deepest+1); visit counter `QuestDungeon(d)` (`CSaveData+0x12f88`, bumped by `InitDunEnterMenu`, read by `_SGET_DUNGEON_STATUS`, `GetVisitInfo` and the world map; the town menu entry "Go to Dungeon" is refused while `QuestDungeon(0) == 0`); Atla tables (`+0x278`, `+0x2078`, made on first entry); `UserStatus+4` character. A dungeon is entered either from its town gate event (d01: the Norune cut-scene ending in `_MAP_JUMP(1 + 200)`), from story maps that call `_SET_DUNGEON_FLOOR(f)` + `_MAP_JUMP(n + 200)` (s16 -> d6 floor 24, s24/s26/s32 -> d4, s25/s34 -> d3, s27 -> d5, s28 -> d6, s51 -> d7), or from the in-game menu; there is no "dungeon unlocked" flag, access follows the story maps and `QuestDungeon`.

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 6. Implementation plan

### 6.1 What exists (verified by reading the sources)
`EventRuntime` implements every EVENT-table function the dungeon scripts call except 8 (3.2); the DUNGEON table (1010+) is entirely `unknown_ext`. `dccore/event_world.*` hard-codes chests/gate/doors/exit. `FloorLayout::fixed`, markers (`floor_markers`), `MonsterWorld` (arrange, step, `death_event` field), `choose_floor_map` exist.

### 6.2 Smallest set for d01 floor 14 end to end (VERIFIED: union of the ext calls executed by 100 (3 flag states), 400 (modes 1/2, right/wrong key), 500 (both intros), 510 in the Python VM)
* **DUNGEON table, 18 ids**: 1010 `_GET_FLOOR_LEVEL`, 1011 `_SET_FLOOR_LEVEL`, 1012 `_GET_OLD_FLOOR_LEVEL`, 1020 `_GET_ACTION_MODE`, 1021 `_ITEM_USE_WINDOW`, 1030 `_GET_OBJHDL`, 1031 `_SET_OBJHDL_POS`, 1033 `_SET_OBJHDL_DRAW_FLAG`, 1034 `_GET_OBJHDL_POS`, 1035 `_GET_OBJHDL_ROT`, 1050 `_GO_DUNGEON`, 1051 `_SET_DUNGEON_MAP`, 1052 `_LOAD_DUNGEON_MAP2`, 1054 `_SET_RANDOM_MAP`, 1055 `_SET_EVENT_SW`, 1056 `_SET_MONSTOR_ID`, 1067 `_SET_MAIN_CHR2`, 1072 `_ALL_DRAW_FLAG`. (`_OPEN_ENTRANCE_WINDOW` 1022 only when entering from outside.)
* **EVENT table, 76 ids, all already implemented**: pad/flags/return code (1, 3, 700, 701, 711, 712), `_MAP_JUMP` 15, loading (32 33 34 35 38 39 44 56), NPC/hero (7 54 55 66 131 133 137 138 139 140 143 144 146 149 152), messages (192 193 195 196 197 200 201 202 204), objects (323 330), camera (422..427 436..440), fades (500 501 502), sprites (503 504), ASQ (600..617), sound (800..818).
* **Engine services** (the real work):
  1. A `DungeonSession` that is the `MoveChara` event layer: marker polling (section 1.3 rules, switch state `MarkerState::on`, auto/item-use/heavy flags, `_SET_EVENT_SW`), the `gameTask` 400/0x191/0x226/0x19a flow, the request dispatcher (1.4), `BtSystemScriptInit/After` semantics (hero = player actor, camera hand-over, windows 1/2, character forced to Toan), pending `_RUN_SCRIPT_NO`. Run `d01` function 100 at floor start instead of `choose_floor_map` alone (keep `choose_floor_map` as the cross-check).
  2. Named-frame access for `_GET_OBJHDL(h, -1, "boss_door")` and `_SET_OBJHDL_POS`: boss doors are **moving collision + camera + visual frames** (`boss_door`, `boss_door_a`, `boss_door_v`; `out_door_*`, `in_door`, `odoriba`, `outdoor_on/off` on other floors), i.e. a translation of a sub-mesh of a part, not the polygon removal of `FloorLayout::door_open`.
  3. Monster side: `MonsterWorld::spawn_view(k, load, pos?, death_event)` implementing `_SET_MONSTOR_ID`, the boss `_RUN_SCRIPT`/107 -> pending event with the freeze + 120-frame fade + wipe (`0x226`), the death-event consumer (`death_event_pending`), `GetMonstorNum()` for `_CLEAN_MONSTOR_SCRIPT_NO`. `monster_ai.cpp:890` currently drops ext 111.
  4. Front end: a cut-scene player over `EventRuntime` state (actors, ASQ, camera, fades, sprites, message windows) - already needed for the towns - plus the fixed-map loader for `d01boss` with the 12 Dran tiles (`CDranMapField`, section 4.2 point 4) for the breaking floor.
  5. Item 207 for testing: grant it (source OPEN).
* Cheapest first milestone ("B0"): headless, no presentation. Start with fast-forwarded cut-scenes (all polls answer done, as `dungeon_trace.py` does) so 100/400/500/510 run to their state changes in a few frames: floor 14 loads, key door, boss spawns, boss dies, `_MAP_JUMP(1,300)` surfaces as `EvNav::MapJump`. Cross-check against `dungeon_trace.py` logs. Then add the presentation.

### 6.3 Order of implementation
1. `DungeonSession` + DUNGEON ids 1010-1012, 1020, 1050-1052, 1054, 1055, 1067, 1072 (state only) + `_GET_DUNGEON_FLOOR`. Test: run 100 for every floor of every dungeon and compare the `_SET_DUNGEON_MAP` choice with `dcfloormap` (all 7 dungeons, 100 floors for d07).
2. Frame handles (1030-1035) and the moving-frame collision; marker polling with the exact trigger rules; 400 (1021) and 500.
3. `_SET_MONSTOR_ID` (1056) + boss `_RUN_SCRIPT` + the `0x226` path + 510. At this point d01 is playable; d02 only needs the post-boss exit markers 800/520 and `_SET_NPC_ON_OFF` (1094).
4. Remaining state functions in the order they appear in the closures: 1058/1059/1098/1099/1100/1081-1084 (chest/trap events 15/16), 1066 (clean-script), 1068/1080 (limit zone), 1061 (`_RUN_SCRIPT_NO`: floor change through 160), 1022/1070 (entrance/escape windows), 1036 (back floors), 1060, 1089/1074/1086 (chara rooms), 1090.
5. Presentation-only ids (`_SET_FLOOR_TITLE`, `_INIT_BEE`, `_SET_ACTIVE_ITEM_ICON`, `_USER_WEAPON_DRAW`) last.

### 6.4 What the other dungeons add
* **d02**: two-stage boss area (500 item-use+auto marker, 800/520 exit markers, `_SET_NPC_ON_OFF` (only d02), `_SET_EVENT_SW(800)`), `_SITEM_LOST(240)` at 300, an EB duel (`_EB_*`, with d01 the only users).
* **d03**: boss spawned by floor start (no marker); 7 models incl. slot 6 three times; guardians with death events 195-197 on floor 8, flags 120-124; `gatekey_3` floor-16 marker (id 400, item 209) with story-map jumps; `_SET_RUBY_ELEMENT`, `_SET_IBOX(-73,0,-87,201)`; `_MAP_JUMP_BGM_STOP`.
* **d04**: two boss keys (moon/sun signets 210/211, flags 26/27), `_NO_STATUS_RECOVER`, `_SET_WORK_FLAG`, two bosses, exit marker 402 and coffin back-floor (201); story jumps with `_SET_DUNGEON_FLOOR(-1)`; the bee swarm (`_INIT_BEE/_END_BEE`, only d04; `SetupViewMonstor` also calls `InitBee` itself for dungeon 4 floor 17 view slot 1, READ).
* **d05**: boss key use (404) + `_HOLD_ITEM_EVENT(404)`, two bosses, `ndoorkey2` (251), jump to e05 with event 301, plank back-floor.
* **d06**: 26 floors, two boss floors in sequence (`_SET_DUNGEON_FLOOR(24)` + `_RUN_SCRIPT_NO(100,1,0)`), int flag 10 state machine, flag 41, five-element `_SET_LIMMIT_ZONE` values, helper bosses with 6-8 models, `_GET_RUBY_ELEMENT`.
* **d07**: 100 floors, final boss with two fights (c21a, c22a), no chara-room scripts, `_SITEM_GET(298)`, `_MAP_JUMP(61)` (= s50).
* **All**: character rooms 350-375 (jump gaps need `UserStatus+4` and the marker's required character), floor-3-style duels (EB mini-game 950-957), first-visit tutorial slide shows (`e29..e32_N.img` sprite sequences with narration) which can be skipped without changing state (they only set the flags 100..106 etc.).

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 7. Reproducing / checking

```
python -I tools/dc/dungeon_events.py usage | funcs | markers | ext
python -I tools/dc/dungeon_events.py closure d01 500      # every ext call reachable from 500
python -I tools/dc/dungeon_events.py state d01 510        # only state-changing calls
python -I tools/dc/dungeon_trace.py d01 100 floor=13 old=13 flag=24:1 int=0:0       # floor-14 start with the key used
python -I tools/dc/dungeon_trace.py d01 400 action=2 use=207
python -I tools/dc/dungeon_trace.py d01 500 [flag=32:1] frames=3000
python -I tools/dc/dungeon_trace.py d01 510 frames=3000
python -I tools/dc/boss_layouts.py [all] [dungeon ...]
native/build/core_release/dcfloormap.exe "iso/Dark Cloud (USA).iso"       # map choice per floor
```
Traces answer every poll at once, so a 1739-frame intro prints in ~30 lines; add `all` to see camera/ASQ/message calls.

## 8. Open questions
1. **Source of item 207 (Horned Key)**: not a chest-list entry (checked all 14 put lists), not a monster row field, not an `_SITEM_GET` literal in any town/dungeon script, not special-cased by `SetKey`. Probably a variable `_SITEM_GET(v)` or a Georama/request reward in a town. d04's keys 210/211 and d03's 209 *are* given by town scenes (e04 i04h06/i04h03, s09 s1001).
2. Which of y = 0 / -56 is "closed" for `boss_door*` (INFERRED bridge); needs the frame offsets of `d01g05_0.mds` or a live check.
3. Exact visual continuity of the auto-marker fade (the 120-frame `EdFadeOut` of the walk-in trigger is reset by `EdFadeInit` at event start unless `DAT_01d567d0 == 0`).
4. int flag 0 = 200 is never written (5.1).
5. Whether the helper "monsters" (`_SET_MONSTOR_ID(k, 0)` for k >= 1) of d03..d07 are visible/active at creation or hidden until the boss script activates them (their function 1 is not read here).
6. `CDranMapField` trigger details (how many ground hits per tile, what polygon attribute 1..12 maps to which `mot` file) - READ from `checkCollision`, not run.
7. The EB duel scoring (events.md section 9.8), needed for d01 floor 3, which sits before the boss on a normal run (flag 102 gates the exit; the duel script itself offers "Try again / back to Norune").
8. Death event of the player: function 5 plays the scene; what follows (game over screen vs continue, `DeadKeyWait 0x168`) is in `gameTask 200` and not read here.

---------------------------------------------------------------------------------------------------------------------------------------------------------

## Appendix A. Ext functions used by each dungeon script (static call sites, `dungeon_events.py usage`)

One row per external function id (EVENT table 0..1000 and DUNGEON table 1010..1101) that appears in any of the seven `dun/script/d0N/event.stb`; the cell is the number of call sites in that script (internal helper functions included, a call site in a loop counts once). 235 distinct ids in total (per dungeon: d01 197, d02 180, d03 164, d04 173, d05 171, d06 169, d07 155). Names are the game's own (`notes/event_ext.csv`).

| id | name | d01 | d02 | d03 | d04 | d05 | d06 | d07 | total |
|---|---|---|---|---|---|---|---|---|---|
| 0 | _GET_PADON | 4 | 2 | 2 | 2 | 2 | 2 | 3 | 17 |
| 1 | _GET_PADDOWN | 4 | 5 | 5 | 4 | 4 | 4 | 4 | 30 |
| 3 | _SET_RETURN_CODE | 4 | 7 | 1 | 4 | 3 | 1 | 1 | 21 |
| 4 | _GET_RANDOM | 2 | 2 |  |  |  |  |  | 4 |
| 7 | _SET_WORLD_COORD | 15 | 7 | 6 | 8 | 8 | 8 | 3 | 55 |
| 8 | _INITIALIZE | 28 | 16 | 9 | 12 | 14 | 14 | 6 | 99 |
| 10 | _DRAW_EXCLAMATION_MARK |  | 1 |  |  |  |  |  | 1 |
| 12 | _SKIP | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 8 |
| 14 | _FINISH | 13 | 10 | 3 | 6 | 6 | 7 | 2 | 47 |
| 15 | _MAP_JUMP | 4 | 4 | 5 | 4 | 3 | 3 | 1 | 24 |
| 16 | _GET_OLD_MAPNO |  |  |  | 2 |  |  |  | 2 |
| 21 | _SET_DUNGEON_FLOOR |  |  |  | 1 |  | 2 |  | 3 |
| 22 | _GET_DUNGEON_FLOOR |  |  | 1 | 1 |  | 1 | 1 | 4 |
| 23 | _MAP_JUMP_BGM_STOP |  |  | 1 |  | 1 |  |  | 2 |
| 32 | _SET_CURRENT_DIR | 31 | 17 | 7 | 9 | 10 | 11 | 4 | 89 |
| 33 | _LOAD_CHR_FILE | 56 | 35 | 17 | 24 | 26 | 34 | 16 | 208 |
| 34 | _LOAD_SYNC | 3 | 3 | 2 | 2 | 2 | 2 | 2 | 16 |
| 35 | _LOAD_CHARA | 66 | 43 | 14 | 25 | 25 | 29 | 11 | 213 |
| 36 | _LOAD_CHARA_TEXTURE | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 8 |
| 38 | _CLEAR_VILLAGER_BUFF | 17 | 6 | 5 | 4 | 6 | 5 | 4 | 47 |
| 39 | _CLEAR_EVENT_BUFF | 23 | 10 | 7 | 8 | 10 | 10 | 5 | 73 |
| 40 | _CLEAR_EVENT_EXBUFF | 29 |  |  |  |  |  |  | 29 |
| 41 | _LOAD_SCENE | 2 | 3 | 2 | 2 | 4 | 2 | 1 | 16 |
| 42 | _SYNC_SCENE_CHARA |  |  |  |  | 2 |  |  | 2 |
| 43 | _SYNC_SCENE_CAMERA | 2 | 3 | 2 | 2 | 4 | 2 | 1 | 16 |
| 44 | _ACTIVE_FILE_BUFFER | 87 | 31 | 13 | 22 | 25 | 30 | 12 | 220 |
| 45 | _RELEASE_SCENE_CHARA |  |  |  |  | 2 |  |  | 2 |
| 46 | _RELEASE_SCENE_CAMERA | 2 | 3 | 2 | 2 | 4 | 2 | 1 | 16 |
| 47 | _DELETE_CHARA | 6 | 2 | 2 | 2 | 1 | 1 | 1 | 15 |
| 49 | _LOAD_ITEM_FILE | 3 | 2 | 2 | 2 | 2 | 2 | 2 | 15 |
| 50 | _LOAD_ITEM | 3 | 2 | 2 | 2 | 2 | 2 | 2 | 15 |
| 51 | _SYNC_CHARA_ITEM | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 8 |
| 53 | _DELETE_ITEM | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 8 |
| 54 | _SYNC_CHARA_CHARA | 5 | 6 | 1 | 1 | 1 | 2 | 2 | 18 |
| 55 | _RELEASE_CHARA_CHARA | 3 | 3 | 1 | 1 | 1 | 2 | 2 | 13 |
| 56 | _LOAD_SPRITE_TEXTURE | 36 | 3 |  | 2 | 1 | 1 | 1 | 44 |
| 66 | _TURN_CHARA | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 14 |
| 67 | _SET_CHARA_POS | 3 |  |  |  |  |  | 1 | 4 |
| 68 | _SET_CHARA_ROT | 1 |  |  |  |  |  | 1 | 2 |
| 73 | _SET_SCENE_POS | 2 | 2 | 2 | 2 | 2 | 2 | 1 | 13 |
| 75 | _SET_SCENE_ROT | 2 | 2 | 2 | 2 | 2 | 2 | 1 | 13 |
| 131 | _GET_NPC_POS | 3 | 1 | 3 | 7 | 4 | 3 | 3 | 24 |
| 133 | _SET_NPC_MOTION | 92 | 17 | 18 | 16 | 17 | 29 | 10 | 199 |
| 137 | _SET_NPC_POS | 100 | 18 | 20 | 24 | 15 | 32 | 16 | 225 |
| 138 | _SET_NPC_ROT | 80 | 12 | 14 | 15 | 9 | 24 | 13 | 167 |
| 139 | _GET_NPC_ROT | 1 | 1 | 2 | 1 | 1 | 1 | 1 | 8 |
| 140 | _NPC_DRAW | 105 | 31 | 21 | 39 | 29 | 23 | 17 | 265 |
| 142 | _NPC_COL |  | 1 | 1 | 1 | 1 | 1 | 1 | 6 |
| 143 | _INIT_NPC_CLOTH | 8 | 4 | 2 | 1 | 6 | 1 | 1 | 23 |
| 144 | _NPC_DRAW_SHADOW | 5 | 7 | 7 | 7 | 10 | 2 | 4 | 42 |
| 145 | _SET_NPC_ANIME | 23 |  |  |  |  |  |  | 23 |
| 146 | _SET_NPC_AMBIENT | 21 | 4 |  | 6 | 5 | 7 |  | 43 |
| 147 | _SET_NPC_SCALE | 14 | 2 | 2 | 2 | 1 | 2 | 1 | 24 |
| 149 | _SET_NPC_FOOT_SOUND | 2 |  |  | 4 |  |  | 2 | 8 |
| 150 | _NPC_STOP | 18 | 12 | 4 | 13 | 2 | 6 | 8 | 63 |
| 152 | _SET_NPC_FLOOR_ID | 3 |  |  | 6 |  |  | 2 | 11 |
| 153 | _NPC_POS_INIT |  | 3 |  |  |  | 1 |  | 4 |
| 160 | _NPC_CLOTH_STEP | 1 | 2 |  |  | 4 |  |  | 7 |
| 192 | _MES_MAKE | 15 | 6 | 4 | 12 | 4 | 6 | 3 | 50 |
| 193 | _MES_CLOSE | 8 | 7 | 5 | 6 | 5 | 4 | 6 | 41 |
| 194 | _MES_NEXTPAGE | 3 | 3 | 4 | 3 | 3 | 3 | 3 | 22 |
| 195 | _SET_MES_AUTOSET | 15 | 6 | 4 | 12 | 4 | 6 | 3 | 50 |
| 196 | _SET_MES_SHIPPO | 38 | 26 | 20 | 23 | 20 | 23 | 12 | 162 |
| 197 | _SET_MES_POS | 19 | 8 | 8 | 12 | 4 | 8 | 4 | 63 |
| 198 | _SET_MES_DRAWSPEED | 4 | 4 | 5 | 4 | 4 | 4 | 4 | 29 |
| 199 | _SET_MES_CURSOR | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 21 |
| 200 | _CHECK_MES_COMPLETE | 3 | 3 | 4 | 3 | 3 | 3 | 3 | 22 |
| 201 | _CHECK_MES_WAIT | 3 | 3 | 4 | 3 | 3 | 3 | 3 | 22 |
| 202 | _CHECK_MES | 3 | 3 | 4 | 3 | 3 | 3 | 3 | 22 |
| 203 | _SET_MES_OKURI | 6 | 2 |  | 4 | 2 |  | 2 | 16 |
| 204 | _SET_MES_FUKIDASHI | 6 | 5 | 3 |  |  |  |  | 14 |
| 206 | _ITEM_GET_MES | 3 | 2 | 2 | 2 | 2 | 2 | 3 | 16 |
| 207 | _INIT_SYS_MES | 6 | 3 | 3 | 3 | 3 | 3 | 4 | 25 |
| 208 | _SKILL_GET_MES | 1 |  |  |  |  |  |  | 1 |
| 258 | _GET_NOW_TIME |  | 1 |  |  |  |  |  | 1 |
| 266 | _SGET_DAY |  | 1 |  |  |  |  |  | 1 |
| 323 | _OBJ_DRAW | 11 | 9 | 10 | 7 | 13 | 7 | 7 | 64 |
| 324 | _SET_OBJ_POS | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 326 | _SET_OBJ_ROT | 1 | 1 | 1 | 1 | 2 | 1 | 1 | 8 |
| 328 | _SET_OBJ_SCALE | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 14 |
| 330 | _HOBJ_CHARA | 12 | 5 | 8 | 2 | 16 | 2 | 2 | 47 |
| 332 | _HOBJ_BT_HOBJ | 4 |  |  |  |  |  |  | 4 |
| 333 | _SYNC_OBJ_OBJ | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 336 | _HOBJ_ITEM | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 416 | _SET_CAMERA | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 14 |
| 417 | _SET_FOLLOW_CAMERA |  |  | 1 | 2 |  |  | 1 | 4 |
| 421 | _CAMERA_STEP | 2 |  |  |  |  |  |  | 2 |
| 422 | _SET_CAMERA_POS | 118 | 36 | 18 | 31 | 43 | 25 | 19 | 290 |
| 423 | _GET_CAMERA_POS | 2 | 2 | 2 | 2 | 3 | 2 | 2 | 15 |
| 424 | _SET_CAMERA_REF | 117 | 37 | 19 | 31 | 39 | 24 | 19 | 286 |
| 425 | _GET_CAMERA_REF | 2 | 2 | 2 | 2 | 5 | 2 | 2 | 17 |
| 426 | _SET_CAMERA_SPEED | 40 | 21 | 9 | 12 | 15 | 15 | 14 | 126 |
| 427 | _SET_PROJECTION | 15 | 2 | 3 | 6 | 1 | 6 | 3 | 36 |
| 428 | _ITEM_GET_CAMERA | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 8 |
| 429 | _SET_CAMERA_ANGLE |  | 1 |  |  |  |  |  | 1 |
| 430 | _GET_CAMERA_ANGLE |  | 1 |  |  |  |  |  | 1 |
| 431 | _SET_CAMERA_ROTATE | 1 | 1 |  |  | 2 |  |  | 4 |
| 432 | _GET_CAMERA_ROTATE | 1 |  |  |  | 1 |  |  | 2 |
| 433 | _RESET_CAMERA | 5 | 1 |  | 1 | 1 | 1 | 3 | 12 |
| 434 | _SYNC_CAMERA_REF_CHARA | 2 |  | 1 | 3 | 5 | 2 | 1 | 14 |
| 435 | _RELEASE_CAMERA_REF_CHARA | 2 |  |  |  | 3 |  |  | 5 |
| 436 | _RESET_CAMERA_ANGLE | 11 | 6 | 6 | 7 | 5 | 4 | 3 | 42 |
| 437 | _SYNC_CAMERA_REF_OBJ | 2 | 1 |  |  |  |  |  | 3 |
| 438 | _RELEASE_CAMERA_REF_OBJ | 1 | 1 |  |  |  |  |  | 2 |
| 439 | _SYNC_CAMERA_POS_OBJ | 1 |  |  |  |  |  |  | 1 |
| 440 | _RELEASE_CAMERA_POS_OBJ | 1 |  |  |  |  |  |  | 1 |
| 500 | _FADE_IN | 38 | 27 | 17 | 18 | 17 | 17 | 11 | 145 |
| 501 | _FADE_OUT | 34 | 26 | 15 | 18 | 17 | 16 | 9 | 135 |
| 502 | _CHECK_FADE | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 503 | _INIT_SPRITE | 64 | 2 |  | 4 | 2 |  | 1 | 73 |
| 504 | _DRAW_SPRITE | 41 | 1 |  | 2 | 1 |  | 1 | 46 |
| 505 | _DRAW_BACK |  |  | 1 |  |  |  |  | 1 |
| 510 | _SCREEN_FILTER | 2 | 2 | 2 |  |  |  |  | 6 |
| 518 | _SP_INIT | 2 |  |  |  |  | 1 |  | 3 |
| 519 | _SP_SET_TEX | 3 |  |  |  |  | 1 |  | 4 |
| 520 | _SP_SET_POS | 2 |  |  |  |  | 1 |  | 3 |
| 521 | _SP_SET_RGBA |  |  |  |  |  | 2 |  | 2 |
| 523 | _SP_DRAW | 5 |  |  |  |  | 1 |  | 6 |
| 524 | _INIT_DRAW_DAY |  | 2 |  |  |  |  |  | 2 |
| 525 | _DRAW_DAY |  | 1 |  |  |  |  |  | 1 |
| 527 | _MAP_TITLE_OFF |  |  |  |  |  | 2 |  | 2 |
| 600 | _ASQ_INIT | 133 | 42 | 17 | 43 | 29 | 30 | 19 | 313 |
| 601 | _ASQ_SYNC_CHARA | 121 | 37 | 14 | 41 | 26 | 27 | 16 | 282 |
| 602 | _ASQ_SET_POS | 24 | 51 | 5 | 31 | 35 | 10 | 10 | 166 |
| 603 | _ASQ_MOVE | 20 | 12 | 5 | 9 | 16 | 2 | 5 | 69 |
| 604 | _ASQ_MOVE_STEP | 8 |  | 1 | 5 | 2 | 2 | 1 | 19 |
| 605 | _ASQ_ROT_REF | 2 | 6 |  | 1 | 7 | 1 | 1 | 18 |
| 607 | _ASQ_WAIT_ROT | 4 | 34 | 3 | 1 | 9 | 2 | 1 | 54 |
| 608 | _ASQ_ROT_MOVE | 7 |  | 1 | 7 | 1 | 1 | 1 | 18 |
| 609 | _ASQ_SET_ROT | 19 | 49 | 4 | 20 | 33 | 9 | 7 | 141 |
| 611 | _ASQ_CHECK | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 14 |
| 612 | _ASQ_MOTION_TRG | 114 | 180 | 25 | 51 | 114 | 27 | 20 | 531 |
| 613 | _ASQ_MOTION_PLAY | 96 | 141 | 21 | 41 | 108 | 21 | 13 | 441 |
| 614 | _ASQ_MOTION_STOP | 18 | 39 | 4 | 6 | 6 | 6 | 7 | 86 |
| 615 | _ASQ_MOTION_NEXT | 70 | 91 | 15 | 24 | 44 | 11 | 15 | 270 |
| 616 | _ASQ_ANIME_TRG | 40 | 22 | 9 | 10 | 21 | 12 | 8 | 122 |
| 617 | _ASQ_ANIME | 65 | 36 | 10 | 12 | 30 | 18 | 13 | 184 |
| 618 | _ASQ_ROT_ANGLE | 4 | 28 | 3 |  | 2 | 1 |  | 38 |
| 650 | _OBJ_ANIME_INIT | 4 |  |  |  | 1 |  |  | 5 |
| 651 | _OBJ_ANIME | 8 |  |  |  | 2 |  |  | 10 |
| 700 | _SSET_GAME_FLAG | 11 | 6 | 6 | 7 | 4 | 4 |  | 38 |
| 701 | _SGET_GAME_FLAG | 12 | 8 | 6 | 14 | 8 | 1 |  | 49 |
| 702 | _SITEM_GET | 5 | 5 | 2 | 2 | 2 | 2 | 3 | 21 |
| 705 | _SET_WORK_FLAG |  |  |  | 2 |  |  |  | 2 |
| 706 | _GET_WORK_FLAG |  |  |  | 1 |  |  |  | 1 |
| 707 | _SITEM_CHECK | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 711 | _SSET_GAME_INT_FLAG | 1 |  | 1 | 1 | 1 | 5 | 1 | 10 |
| 712 | _SGET_GAME_INT_FLAG | 1 |  | 3 | 1 |  | 2 | 1 | 8 |
| 716 | _SGET_EQUIP_WEAPON |  | 1 |  |  |  |  |  | 1 |
| 718 | _SITEM_LOST | 1 | 2 | 1 | 1 | 1 | 1 | 1 | 8 |
| 720 | _SATRA_CHIP_GET |  |  |  |  | 1 |  |  | 1 |
| 800 | _LOAD_SND_SYNC | 3 | 2 | 2 | 1 | 1 | 1 | 1 | 11 |
| 801 | _LOAD_BGM | 16 | 8 | 1 | 2 | 2 | 2 | 2 | 33 |
| 802 | _PLAY_BGM | 17 | 8 | 1 | 2 | 2 | 2 | 2 | 34 |
| 803 | _STOP_BGM | 18 | 4 | 1 | 3 | 3 | 4 |  | 33 |
| 806 | _GET_BGM_NO | 1 | 3 |  |  |  |  |  | 4 |
| 807 | _PLAY_SE | 84 | 62 | 13 | 26 | 22 | 28 | 16 | 251 |
| 808 | _STOP_SE | 17 | 2 | 2 | 3 | 4 | 3 |  | 31 |
| 810 | _LOAD_SPECIAL_SE | 6 | 4 | 6 | 2 | 5 | 4 | 3 | 30 |
| 811 | _PLAY_SPECIAL_SE | 8 | 4 | 7 | 3 | 5 | 4 | 3 | 34 |
| 812 | _STOP_SPECIAL_SE | 3 |  |  | 2 |  |  |  | 5 |
| 815 | _DELETE_BGM | 1 | 1 |  |  |  |  |  | 2 |
| 816 | _LOAD_SOUND_SET | 7 | 6 | 1 | 3 | 1 | 4 | 3 | 25 |
| 817 | _BGM_FADE_IN | 1 | 1 | 1 | 1 | 1 |  |  | 5 |
| 818 | _BGM_FADE_OUT | 2 |  | 1 | 2 |  | 1 | 2 | 8 |
| 823 | _SET_SPECIAL_SE_VOL | 3 |  |  | 2 |  |  |  | 5 |
| 903 | _GET_APAD | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 950 | _EB_INIT | 2 | 2 |  |  |  |  |  | 4 |
| 951 | _EB_SET_KEY | 9 | 14 |  |  |  |  |  | 23 |
| 952 | _EB_LOOP | 2 | 2 |  |  |  |  |  | 4 |
| 953 | _EB_SET_MOTION | 2 | 2 |  |  |  |  |  | 4 |
| 955 | _EB_INTRO_START | 3 | 2 |  |  |  |  |  | 5 |
| 956 | _EB_INTRO_LOOP | 3 | 1 |  |  |  |  |  | 4 |
| 957 | _EB_INTRO_END | 2 | 2 |  |  |  |  |  | 4 |
| 1010 | _GET_FLOOR_LEVEL | 3 | 2 | 2 | 3 | 3 | 4 | 3 | 20 |
| 1011 | _SET_FLOOR_LEVEL | 2 | 2 | 3 | 3 | 2 | 5 | 2 | 19 |
| 1012 | _GET_OLD_FLOOR_LEVEL | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1020 | _GET_ACTION_MODE | 5 | 6 | 5 | 6 | 5 | 4 | 4 | 35 |
| 1021 | _ITEM_USE_WINDOW | 4 | 5 | 4 | 5 | 4 | 3 | 3 | 28 |
| 1022 | _OPEN_ENTRANCE_WINDOW | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1023 | _CHECK_EVENT_FLG | 1 | 1 | 1 | 1 | 1 | 2 | 2 | 9 |
| 1024 | _SET_EVENT_FLG | 1 | 1 | 1 | 1 | 1 | 2 | 2 | 9 |
| 1030 | _GET_OBJHDL | 59 | 75 | 79 | 122 | 105 | 84 | 21 | 545 |
| 1031 | _SET_OBJHDL_POS | 41 | 21 | 36 | 91 | 6 | 30 | 10 | 235 |
| 1032 | _SET_OBJHDL_ROT | 4 |  | 4 |  | 70 | 4 | 6 | 88 |
| 1033 | _SET_OBJHDL_DRAW_FLAG | 15 | 54 | 45 | 59 | 36 | 51 | 11 | 271 |
| 1034 | _GET_OBJHDL_POS | 9 | 4 | 4 | 5 | 5 | 6 | 2 | 35 |
| 1035 | _GET_OBJHDL_ROT | 9 | 4 | 4 | 5 | 5 | 6 | 2 | 35 |
| 1036 | _SET_URA_DUNGEON | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1040 | _GET_EVENT_POS | 2 | 2 | 1 | 2 | 2 | 1 | 1 | 11 |
| 1041 | _GET_EVENT_ROT | 1 | 1 |  | 1 | 1 |  |  | 4 |
| 1050 | _GO_DUNGEON | 3 | 2 | 5 | 4 | 2 | 4 | 2 | 22 |
| 1051 | _SET_DUNGEON_MAP | 5 | 4 | 5 | 4 | 4 | 6 | 7 | 35 |
| 1052 | _LOAD_DUNGEON_MAP2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1053 | _LOAD_MONSTOR | 3 | 2 | 2 | 2 | 2 | 2 | 2 | 15 |
| 1054 | _SET_RANDOM_MAP | 5 | 4 | 4 | 4 | 4 | 4 | 3 | 28 |
| 1055 | _SET_EVENT_SW | 16 | 18 | 12 | 24 | 28 | 10 | 2 | 110 |
| 1056 | _SET_MONSTOR_ID | 5 | 1 | 15 | 8 | 4 | 17 | 2 | 52 |
| 1057 | _CHK_ATRA_HAVE | 1 |  |  |  |  |  |  | 1 |
| 1058 | _SET_ATRA | 2 |  |  |  |  |  |  | 2 |
| 1059 | _SET_IBOX | 4 |  | 1 |  |  |  |  | 5 |
| 1060 | _GET_NOW_USER_ID | 7 | 9 | 7 | 10 | 9 | 9 | 1 | 52 |
| 1061 | _RUN_SCRIPT_NO | 2 | 1 | 3 | 2 | 2 | 6 | 2 | 18 |
| 1062 | _GET_NPC_OBJHDL |  | 2 | 6 | 2 | 3 | 3 |  | 16 |
| 1063 | _SET_MOTION_OBJHDL |  | 7 | 12 | 1 | 1 | 5 |  | 26 |
| 1064 | _GET_GATEKEY_NO |  | 1 |  |  |  |  |  | 1 |
| 1065 | _USER_WEAPON_DRAW |  |  | 6 |  | 2 |  | 1 | 9 |
| 1066 | _CLEAN_MONSTOR_SCRIPT_NO | 1 |  |  |  |  |  |  | 1 |
| 1067 | _SET_MAIN_CHR2 | 1 | 1 | 1 | 2 | 1 | 1 | 1 | 8 |
| 1068 | _SET_LIMMIT_ZONE | 2 | 2 | 2 | 2 | 2 | 5 |  | 15 |
| 1070 | _OPEN_ESCAPE_WINDOW | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1072 | _ALL_DRAW_FLAG | 4 | 4 | 4 | 6 | 8 | 4 | 4 | 34 |
| 1073 | _SET_FLOOR_TITLE | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1074 | _GET_RUBY_ELEMENT |  |  | 4 | 4 | 4 | 4 |  | 16 |
| 1076 | _SET_FLOOR_TITLE_OFF |  |  | 1 |  |  | 2 |  | 3 |
| 1077 | _INIT_BEE |  |  |  | 2 |  |  |  | 2 |
| 1078 | _END_BEE |  |  |  | 2 |  |  |  | 2 |
| 1079 | _EASTKING_COMPLETE |  |  |  |  |  | 1 | 1 | 2 |
| 1080 | _SET_RES_LIMMIT_ZONE | 1 | 1 | 1 | 1 | 1 | 1 |  | 6 |
| 1081 | _GET_ITEM_TRAPID | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1082 | _RESET_ITEM_TRAP | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 21 |
| 1083 | _BOM_SET | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1084 | _SET_STATUS_ERR | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 14 |
| 1086 | _CHECK_MARDAN | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1088 | _SET_RUBY_ELEMENT |  |  | 1 |  |  |  |  | 1 |
| 1089 | _CHECK_CHR_HELP |  | 2 | 4 | 5 | 5 | 5 |  | 21 |
| 1090 | _HOLD_ITEM_EVENT | 1 | 1 | 1 |  | 3 | 1 | 1 | 8 |
| 1092 | _NO_STATUS_RECOVER |  |  |  | 2 |  |  |  | 2 |
| 1094 | _SET_NPC_ON_OFF |  | 2 |  |  |  |  |  | 2 |
| 1095 | _GET_MAP_CODE |  |  |  | 1 | 1 | 1 |  | 3 |
| 1096 | _SET_ACTIVE_ITEM_ICON |  |  | 1 | 1 |  | 2 |  | 4 |
| 1097 | _GET_ITEM_UNIT_NO | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1098 | _SET_IBOX_ANGLE | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 14 |
| 1099 | _SET_IBOX_FINISH | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |
| 1100 | _GET_PIERO_ITEM | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 7 |

235 distinct ext functions; per dungeon: d01 197, d02 180, d03 164, d04 173, d05 171, d06 169, d07 155
