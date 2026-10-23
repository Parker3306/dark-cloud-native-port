# Back floors ("ura" / "under" dungeons): how the original does them and how to port them

Scope: the back floor ("ura", JP 裏; the code also says "Under") that every random floor of the seven dungeons owns: how the player gets in and out, what the engine swaps, how the back floor is generated, what is different inside, what is saved, and what has to change in `native/`.

Tags: **READ** = read from `work/decomp/all_dun.c` (Ghidra decompile of SCUS_971.11 + DUN.BIN) or from disassembly (`work/disasm.ps1`), **VERIFIED-DATA** = checked against the ELF tables / disc files / the script VM, **INFERRED** = deduced, **OPEN** = not resolved. Nothing here was compared with a running game (no PCSX2 session was used). Companion notes: `notes/dungeon_events.md` (script VM, request dispatcher 1.4, marker cfg 1.5), `notes/native-plan.md` (floor generator).
Scratch tools used (all read-only): `tools/dc/stbtext.py`, `tools/dc/dungeon_events.py state d0N 200`, `work/disasm.ps1 <start> <end> <out> [SCUS_dun DisasmForce.java]`, an ELF table reader on `extracted/SCUS_971.11`.

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 0. The model in ten lines

1. Every random floor (map kind 1) is built **twice** by `BtCleatRandomMap` (0x01DB8790, called by `_SET_RANDOM_MAP` 0x001BC580): first the normal floor into `MainDungeonMap` (0x01DC4BE0), then a copy of it is turned into the back floor in `UraDungeonMap` (0x01DD56F0) with its own `UraEventMan` (0x01DF5EB0). Story rooms and boss rooms (kind 0 / 2, `BtCleatFreeMap`) have no back floor.
2. The back floor is NOT a separate floor sequence: same `selectMapNo`, same floor number (`UserStatus+2` never changes), same `.cfg` part list and `.mpd`, same `rooms = 6`. It is a second random layout of that floor with a different monster row, different chest lists, no stairs, no Atla, no divider walls/doors, a blue "URA_*" lighting set and a "BACK" banner.
3. The only way in or out is the marker with event id 200 (and 201 in d04/d05) on part 40 (0x28, the "ura entrance/exit", cfg comment 裏マップ出入口). Both floors contain that part (`setUnderDungeonStart` runs for both). The dungeon script (`dun/script/d0N/event.stb`, function 200/201) checks a per-floor event flag, asks a Yes/No question and plays a scene whose middle is `_SET_URA_DUNGEON()`.
4. `_SET_URA_DUNGEON` only posts request 2 (`DAT_01d567d8 = 2`); the dispatcher turns it into `gameTask = 0xa0`, whose body toggles the global `BtUraDongeon` (0x002A34B4), swaps `NowDngMap` / `NowEventMan` / `RandomItem`, reloads the monsters and recomputes the visible cells.
5. There is ONE monster unit (`MainMonstorUnit`, `NowMonstorUnit` is only ever assigned to it). Entering the back floor wipes the main floor's monsters and loads the back row (8 monsters); leaving wipes the back monsters and re-arranges 15 fresh main monsters.
6. Back-floor monsters: row `BtUraEnemyLayoutList[map]` + floor*0x70, 8 instead of 15, permanently "powered up" (`+0x1e3e0` kept at 180: double damage dealt, half damage taken), weapon exp x2, no gate key.
7. Back-floor chests: 4..7 small chests from `ItemPutListPtr[map + 7]` (only items: attach spheres, gems, special items; weapons only in d06/d07), no map/crystal, no Atla statues, no link-door chest.
8. Nothing is saved: no UserStatus / SaveData byte says "in back dungeon" or records back-floor progress. The flag is the runtime global `BtUraDongeon`; the unlock state is a per-floor event flag cleared at every new floor.
9. There is no goal on the back floor (no exit stairs, no gate, no boss). Return is the same marker. Escape powder / the escape window / death were not traced for the back floor (nothing in the code looks at `BtUraDongeon` there, INFERRED: same behaviour as on the main floor).
10. The port's generator already has the back-floor layout (`build_random_map(..., fresh = false)`), the back chest list (`build_event_data(..., main_floor = false)`), the back layout rows (`tables.layout[1]`), `CfgEnv back` and `MonsterWorld::double_weapon_exp` (= unit `+0x44`); none of them is wired up yet.

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 1. Entering and leaving

### 1.1 Engine side: how a marker raises event 200 / 201 (READ)
No special case for 200/201 exists in the engine; they go through the generic marker path of `MoveChara` (0x01DB08A0, gameTask 0), see `dungeon_events.md` 1.3. For this feature the relevant facts:
```
if (DAT_01dc44e4 != 0 && DAT_01dc4518 == 0) {            // player free (not attacking / frozen)
    slot = CDungeonEventMan::SearchDataSlotPos2(NowEventMan, &pos)   // first of 0x60 slots with switch on, |dy| < 40, dist <= radius + 10
    ...  DAT_01d5676c = def->event_id (+0x20) ; DAT_01d56774 = def->ext (+0x38)
    if (def->auto (+0x24))                      -> fade out, gameTask 500 (mode 0)
    else if (slot->item_use (+0x30) && Down(pad, 0x80))  -> gameTask 400, DAT_01d56764 = 2   // item-use button
    else if (Down(pad, PadInput_OK))                      -> gameTask 400, DAT_01d56764 = 1
}
```
So for the 200/201 markers (`PT_MARKER "ura_2",30,200,ITEM_USE=ON,AUTO=OFF,SWITCH=ON,EXT=OFF`): standing in the area does nothing; OK = mode 1, the item-use button (0x80, `kPadUse` in the port) = mode 2 and only while the slot's item-use flag is on. `NowEventMan` is the active floor's manager, so after the swap the markers of the other floor are the live ones.
`_HOLD_ITEM_EVENT(id)` (0x001BD490) calls `SearchItemEventHold` (0x001CCA60) on BOTH `DngEventMan` and `UraEventMan`: it clears slot `+0x30` (item-use) of every slot whose definition id (`def+0x20`) equals the id. After the unlock the marker still answers OK but no longer the item button.
`_SET_EVENT_SW(id, on)` (`SearchDataSwitch`, 0x001CC990) acts on `NowEventMan` only, which is why the d04/d05 scripts repeat it after the swap.

### 1.2 Cfg markers (VERIFIED-DATA, all 21 `d0?main_*.cfg`)
Block index 40 of every main cfg (the 41st `DEF_PATS`, comment `//no40`, `裏マップ出入口` = "back map entrance/exit"; blocks 41..43 are the same room turned, `no44` = `裏マップ通路` d01o15_0, the filler the generator puts in column 19) holds:
```
d01..d03, d06, d07 : PT_MARKER "ura_2",30.0 (d02: 10.0),200,ON,OFF,ON,OFF
d04                : "ura_2" 30.0, 200, ON,OFF,ON,OFF    and   "hitsugi" 40.0, 201, OFF,OFF,OFF,OFF   (coffin)
d05                : "ita"   30.0, 200, ON,OFF,ON,OFF    and   "ura_2"  10.0, 201, OFF,OFF,OFF,OFF   (plank / ledge)
```
(Counting `DEF_PATS` blocks with comments stripped; the port's parser already numbers them this way.) Part 40 = grid part `0x28` = the cell `setUnderDungeonStart` marks (flags 0x201, part 0x24 then 0x28 in `mapPartsFilter`); the port's `parts_filter` does this already.

### 1.3 Script side: function 200 (and 201), all seven dungeons (VERIFIED-DATA, `dungeon_events.py state`, `stbtext.py`)
Common structure (flag index k = 1 in d01, 0 elsewhere; `fn` names are record offsets):
```
mode = _GET_ACTION_MODE();  f = _CHECK_EVENT_FLG(k);
if (f && mode == 1) { r = YesNo(window); if (r == 1) return;   fn_scene(); return; }   // choice 0 = take it, 1 = quit
if (mode == 1)      { message "locked / needs item"; return; }
if (mode == 2)      { _ITEM_USE_WINDOW(&v, KEY...);
                      if (v == KEY) { message "unlocked"; [small scene]; _SET_EVENT_FLG(k, 1); _HOLD_ITEM_EVENT(200) [d04/d05: _SET_EVENT_SW(200,0); _SET_EVENT_SW(201,1)] }
                      else if (v >= 0) fn_1920(31)  /* refusal reaction (hero turns to the camera) */ }
```
| dungeon | key item (id, name) | texts (event bank) | scene function |
|---|---|---|---|
| d01 | 224 Tram Oil | 300 "There is a tram with rusty wheels.\n(Use item with <btn>)", 301 "Tram wheels are fully oiled.\nTram can be used now!", 302 " Take the tram\nQuit." | fn_4cb4c (cart `d01mob` rides away, swap, cart rides in) |
| d02 | 225 Sun Dew | ids 300..303, **not in the d02 bank** (OPEN) | fn_2d5c0 (`d02mob` cart) |
| d03 | 226 Flapping Fish (unlocks), 227 Rotten Fish (only plays a gag scene) | ids 20..22, **not in the d03 bank** (OPEN) | fn_20ac8 (`_USER_WEAPON_DRAW(0/1)`) |
| d04 | 228 Secret Path Key | ids 20..22, **not in the d04 bank** (OPEN) | fn_3239c (coffin lid `udoor`/`omote`/`gyaku`, `d04mob`); used on 200, then 200 off / 201 on |
| d05 | 229 Bravery Launch | 20 "There is a sign.\n\"Test your courage!\"\n(Use item with <btn>)", 21 "Set the Bravery Launch\nat the cliff.", 22 " Test your courage\n Whimp out" | fn_2d7d4 (fn 201); `fumikiri` object shown on unlock; 200 off / 201 on |
| d06 | 230 Flapping Duster | 20 "A big painting on the wall.\nThere is a carpet in front.\n(Use item with <btn>)", 21 "The carpet is floating!", 22 " Take the carpet\n Do not take it" | fn_27168 (`o11_0` carpet) |
| d07 | 231 Crystal Eyeball | 20 "There is a sculpture with\nan eye missing.\n(Use item with <btn>)", 21 "You put the Crystal Eye\nin the sculpture.", 22 " Touch the sculpture.\n Quit." | fn_151dc (`mov` / `eye`) |
(Item names from `work/tables/items.csv`; `<btn>` is glyph `{fd09}`. d02's script uses 302 as a notice and 303 as the Yes/No question. The three missing banks are not in the shipped `.stb` message tables nor in `dun/message/*`, `meswin/system*.mes`; it was not resolved where the real game gets them, or whether it shows an empty window.)
Not consumed: there is no `_SITEM_LOST` in any 200/201 closure and `BtMiniItemSelect_Loop` (0x001D3440) only writes the chosen id through `DAT_01d567c0`, so the key stays in the bag. The unlock flag is `UserStatus+0x8aa4+k` (`_SET_EVENT_FLG`, 50 bytes) and is zeroed by `ClearEventFlag` (0x001BDB20) at the end of `BtCleatRandomMap` / `BtCleatFreeMap`: **the unlock lasts for the current floor only; the key item is needed again on every floor.**
Where the key items come from (VERIFIED-DATA, ELF `ItemPutListPtr` 0x002763D0): they are plain entries of the normal chest lists (d01 Tram Oil 3x in the 0x100/0xff rows and 1x in the floor-10 row, d02 Sun Dew 6x/4x, d03 Flapping Fish 2x, d04 Secret Path Key 3x, d05 Bravery Launch 3x, d06 Flapping Duster 3x, d07 Crystal Eyeball 5x/4x), and appear in the Piero lists (`PieroItemListPtr`, B list: Tram Oil, Sun Dew ...).

d01 scene `fn_4cb4c` in order (READ of the disassembly): fade out; load `ura.cfg` / `d01mob.chr` bits; the cart object `d01mob` (handle 2) is moved along z by `_SET_OBJHDL_POS(2, 0, 0, x)` with a speed ramp (hero follows, camera tracks) until x <= -100, `_FADE_OUT(60)` starts after 100 frames; `_SET_URA_DUNGEON()`; `yield` (the engine swaps floors in this frame); then the hero/cart are placed again: `_GET_OBJHDL_POS(2)`/`_GET_OBJHDL_ROT(2)` -> `_SET_WORLD_COORD(pos, rot)`, the cart rides in from -70 to 0 (z of its frame) with a braking ramp, fade out, `_SET_OBJHDL_ROT` door frames, **`_SET_NPC_POS(-1, -20, 0, 0)`** (hero 20 units beside the cart, in the cart frame), `_SET_NPC_ROT(-1, 0, 3.14/2, 0)`, `_RESET_CAMERA`, `_FADE_IN(30)`, **`_SET_FLOOR_TITLE()`**, return.
After the swap the objects `_GET_OBJHDL_POS` reads are the SHARED part frames (the copy `UraDungeonMap = MainDungeonMap` is shallow), whose world position the next `DrawMap` of the now-active map has set to the back floor's own entrance cell. Final hero placement after the swap, by dungeon (relative to the entrance part's frame / the world coordinate the scene sets, READ from the scripts): d01 (-20, 0, 0) yaw 90 deg; d02 (-20, 2, 0) yaw 90; d03 (0, 0, 50) yaw 0; d04 (-20, 0, 0) yaw 180; d05 (0, 0, 40) in the frame `ura_2` (an earlier step uses (0, 0, 20)), yaw 0; d06 (-22.32, 57.1, -40.65) in the carpet frame `o11_0` (the carpet first slides in, hero at (0, v, z) while it moves), yaw 180; d07 (0, 0, 30) yaw 0 (first 180 while the sculpture turns). The same scene runs when going back (the script cannot tell which side it is on; the request just toggles).

### 1.4 The request and gameTask 0xa0 (READ)
```
_SET_URA_DUNGEON (0x001BC300):   DAT_01d567d8 = 2; return 1;
request dispatcher, handler for 2 (0x01DB4C6C, jump table 0x01DC3010, run from gameTask 0x191 after EdEventMode):
        DAT_01d567d8 = 0;  gameTask = 0xa0;
gameTask 0xa0 (0x01DB4EC0 .. 0x01DB50F8; also a textual copy inside MoveChara):
    CleanViewMonstor(NowMonstorUnit, BtUraDongeon); BtSetEventExtendTable();
    if (BtUraDongeon == 0) {                         // main -> back
        BtUraDongeon = 1;  NowDngMap = &UraDungeonMap;  NowEventMan = &UraEventMan;
        RsetMimicEvent(&UraDungeonMap);              // 0x001C8E50: reset the map's mimic-chest events (type 8)
        RandomItem = &SubRandomItem;                 // items lying on the floor are per floor
        CleanViewMonstor(NowMonstorUnit, 1);         // stores the flag in unit+0x44
        BtLoadMonstor(1);                            // BtUraEnemyLayoutList[map] + UserStatus[2]*0x70
        ArrangementPos(NowMonstorUnit, UraDungeonMap, 8, -1, 0);        // 8 monsters, random model
        DrawMapCalc(NowDngMap, NowDngMap[0x2f7b]);   lightingMode = 1;
    } else {                                         // back -> main
        BtUraDongeon = 0;  NowDngMap = &MainDungeonMap;  NowEventMan = &DngEventMan;  NowMonstorUnit = &MainMonstorUnit;
        RsetMimicEvent(&MainDungeonMap);  RandomItem = &MainRandomItem;
        CleanViewMonstor(MainMonstorUnit, 0);  BtLoadMonstor(0);
        ArrangementPos(MainMonstorUnit, MainDungeonMap, 15, -1, 0);     // fresh 15 monsters
        DrawMapCalc(...);   lightingMode = 0;
    }
    gameTask = 0x191;                                // the event script resumes next frame (after its `yield`)
```
`NowMonstorUnit` is assigned only in `GameInit` and in the "back -> main" branch (always `&MainMonstorUnit`); the symbol table has no second unit (`MainMonstorUnit` is 395,088 bytes and nothing named Ura/Sub exists), so the back monsters live in the same unit. **Consequences (READ):** entering wipes the main floor's monsters; returning rebuilds them from scratch (killed monsters come back, 15 new positions); the same for the back floor on every re-entry. Chests, doors and the explored mask live in the per-floor map object and persist. `BtLoadMonstor` chooses the table by its argument (0 main / 1 back), not by `BtUraDongeon`.
All 45 references to `BtUraDongeon` (VERIFIED by grep of `all_dun.c`): this swap, the resets to 0 in `BtCleatRandomMap`/`BtCleatFreeMap`, the `CleanViewMonstor` second arguments, `_GET_PIERO_ITEM`, the floor banner in `MainDraw` and its copies, and `topStatusInfo`. No ext function returns it, so scripts never read it.

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 2. How the back floor is built

### 2.1 `BtCleatRandomMap` (0x01DB8790), READ + disassembly check (`jal 0x1cb670` at 0x01DB89E4 and 0x01DB9100)
```
NowDngMap = Main; NowEventMan = DngEventMan;  LostGateKey(UserStatus);  clear event slots, boxes, atra, traps, drops ...
buildRandomMap(Main, 6, 1);                     // a2 = 1: new seed = (int)(rand() / 100000.0f), srand(seed)
buildEventData(Main, UserStatus[2], Main.type, 1);  FlushCheckMask;  DrawMapCalc;  SetupEvent(DngEventMan, Main, type);
NowDngMap = Ura; NowEventMan = UraEventMan;
UraDungeonMap = MainDungeonMap;                 // struct copy (68,368 bytes: seed, parts, water, NPC tables ...), then DngEventMan -> UraEventMan (0x2800 bytes)
clear the copied event slots / boxes / atra / traps
buildRandomMap(Ura, 6, 0);                      // a2 = 0 (`clear a2` at 0x01DB90FC): NO srand, the rand() stream just continues
buildEventData(Ura, UserStatus[2], Ura.type, 0);// a3 = 0: "back" flavour
FlushCheckMask(Ura); DrawMapCalc; SetupEvent(UraEventMan, Ura, type);
NowDngMap = Main; NowEventMan = DngEventMan; ClearEventFlag(UserStatus); BtUraDongeon = 0; BtEquipMap = 0; BtEquipMasuisyou = 0;
```
Rules: same dungeon, same floor number, same cfg and parts, **6 rooms** (`roomStackCnt <= 5`), **seed = whatever the global rand() is** when the main floor is finished (so the back layout is a different, but reproducible, function of the floor seed: it needs the exact rand() state after `buildEventData(Main)`), part grid exactly like the main floor except what `param_3 == 0` skips:

| `buildRandomMap` step | main (`a2 = 1`) | back (`a2 = 0`) |
|---|---|---|
| `srand(new seed)` | yes | no |
| `setRoomObject` (object / decoration cells) | yes | yes |
| `setRoomDivid` (divider walls + doors) | yes | **no** |
| `setUnderDungeonStart` (entrance cell 0x201 -> part 0x28) | yes | yes |
| `setStair` (in-stairs 0x80 -> part 0x1e, out-stairs 0x100 -> 0x1f) | yes | **no** |
| `mapPartsFilter`, `memcpy` of the grid, `buildDummyModel` (selectMapNo 4 = d05) | yes | yes |
| `GetRoomLinkInfo` roll (the `rand()` is drawn in both, the call needs a2 != 0) and `BuildCharaSpecialParts` (character rooms 350..375) | yes | **no** |
| `selectMapNo == 1`: the tint pick `rand() * 3` | yes | yes |
| `SetUnderLoad` (column 19 = part 0x2c) | yes | yes |
So the back floor has no stairs (no `in_2` / `out_2` frames, no event 160, no gate 161), no dividers, no locked doors (250/251), no link-door chest, no character rooms.
`buildEventData(map, floor, type, flag)` (0x001C8540, only for `type == 1`): chest count `rand()*4/2^31 + 4` (4..7) in both; `PresetSmallItemNo_Get(map, floor, table = (flag == 0), ...)` selects `ItemPutListPtr[map + 7*table]`; only when `flag != 0`: the first two small chests are forced to 0xe9 (map) and 0xea (magic crystal) and `BtAtraFloorCyoice` places the floor's Atla statues. Trap circles (21 % each, up to 3) in both. `BtCleatFreeMap` (story/boss rooms) calls `buildEventData(Main, ..., 1)` only.
**Effect on the main floor's monsters (READ):** `_LOAD_MONSTOR` (= `BtLoadMonstor(0)` + `BtArrengeMonstor`, 15 monsters) runs AFTER `_SET_RANDOM_MAP`, i.e. after the back floor consumed its share of rand(). The port currently arranges the main monsters from the state right after the main events; a faithful port must build the back layout and its events first.

### 2.2 Environment (READ)
The cfg interpreter (the per-line `TEIGI_*` handler right after `TEIGIMdsLoad` 0x001CE9F0) stores `LIGHT_C`/`AMBIENT`/`FOG`/`BG_COL` into `main_*` globals and `URA_LIGHT_C`/`URA_AMBIENT`/`URA_FOG`/`URA_BG_COL` into `sub_*` globals (light index = last LIGHT_C argument, 1..3). `MainDraw` (0x01DAE2A0) picks them by `lightingMode` (0 main, 1 back): `MGSetPLight(0x1df8740 / 0x1df8780)`, `MGSetAmbient(0x1df87c0)`, `MGSetBGColor(sub_bgColor ..)`, `MGSetFogParm(sub_fogRate ..)`. `lightingMode` is set by the 0xa0 body and by `GameInit`. d01main_a: ambient 0 for both; normal lights (120,110,100)/(60,40,30), fog 380..650; back lights (100,110,120)/(30,40,60) (blue), fog 300..600, bg black. The port parses all of it (`CfgEnv front, back`); only `DcDungeon::get_environment` (always `cfg.front`) ignores the back set.

### 2.3 HUD / banner / minimap (READ)
* Floor number: `UserStatus+2 + 1` (d06 through `BtGetFloorLevel`), same as the main floor. `_SET_FLOOR_TITLE` (0x001BCFE0 -> `FloorTitleOn` 0x01DC1290) shows the banner at the end of the entry scene. In `MainDraw` and its copies the banner has: main = optional "LIMITED ZONE" (source rect 0,0x78,0xf0,0x30 -> dest 0xd4,0xe6, only if `UserStatus+0x8b10 >= 0`); back = **"BACK"** (source 0x110,0x78,0x60,0x38 -> dest 0x110,0xe6, no limited-zone test) plus the same floor-number digits (checked against `data/dun/img/us/dname00.img`: the atlas holds the digits, "LIMITED ZONE" and "BACK").
* `topStatusInfo` (0x001B04F0): when `BtUraDongeon != 0` one extra sprite from the HUD texture next to the floor plaque: source (0xda, 0x2b, 0x26, 0x29), destination x = 0x23c, same y as the plaque (the plaque itself is source 0x9a,1,0x66,0x29 at x = 0x1fc).
* Minimap / explored cells: `DrawMiniMap`, `checkMask`, `FlushCheckMask` are methods of `NowDngMap`, so the back floor has its own explored mask (fresh after the build), tiles from the same `MINIMAP_IMG`.
* `BtEquipMap` / `BtEquipMasuisyou` (map / crystal collected) are reset only when a new floor is built, so they carry into the back floor, where no map or crystal chest exists.

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 3. What is different inside

### 3.1 Monsters (READ for code, VERIFIED-DATA for tables)
* Table: `BtUraEnemyLayoutList` 0x002917D0 (7 pointers to `BtUraEnemyLayout00..06`, 0x70 bytes per floor, 9 slots of {kind, monster id, weight}); `BtLoadMonstor(1)` reads slot[+4] only, row = `UserStatus[2]` (same floor number as the main table, already loaded as `tables.layout[1]`). Back rows are non-empty exactly on the random floors: d01 12 floors (empty 3, 7, 14), d02 15 (8, 16), d03 16 (8, 17), d04 16 (8, 17), d05 13 (7, 14), d06 24 (24, 25), d07 99 (99) = **195 back floors**. Example rows: d01 f0 {e03a, e42a, e101a}, f9 {e35a, e34a, e77a}, f13 {e77a}; d02 f1 {e12a}; the back rows use 2..4 models and a different mix than the main rows (e.g. d01 f2 main {e60a, e03a, e06a, e05a, e103a}, back {e03a, e42a}).
* Count and placement: `ArrangementPos(unit, UraDungeonMap, 8, -1, 0)` (main 15); uses the back map's `SearchiDoPutArea` data and the back floor's chests / trap circles (no Atla).
* `CMonstorUnit::CleanViewMonstor(unit, flag)` (0x001DF9F0) stores `flag` in **unit `+0x44`** (the port calls it `double_weapon_exp`). Its readers (all four, grep of the unit code): `SetKey` (0x001D7C80) returns at once when `+0x44 != 0` -> **no gate-key carrier in the back floor**; `CMonstorUnit::Step` (0x001DD540): per awake monster with `slot+0x1e4a4 != 0` it does `if (unit+0x44) slot+0x1e3e0 = 0xb4` (180) BEFORE the hit-box gathering and `CheckDmg`; the kill reward `if (unit+0x44) exp <<= 1` (weapon experience x2, before the 1.2 multiplier).
* `slot+0x1e3e0` is the monster's "power-up" counter (`_SET_BIN2` in scripts): while > 0 its attack spheres are registered with `damage << 1`, `CheckDmg` halves the damage it takes, and the palette goes orange (ambient 255/80/15). It is decremented once a frame, so with the reset every frame it stays on: **back-floor monsters deal double and take half damage** (INFERRED from the three facts; the port already implements all three effects of `bin2`: `monster_combat.cpp:440`, `:480`, `:678`).

### 3.2 Items and chests (VERIFIED-DATA, ELF dump)
* `ItemPutListPtr[map + 7]` (0x002738F0..): one row for floors below `floorNum[map]` (key 0x100) and one for the rest (0xff); no per-floor rows. Content (the same list in every dungeon except the weapon part and d07's shorter small list): 52 small ids (d07: 37, without the consumables from Stamina Drink on) = attach materials 81..85, 91..106, 111..120, Flame/Chill/Lightning/Wind/Sacred Gem, Stamina/Antidote Drink, Sacred Water, Soap, Mighty Healing, the four amulets, Dran's Feather, Throbbing Cherry, Gooey Peach, Bomb Nuts, Poisonous Apple, Mellow Banana, Powerup Powder. **Big (weapon) list empty in d01..d05** (`PresetSmallItemNo_Get(..., small = 0)` returns -1 there, 12 % of the box tries just fail), 45 weapon ids in d06, 36 in d07. No key items, no repair/escape powders, no food.
* Piero ("Happy Clown", script function 16 -> `_GET_PIERO_ITEM`, ext 1100 at 0x001BD790): `GetPieroItem(selectMapNo, BtUraDongeon, &a, &b)` (0x001BFAB0) reads `PieroItemListPtr[map + 7*ura]` (0x00279CF0): shallow set (floor < `floorNum[map]`) = lists A (count `p[0]`, items `p[2+i]`) and B (count `p[1]`, items `p[0x42+i]`), deep set = `p[0x82]` / `p[0x83]` with items `p[0x84+i]` / `p[0xc4+i]`; each pick: `idx = (int)(count * rand() / 2^31)` (0 if out of range), `roll = (int16)(rand() * 100 / 2^31)`, rejected while `roll <= rate[item-1]` where `rate = *(int16*)(ItemSetRateTbl[map] + 2*(id-1))` (the real table behind the pointer, unlike `PresetSmallItemNo_Get`), then `_SITEM_GET(a); _SITEM_GET(b)`. The back lists are almost the same lists as the main ones (A = weapons/food/amulets, B = attach materials + key items + gems); the port stub `case 1100` returns (-1, -1) for both floors.
* Drops: per-floor `CRandomItem` (`MainRandomItem` 0x01EA76C0 / `SubRandomItem` 0x01EA7CE0, 1,568 bytes each) switched with `RandomItem`.

### 3.3 Completion, rewards, flags
Nothing: no boss, no exit, no `_SSET_GAME_FLAG`/story int in 200/201 or anywhere else tied to the back floor, no entry in `UserStatus` for "back floor cleared". The rewards are the chests, the Piero boxes, doubled weapon exp and the monsters' drops.

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 4. Save state

* `BtUraDongeon` (0x002A34B4, runtime) is the only "in the back floor" state; it is zeroed by `BtCleatRandomMap`/`BtCleatFreeMap` and by the overlay's BSS clear; it is not part of `UserStatus` or `SaveData` (the game cannot be saved inside a dungeon anyway: "Leave dungeon temporarily" goes to town and the next entry rebuilds everything).
* `UserStatus+2` (floor), `+3` (old floor), `+0x42bf+map` (deepest floor), `+0x8b10` (limit zone) are untouched by the swap.
* Per-floor event flags `UserStatus+0x8aa4 .. +0x8ad5` (50 bytes): the unlock flag lives here (d01 index 1, d02..d07 index 0; index 1 is also used by d06/d07 function 160), cleared by `ClearEventFlag` when a floor is built.
* Event-manager switch states and item-use flags of the two floors are runtime only (`DngEventMan` / `UraEventMan`).

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 5. Where it plugs into `native/`

### 5.1 What already exists
* `dccore/dungeon.*`: `build_random_map(seed, rooms, map_no, fresh, GenState*)`: `fresh = false` implements `a2 = 0` exactly (no divider, no stairs, `set_start` only, `GetRoomLinkInfo` / chara parts skipped, the `rand()` draws of the roll and of the map-1 tint kept). `seed` is the LCG state itself (`GameRand::seed` assigns `state`), so passing the current state continues the stream as the game does.
* `dccore/events.*`: `build_event_data(..., main_floor = false)` = flag 0 (table 1 lists, no 0xe9/0xea, no Atla); `EventTables::preset_item` already takes `table` and returns -1 for an empty weapon pool like the game.
* `dccore/tables.*`: `layout[1][map][floor]` (row counts identical to the main tables), `MonsterUnit::load_floor(..., back_floor)`.
* `dccore/floor.*`: `DungeonCfg::front/back` parsed, `floor_markers()` already yields the id-200/201 markers of any layout.
* `dccore/monster_ai.h`: `MonsterWorld::double_weapon_exp` (= unit `+0x44`; `set_key` returns on it, `drop_loot` doubles the exp), `bin2` effects.

### 5.2 What has to change
1. **`gdext/src/dc_models.cpp` `start_floor` (and `build_walker`)**: after `build_random_map(main)` + `build_event_data(main)` do what `BtCleatRandomMap` does next: `ura = build_random_map(rng_.state /* seed = running state */, 6 /* the game passes 6 here */, map_no, false, &gen)`; `rng_.seed(ura.rand_state)`; `ura_events = make_floor_events(ura, map_no)` (empty: no link-door chest); `build_event_data(ura_events, ura, ..., floor, false, rng_)`; only then take `monster_rand_state = rng_.state` for the main monsters. Clear `status_.raw[0x8aa4 .. +0x32]` (ClearEventFlag) right after the build (not done anywhere today: the d01..d07 unlock flags would leak into the next floor) and zero the "map / crystal" HUD flags as the game does. Only for map kind 1; story / boss rooms stay single.
2. **`DcWalker` needs two floor states and a swap**: keep `{FloorLayout, EventWorld (chests, markers + MarkerState, door_open), explored mask visited_/room_seen_, drops, DcDungeon node + chest/atra/marker/trap nodes}` for main and back, and add `swap_back_floor()`: toggle `back_`; point `layout_` (the `Walker` holds `&layout_`) and `events_` at the other state; show the other `DcDungeon` node (build it lazily, hide the other) and call the environment setter with `cfg.back`/`cfg.front`; reload monsters WITHOUT rebuilding `MonsterWorld` (`set_monsters` re-creates it and resets the player's status record): `monsters_->load_floor(disc, tables, map, floor, back)`, clear `actors`, `monsters_->arrange(layout_, tables, map, rng_, back ? 8 : 15, -1, spawn, &events_->ev)`, `world_->double_weapon_exp = back`, call `set_key` only for the main floor, rebuild the monster nodes; `RsetMimicEvent` of the target floor (drop its type-8 events). `DcDungeon::get_environment` must take the back set (`cfg.back`) and `dungeon_run.gd` must re-run `_setup_environment` and re-init `hud.set_map` (minimap of the other layout) on the swap.
3. **Per-tick marker handling, `dc_walker.cpp` `poll_script_markers`**: remove 200 and 201 from the skip list (the functions exist in all seven scripts); run them like any script marker: mode 1 on OK, mode 2 on the use button only if the slot's item-use flag is on. `MarkerState` needs an `item_use` flag initialised from `PartMarker::use_item` and cleared by `_HOLD_ITEM_EVENT`; `set_event_sw` already acts on `events_` (the active floor), which is the right manager after the swap.
4. **A `DungeonSession` for random floors**: `assemble_walker` only creates one when `story_next_`. Create it for every kind-1 floor (`set_dungeon_session(session, -1)`: no floor-start run) so functions 200/201 (and later 15/16, 160 ...) can run. In `dungeon_session.cpp`: `case 1036` (`_SET_URA_DUNGEON`): post a request (new hook, e.g. `std::function<void()> set_ura_dungeon`) that the host executes at the end of the tick (the script `yield`s right after it) and that also moves `event_pos_` to the target floor's id-200/201 marker; `case 1090` (`_HOLD_ITEM_EVENT`): hook to clear item-use of that id in both floor states; `case 1034/1035` (`_GET_OBJHDL_POS/ROT`) and `1041` (`_GET_EVENT_ROT`): today they return nothing/0, but the entrance scenes build the hero's final position from them (`_SET_WORLD_COORD(GET_OBJHDL_POS(entrance frame))` + `_SET_NPC_POS(-1, offset)`): return the entrance marker's world position / the cell rotation (part 0x28 is always rot 0) so `place_hero` lands next to the entrance of the NEW floor (offsets in 1.3). `case 1073` (`_SET_FLOOR_TITLE`): hook for the banner ("BACK" when the back floor is active, digits = floor + 1).
5. **`_ITEM_USE_WINDOW`** (1021) currently picks the first listed item the player owns without asking; for the entrance this auto-unlocks on the first use-button press (and d03's list 226, 227 prefers 226). Acceptable as a first step; the real picker is a restricted bag list (`BtMiniItemSelect`, result id or -1).
6. **Monsters**: `MonsterWorld::step_actor` start (monster with `active != 0`): `if (double_weapon_exp) a.bin2 = 0xb4;` (before `gather_polys` / `check_dmg`); `DcWalker::set_monsters` hard-codes 15 monsters: make the count a parameter (8 for the back floor). The game's draw tint for `bin2 > 0` is not ported (cosmetic).
7. **HUD** (`native/godot/dungeon_hud.gd`): while on the back floor draw the extra plaque sprite (source 0xda,0x2b,0x26,0x29 at x = 0x23c) and make the floor banner say BACK (source 0x110,0x78,0x60,0x38) instead of LIMITED ZONE.
8. **Piero** (`case 1100`): port `GetPieroItem` (tables `PieroItemListPtr[map + 7*back]`, rates from the pointer in `ItemSetRateTbl[map]`), used by both floors.

### 5.3 Things this research found about the existing port (not back-floor specific)
* **Start cell**: `Walker::place_at_start` puts the player on the cell with flags 0x200 = the back-floor entrance (part 0x28). In the game the random floor is entered at the in-stairs part 0x1e (flags 0x80): function 100 runs `_SET_RANDOM_MAP; _LOAD_MONSTOR; fn_34e14` and function 150 (= fn_34e14) sets the world coordinate from the frame `in_2` of the stairs part and puts the hero 2 units in front of it (`_SET_NPC_POS(-1, 0, 0, 2)` after the `in01.chr` walk-out). `DcWalker::setup` already marks the 0x1e cell visited, which is consistent with that. Worth checking whether the start position of the port is a deliberate shortcut.
* **rand() order** (2.1): the main floor's monsters (and everything else drawing from the running generator before the next floor) are positioned from a state that lacks the back floor's draws.
* **`SetUnderLoad` part 0x2c** is cfg block `no44` "裏マップ通路" (d01o15_0), not just a blank filler; it is part of both floors.
* **Event flags** are never cleared between floors (needed by the unlock logic above).

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 6. Open items

* Text of the entrance windows of d02 (ids 300..303), d03 and d04 (ids 20..22): not found in any shipped bank.
* Exact hero placement offsets after the swap were read from the scripts but never measured in the game (the object frames involved, e.g. `d01mob`, are positioned by `DrawMap` of the active map; the port has no frames for part objects).
* Whether the monster "power-up" tint is visible exactly as in the main floor's `bin2` state (palette flags `+0x1e520..+0x1e530` are set in `Step`, READ, but not run in the game).
* The behaviour of `_ITEM_USE_WINDOW`'s real picker when the bag holds none of the listed items (window opens with an empty list?).
* Live check with the PCSX2 rig (`coop/hooks.py` mailbox): call `BtCleatRandomMap` after `srand(n)` and compare the ura grid (`UraDungeonMap + 0x9c50`, 20 x 20 cells of 0x10 bytes: part, rot, ...) with `build_random_map(state_after_main, 6, map, false)`, then read `BtUraDongeon` and the monster count after triggering `gameTask = 0xa0`. Everything above is from static reading.

---------------------------------------------------------------------------------------------------------------------------------------------------------

## 7. Port state (implemented, `native/`)

Verified with the harness (`work/back_all.py`, `work/back_smoke.py`, part of `work/smoke.sh`): the entrance of all seven dungeons runs its real script (use button with the key item -> "unlocked" -> OK -> yes/no -> scene -> swap), the hero arrives at the entrance of the other floor and the same scene brings him back. d01: 8 back monsters = the back row {e03a, e42a, e101a}; BACK banner; blue back lighting; the chests / doors / explored cells stay with their floor.

* **Generation** (`DcModels::start_floor`): after the main events `build_random_map(rng_.state, rooms, map, fresh = false)`, `make_floor_events`, `build_event_data(..., main_floor = false)`; only then the running stream goes to the main floor's monsters (the order the game has, 2.1). The per-floor event flags (`status +0x8aa4 .. +0x32`) are cleared at every floor start (`ClearEventFlag`). Handed to the walker through `DcModels::back_next_` -> `DcWalker::set_back_floor`.
* **Swap** (`DcWalker::swap_floor`, run at the end of the tick in which the script's `_SET_URA_DUNGEON` (ext 1036) was called; the script yields right after): the layout, `EventWorld`, floor node, chest / statue / marker / trap nodes, explored mask and marker bookkeeping of the two floors are exchanged in place (`FloorSlot shelf_`; the walker, monsters and events keep their pointer to `layout_`); the back floor's node and event world are built the first time. The one monster unit is wiped (`MonsterWorld::clear_monsters`, the player's own effect stays), reloaded with the other row (`MonsterUnit::load_floor(..., back)`), 8 / 15 monsters arranged from the live stream, `world_->double_weapon_exp = back` (no gate key, exp x2) and `step_actor` keeps `bin2 = 0xb4` (double damage dealt, half taken). Mimics are re-attached (`attach_mimics(true)`). Notice kind 41 (a = back) makes `DungeonRun` rebuild the environment (`DcDungeon::set_back` -> `cfg.back` lights / fog / background), the HUD's map and the extra plaque; kind 42 (`_SET_FLOOR_TITLE`) starts the banner ("BACK" instead of LIMITED ZONE on the back floor).
* **Markers**: 200 / 201 run like any script marker; the item-use button works while `MarkerState::item_use` is on, `_HOLD_ITEM_EVENT` (ext 1090) clears it on both floors.
* **Object handles** (ext 1030..1035): `find_floor_frame` also finds the model objects of a part (`d01mob`, `udoor`, `mov`, `eye` ...) by the name before their `__flags` suffix; `_SET_OBJHDL_POS/ROT` store an offset in the part's axes, `_GET_OBJHDL_POS/ROT` return the frame's CURRENT world place (the frames are shared by the two floors, so after the swap they are where the new floor has them) plus that offset. The scenes' final placement (`_SET_WORLD_COORD` of the cart / carpet / sculpture frame, `_SET_NPC_POS`) therefore lands next to the new floor's entrance.
* `dccoll <iso> <stem> <seed> <rooms> <map> 0 0 --find <name>` lists every placed part that has a frame / object of that name; `--frames` now also lists the model objects.

* **`_ITEM_USE_WINDOW`** (ext 1021) opens a real window (`DungeonRun._open_item_pick`: the bag, Enter / Esc, answer = item id or -1) through `EventRuntime::begin_modal(11, ...)` / `DcWalker::open_item_modal` (notice 43) / `DcWalker::modal_done`; the harness (`play.gd`) sets `auto_pick` (the first listed item the bag holds) unless `--manual-pick 1`.
* **Piero (ext 1100) and trapped chests** are ported, see notes/dungeon_items.md section 3.3.

* **Moving objects** (ext 1030..1033): the objects a dungeon script can address (every identifier-like string of its `event.stb`, `DcModels::script_names`) get a node of their own with their children (`DcDungeon::set_dynamic_objects`; `Surface::path` is the chain of parents); `_SET_OBJHDL_POS/ROT` becomes `DcDungeon::set_object(name, offset, rotation)` (turned about the object's own frame, then moved; angles negated for Godot), `_SET_OBJHDL_DRAW_FLAG` `set_object_visible`. The d01 tram ride is visible (hero in the cart).

* **Object animations** (`_HOBJ_BT_HOBJ` + `_OBJ_ANIME`, ext 332 / 650 / 651): `EventRuntime::step_obj_anime` is `ObjAnimePlay` (decompile at 0x1661e0: the current value goes to the handle, then moves on by the speed; modes 0 add forever, 1 start again, 2 bounce, 3 stop at the end; arguments `(slot, handle, kind 0 rot / 1 pos / 2 scale, mode, speed[3], start[3], end[3])`, rotations in radians); `DcWalker::apply_object_anims` turns the group's rotation into `DcDungeon::set_object` for every dungeon object handle behind it (nested dynamic objects: the tram's wheels hang under the tram). The d01 tram's doors open and its wheels turn. Only the dungeon (kind 6 handles); the towns' handles do not use it yet.

Live comparison of the whole floor start (2026-10-20, `tools/dc/validate_back_all.py`, port side `native/dccore/tools/dcback.cpp`): the rig `srand(s0)`s the real game, calls `BtCleatRandomMap`, `BtLoadMonstor(0)` + `BtArrengeMonstor` (main monsters), then emulates gameTask 0xa0 (`NowDngMap = UraDungeonMap`, `CleanViewMonstor`, `RsetMimicEvent`, `BtLoadMonstor(1)`, `srand`, `ArrangementPos(unit, Ura, 8, -1, 0)`) and compares **cells of both floors, every event / chest / atra / trap slot of both floors (mimic chests included) and the position + hp of the 15 main and the 8 back monsters** with the port from the same generator state. Result over d01..d07 (2 floors each, 6 random states per floor, 84 floors): **78 identical**, the other 6 differ only because of the game's uninitialised-stack read in `PresetSmallItemNo_Get` (below). Bugs this found and fixed: (1) **d05 never matched the game**: `buildDummyModel` (map 4, 3 `rand()` per try until 8 scenery blocks stand, `d05c01..03.mds`) was missing, which shifted the rand stream of every d05 floor (layout of the link door, chests, monsters, the whole back floor); it is ported now (`FloorLayout::dummy_pos/dummy_kind`, `DUMMY_MODEL` in the cfg, drawn by `DcDungeon::setup`); (2) the stack rates below.
**Stack rates**: `PresetSmallItemNo_Get` copies 190 rates into a 512-byte local array and then reads the rate of item ids 191..400 from the rest of its stack frame (and a local array behind it). That memory is what the callers left there. `tools/dc/probe_preset.py` replaces both `jal PresetSmallItemNo_Get` of `buildEventData` by a stub that copies the callee's frame before each call (4 dungeons x 3 seeds, main floor and back floor): the slots that are identical in every recording (pointers to `MainDungeonMap` / `UraDungeonMap`, a stack address, float halves, small constants) are now in `kStackRatesMain/Back` (`events.cpp`), e.g. rates of 16256 / 19424 / 26968 mean "never picked", -1280 / -708 "always". A group of slots changes with the seed (pairs of small ints 0..14 that look like the cell list `GetRoomLinkInfo` leaves on the stack); they are taken as 0, so a floor differs from the game when such an item is picked with a roll below that value (about 7 % of floors). The weapon-ownership penalty in the same function (rates of owned weapons drop by 10 / 20) is not modelled.
Not compared live: the monsters' later behaviour on the back floor (it uses the same AI as the main floor), the back floor's Atla statues (the back build never places any).

* **Co-op**: the swap is shared (notes/coop_native.md step 6): `CoopDungeon.on_ura_swap` / event `ura_swap`.
