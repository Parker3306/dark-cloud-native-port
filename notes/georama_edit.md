# Georama EDIT MODE (town rebuilding): data model, rules, loop

Scope: how the player rebuilds a town (EditMode / `GameMode` 4, the edit menu `GameMode` 6, the Atla board, requests, completion events) as implemented by the main executable (SCUS_971.11 + the dungeon overlay, decompile `work/decomp/all_dun.c`, names/addresses from `notes/symbols.csv`).
Companion of `notes/georama.md` (file formats, `mapinfo.cfg`, PTS, CEditArea/CEditGround basics: not repeated here) and `notes/events.md` (event VM, ext functions).

Evidence tags: **[VD]** = verified against data on the disc (tool given), **[R]** = read from the decompile/disassembly, **[I]** = inferred, **[?]** = unresolved.
Terminology: *town* t = 0..4 = `MapNo` / `NowEditMap` = e01..e05 (Norune, Matataki, Queens, Muska Lacka, "Yellow"/e05); index 5 exists in the tables as a dummy sixth town (data present, never edited). *part type* p = index in the `BLD_PARTS/GRD_PARTS/ROAD_PARTS/...` list of `mapinfo.cfg` (0..23). *Atla* = the collectable items of the dungeons that unlock parts and "tips".

## Tools (all in `tools/dc/`, run with `python -I`)

| tool | what |
|---|---|
| `georama_edit_tbl.py` | dumps the static tables of the ELF: `EditPartsData` (default), `--join` (with mapinfo + PTS, flags the stale PTS element ids), `--elem`, `--chara`, `--atra`, `--names` (English names from `commenu/a_usa/emenu.pak:atrames.bin`), `--json` (everything) |
| `georama_requests.py` | Python re-implementation of the five `*Request` functions + primitives; evaluates the developer layouts `gedit/e0N/gdata0.edt` (see 6.4) |
| `mesdump.py` | dumps a `.mes` bank (e.g. `gedit/system/editsys_1.mes`) |

---------------------------------------------------------------------------------------------------------------------------------
## 1. Save data and runtime objects

### 1.1 Per-town save block (3000 bytes at `CSaveData + 0x67c + 3000*t`, t = 0..5) [R, VD for sizes]
`CSaveData::Initialize` 0x158b60 memsets the whole save (0x131c0 bytes) to 0 and then sets the three lists below. All offsets are inside the 3000-byte block (absolute = `0x67c + 3000*t + off`); the block is fully used: 0x68 header + 24*0x18 part records + 0x82*0x10 placements + 20*12 NPC records = 0xbb8 = 3000 [VD]:

| off | size | field |
|---|---|---|
| 0x000 | u32 | **`GrdData[0]`** = total number of requests of the town (recomputed by every `RequestCheck`; saved) |
| 0x004 + 4*i | u32 x24 | `GrdData` **request[i]** (0/1), i = part type (see 6) |
| 0x064 | u32 | **request-event flag** (`_SSET_REQUEST_EVENT_FLAG(town,v)` 0x1954d0; the "all requests done" event 0x83 runs only while it is 0) |
| 0x068 + 0x18*p | 24 x 0x18 | `EditPartsInfo` record of part p (`GetEditPartsInfo` 0x157ff0): `+0 u32 flags` (bit0 = completion event was played, `SetCompEvent`), `+4 i16 obtained` (0/1: the Atla part was found; **`_SGET_ATRA_PARTS_NUM` counts these**), `+6 i16 placed` (copies on the map, saved but recomputed on load), `+8 i16 max` (current maximum number of copies, 0 = use the table value), `+10 i16[6] installed` (element k installed flag) |
| 0x2a8 + 0x10*n | up to 0x82 x 0x10 | **placement list** (`GetParts` 0x158060 counts entries until `i16 part < 0`): `{i16 part, i16 rot, f32 x, f32 y, f32 z}`; terminator `part = 0xffff`. Roads/rivers/bridges are stored with the *base* part number (first part of that kind), x/y/z = `CMapParts` position. Same record format as `gdata0.edt` |
| 0xac8 + 0xc*n | 20 x 12 | **`GrdNPCData`** (`GetGrdNPCData` 0x1580d0, villager n = `VILLAGER_INFO.+0x44`): `+0 u32 flags` (**bit 2 = the player has asked this villager about his/her request/house**: set in `TalkMode` when menu entry #3 (4th) of a normal villager talk is chosen), `+4` unused, `+8 i32` talk-message counter used by the scripts (`_SGET/_SSET_NPC_TALK_MES`) |

Separate from the block: **tip tray** = `GetElemData(t)` (0x1581a0) = `CSaveData + 0x4ccc + 0x100*t`: 0x80 `i16`, -1 = free slot, otherwise the id of a collected "tip" (= element id, 6.1); `AtraChipGet` 0x158760 puts a chip in the first free slot.
New game: everything is 0 except the tray (-1) and the placement terminators: **no part is obtained, nothing is placed, no request met** [R: Initialize].

Save/load glue: `EditSave` 0x177bf0 = `CEditGround::Save(t, SaveData)` 0x1a4c90 (placement list) + `CEditPartsInfo::Save` 0x19a2c0 (+ clock); called by the Save menu (`MenuSave` -> 0x151614 site) and `EditExit` 0x177d40. `EditLoad` 0x177c80 = `CEditPartsInfo::Load(t, save, 0)` + `CEditGround::Load(t, save)` 0x1a4e10 (rebuilds the 0x10-byte list exactly like `Load(char*)` of `gdata0.edt`) + `EditPartsObjectOnOff` + `MakePartsBox`.
`CEditPartsInfo::Load(...,param_4=0)` (used by EditLoad) reads obtained/flags/max/installed but **not** the placed count (recomputed by re-placing the parts) and **not** the requests; `param_4=1` (used by the Atla menu for another town, `BtEditPartsInfo`) also reads placed count, request[] and `GrdData[0]`.

### 1.2 `CEditPartsInfo` (runtime, `EditPartsInfo` at 0x1d19bf0; `CEditGround+0x15030` points to it) [R]
```
+0x000 u32 total_requests          (= GrdData[0])
+0x004 u32 request[24]
+0x064 EDITPARTS_INFO info[24]     (0xe8 each; GetPartsInfo(i) = base+0x64+0xe8*i, null if i<0 or >23)
```
`EDITPARTS_INFO` (0xe8):

| off | field |
|---|---|
| +0x00 | part type number (= index) |
| +0x04 | flags (bit0 = completion event done) |
| +0x08 | **obtained** (the Atla part was found). `CheckPartsInfo` 0x199f90 (part is usable/listed) = `obtained != 0 && max >= 1` |
| +0x0c | **placed** copies on the map (`SetMapParts` ++, `DeleteMapParts` --) |
| +0x10 | table word w2 (plate/icon order, unused by the edit rules) [I] |
| +0x14 | **completion-event trigger kind** = table word w4: 0 none, 1 played via the scripts (talk/entry), 2 played automatically when the hero stands on the part (6.5) |
| +0x18 | **max** copies allowed now (table w0; for stackable parts see 2.3) |
| +0x1c / +0x20 | footprint width/height (from the PTS header, `Initialize(..., EPARTS_INFO_HEADER*)` 0x19a740) |
| +0x24 + 0x20*k | 6 **elements** k = 0..5: `+0 id` (-1 none), `+4 rel` (index of a prerequisite element of the same part or -1), `+8 installed` (0/1), `+0xc..+0x18` four name-list pointers (object names switched by this element: mode 0 = town, modes 1..3 = interior door id+1, see 2.5), `+0x1c npc` (villager index whose `GrdNPCData.flags & 2` must be set before the tip may be shown/attached, -1 none) |
| +0xe4 | `EPARTS_INFO_HEADER*` of the PTS (footprint grid) |

* `CheckComplete(p)` 0x19a020 = every element with `id >= 0` has `installed != 0` (a part without elements - tree, road, river, bridge, pond - is always complete) [R]. Note: obtained/placed are **not** part of it.
* `GetCompEvent/SetCompEvent` = bit 0 of `+4`. `GetRequest(p)` = `request[p]` (0 outside 0..23).
* `GetNextParts(i)` 0x19a210 / `GetNextPartsNum` iterate the parts that pass `CheckPartsInfo` (the ones listed on the Atla board).
* `Initialize(t)` 0x19a570 fills all 24 records from the static table (2.1): obtained=0, placed=0, max=w0, installed=0, ids/rel/names/npc from the table. `Clear` 0x19a280.
* The PTS-embedded element ids (`georama.md` 3.2) are **stale authoring data and are not used**: the ELF table is authoritative **[VD]** (`georama_edit_tbl.py --join` flags 39 of the 72 PTS-backed parts whose element ids differ from the table; widths/heights/max come from the PTS).

---------------------------------------------------------------------------------------------------------------------------------
## 2. Static tables (main ELF) and the Atla system

### 2.1 `EditPartsData` 0x2540D0: 6 towns x 0x125c, 24 parts x 0xbc (47 words) [VD, `georama_edit_tbl.py`]
Entry (word index): `w0` **max copies** (0 = slot unused; a table row with `w0 == 0` ends the list: `AtraBoardMaxNum` 0x218780 counts rows while `w0 > 0`), `w1` **stackable** flag (>0 for the road/river/tree parts), `w2` plate order, `w3` **name message offset** (message id = `1000 + 200*t + w3` in `atrames.bin`, [VD] names match), `w4` **completion-event kind** (-> info+0x14), then 6 element blocks of 7 words at word 5 + 7k: `id, rel, name0..name3 (char* into .rodata), npc`.
Part counts/max per town and the full element lists are in `--names` output; summary:

| town | parts (type: name, max) |
|---|---|
| 0 Norune | 0 My House, 1 Macho's House, 2 Laura's, 3 Paige's, 4 Claude's, 5 Hag's, 6 Alnet's, 7 Gaffer's Buggy, 8 Dran's Windmill, 9..11 Small Windmill 1..3, 12 Pond, 13 Trees (10), 14 Road (25), 15 River (20), 16 Bridge (5) |
| 1 Matataki | 0 Pao, 1 Cacao, 2 Bunbuku, 3 Kye&Momo, 4 Baron, 5 Couscous, 6 Gob, 7 Mushroom house, 8..10 Well 1..3, 11..13 Watermill 1..3 (on-river), 14 Wise Owl Shop, 15 Tree (10), 16 River (40), 17 Bridge (5), 18/19 Earth A/B (terrain mounds) |
| 2 Queens | 0 Ruty, 1 Suzy, 2 Lana, 3 Jack, 4 Joker, 5 Divining House, 6 Cathedral, 7 Basker, 8 King's Hideout, 9 Sheriff, 10 Fountain, 11 Leaning Tower, 12 Tree (10), 13 Road (30) |
| 3 Muska Lacka | 0..7 houses, 8..10 totem poles (`e04a01..03`), 11 lake, 12 tree (10), 13 road (25) (names: **no message in `atrames.bin`**, see 9) |
| 4 e05 | 14 parts, one copy each (houses/robot parts; 0,3,4,9,10 have completion kind 2) |

Stackable parts (`w1 > 0`, max w0): **each Atla pickup adds 5 copies**, capped at w0 (road 25 = 5 pickups, trees 10 = 2, river 20 = 4). Counts agree with the dungeon tables (2.4) [VD]. Non-stackable (houses, bridge w0=5 with w1=0): one pickup unlocks the part (a second pickup is ignored).

### 2.2 `EditElementData` 0x25AF00: 6 towns x 100 x 12 bytes [VD]
Indexed by atra id: `[0..39]` part rows (index = part type; `+0 tile, +4 message offset, +8 ?`), `[40..79]` object **tips**, `[80..99]` **person tips**. Row = `{i32 atlas_tile, i32 message_offset, i32 copies}`; `copies` = how many Atla copies of that tip exist = number of element blocks that use the id (e.g. Fence in Norune = 5 houses). Tip (element) id `e` is `atra id - 0x28`: ids 0..39 are objects, 40..99 are persons (person `40 + n` = villager n of the town; **[VD] all 55 links of `EditCharaData` satisfy element.id == 40 + villager**). Tip tile in the board atlas: `(tile % 7)*0x24, (tile / 7)*0x24` (`RetCTexAtora` 0x2181e0).
Names/descriptions (English `commenu/a_usa/emenu.pak:atrames.bin`, 249 ids, ids `[1000 + 200*t + x]`): `x = w3` part name (1000 "My House" ...), `x = 0x28 + msg` object tip name (1040 "Fence", 1041 "Lamp" ...), `x = 0x50 + msg` person name (1080 "Llama" ...), `+0x3c` on those = the "not yet installed" description ("Slot", 1100.., 1140.. "Slot (character)"). Special case: town 2 part 1 (Suzy) adds `GameIntFlag + 0x1c` (a story-dependent name). Help ids 200/201/202/212: "Assemble Georama Parts in edit mode & dungeon, not here." / "See other village / Organize" / "???".

### 2.3 What `_SATRA_PARTS_GET` / `AtraPartsGet` does (0x158660) [R]
`AtraPartsGet(save, t, p)`: `rec = EditPartsInfo(t,p)`; `data = EditPartsData(t,p)`;
* `data.w1 < 1`: if `rec.obtained != 0` return 0 (already owned); else `obtained = 1`.
* else (stackable): `max = (obtained == 0) ? 5 : min(max + 5, data.w0)`; `obtained = 1`.
`AtraChipGet(save, t, chip)`: append `chip` (>= 0) to the tray (first -1 slot of 0x80).
Both are reached through `getAtraToSaveData(atraId, floor, save, town, dungeon)` 0x1b7470 (dungeon pickup): `atraId < 0x28` -> part `atraId` of town `dungeon`; else chip `atraId - 0x28`. The ext functions `_SATRA_PARTS_GET(town1, part)` / `_SATRA_CHIP_GET(town1, chip)` (1-based town) do the same; **no event script gives parts** (`grep` over every `gedit/**/event.stb` [VD]); the only user is `gedit/e02/event.stb`: ten `_SATRA_CHIP_GET(2,1)` (town argument 2 = Matataki; chip 1 = the "Sign" tip; ten copies given by one story scene, enough for the ten Matataki elements that use it).

### 2.4 Dungeon side: where Atla items are [R + VD]
`AtraAppearData` 0x25DF30 -> 6 lists (0x25CB20..) of `{atra id, floor spec, count}` terminated by atra id -1 (dumped by `--atra`). Order inside a list: a part id (0..16) followed by the tips/persons that part needs (e.g. dungeon 0: part 0 + atra 58,59,60 = Keg/Chimney/Upstairs Storage and 92,80,94 = Renee/Llama/Stray Cat). `floor spec`: `n >= 1` fixed floor n, `-1` random floor of the first half (`floor < CenterFloorTbl`), `-2` random floor of the second half (`CenterFloorTbl .. MaxFloorTbl`), at most 8 Atla per floor and not on floors in `noEntryTbl`.
`BtAtraListMake(d)` 0x1c09c0 runs the first time dungeon d is entered (`UserStatus+0x42bf+d == -1`). State in `CDngStatusData` (= `CSaveData+0x540c`): per dungeon `d`: entry list at `+0x2078 + d*0x4b0 + i*12` (copy of the table row, `count` decremented on pickup, atra id -> -1 when exhausted), per-floor slot list at `+0x278 + d*0x500 + floor*0x20 + s*4` (-1 empty, -2 "random placeholder", -3 collected, >= 0 entry index). `BtAtraFloorCyoice` 0x1c0d20 resolves the -2 placeholders when a floor is generated; `GetAtraData` 0x1bf950 marks the pickup (-3) and calls `getAtraToSaveData`.
**[VD]** The lists are exactly the Georama inventory: for every dungeon d the table contains each part p of town d once (stackable parts: `w0/5` times) and each tip e exactly `EditElementData[40+e].copies` times (= number of element blocks using it) - the only exception is e05 tip 15 (part 7), which has no dungeon source and no script that gives it [?]. So the dungeon of index d alone supplies everything town d needs.
At the start of a new game dungeon d guarantees (floor spec 1 rows, e.g. dungeon 0): part 1 + its first tips on floor 1/2, trees (13), roads (14), river (15) on floor 1, etc.

### 2.5 Element -> visible objects [R]
`EditPartsObjectOnOff()` 0x17ddb0 (every `EditMode` tick, in `EditLoad`, in GameMode 8): for every listed part (`GetNextParts`) `EdPartsObjectOnOff(ObjParts[p], info, mode 0)` 0x1850a0: for each element k with `0 <= id < 0x24`, `installed = (info.elem[k].installed != 0)`; the k-th name string (space separated list, `GetElementObjName` 0x19b680) is split into object (frame) names; for every name `FrameObjectOnOff(frame, v)` with `v = installed ? 1 : 2` (1 = visible, 2 = hidden); a leading `-` inverts it, a leading `*` marks a special frame (visible only if installed). Person elements (`id >= 0x28`) switch nothing. The toggle acts on the **template** `ObjParts[p]` whose frames the placed copies share, so the look is per part *type*, not per instance [I]. Interiors use the same function with `mode = EdInteriorJumpID + 1` (strings 1..3 of each element, e.g. `door2 -door1`) from `EditInInit`.
After the loop `RequestCheck()` runs (6).

### 2.6 The Atla board (= the "Build" entry of the edit menu) [R]
Entered from the edit menu entry 0 (`EditSwitch` 9 -> 3, `InitMenuAtora1(mode 2, town, ..)` 0x21a130 + `InitMenuAtoraSelect` 0x21a440). The same board also exists in the town walk menu (`BtlMenuMode` 1: board of the nearest town `GetNowMapTransAtraMap`, **read-only**: Cross on anything gives the error beep) and in the dungeon menu (`BtlMenuMode` 0: board of the dungeon's town, tips can be installed, parts cannot be selected for placement). Board textures `commenu/a_usa/a<t+1>.pak:a<t+1>.img`, text `atrames.bin`.
* Pages = parts with `CheckPartsInfo` true (obtained and max >= 1), in table order (`SearchAtoraInfo(n)` 0x218470). Up/Down changes page (`DAT_01da80de`) or moves between the header (cursor 0) and the six slots (cursor 1..6, `AtoraBoardEnableMovePos` 0x218b60 hides slots without element or whose `rel` prerequisite is not installed); L1/L2 and R1/R2 (`Down(5)` / `Down(10)`) switch to the previous/next town board (limit `GetAtoraMaxVillage` 0x22bc40), the board of another town is edited through `BtEditPartsInfo` and saved (`CEditPartsInfo::Save`) when leaving the page.
* **Header + Cross**: if no tip is held and `placed != max` (copies left) and board town == current town -> returns 10: the part becomes the selected part (`EditMenuStatus = 0`, `DAT_01d19bd4 = part`), the menu closes and edit mode continues in *place* state. Otherwise error beep (`ComMenuSePlay(2)`); the completion branch (`state 3`, `EditMenuStatus = 5`, event 200+p) is only reachable for the dummy town 5 (`DAT_01da80dc == 5`, East-King scene) [R, disassembly 0x21d18c].
* **Slot + Cross** (not in the read-only mode 1; **a complete part is locked**: if `AtoraAllTipGet(part)` every Cross on a slot only beeps): picks up an installed tip into the "hand" (`NowTipHavePt`: chip value, source slot) if no other installed element depends on it as `rel`, or, with a tip in the hand, **installs** it: allowed iff the slot is empty, hand chip == `elem.id`, the `rel` element (if any) is installed and `AtraTipCanDisplay` (npc gate: `id < 0x28 && npc >= 0 -> GrdNPCData(t,npc).flags & 2`). Installing sets `installed = 1`; if then every element is installed (`AtoraAllTipGet` 0x218500) SE 0xf. Tray: Cross swaps hand and tray cell, Square (0x80) sorts the tray (`SeitonAtoraTipBoard`), Circle puts the tip back (`AtoraMenuTipCancel`). The board writes straight into the live `EditPartsInfo` (mode 2, current town), so installing a tip immediately changes the look of the town.
* Pending-completion marker: `AtoraCompOrEvent(info)` 0x218610 = `table.w4 != 0 && CheckComplete && placed == max && every person element id >= 0x28 has GrdNPCData(t, id-0x28)+8 != 0 && !(flags & 1)`; the header cursor is forced to 0 when it holds.
* Texts: help window messages = `atrames.bin` ids from `AtoraMsgNoGet` 0x218da0 (part: `w3`, tip: tile row, `-0x314` -> message 0xd4 "???").
* The Analyze screen's bar 1 counts the tray as progress (4.4).

### 2.7 Villagers belong to parts (`EditCharaData` 0x267070) [R + VD]
`LoadEditMapData` (0x174390, for `MapNo < 5`) overwrites the first 16 `VILLAGER_INFO` (0x90 bytes at `EditMapInfo+0x18400`) from `EditCharaData[t][n]` (0x14 bytes, 20 rows of 400 bytes per town): `char* model` (`pNNa`, **overrides the PEOPLE2 model**), `i16 part -> +0x48` (**parts_no**, -1 = always present), `i16 element -> +0x4c` (element index k whose installed flag makes the villager appear), `i16 -> +0x5c` (`hide_until_event`: villager is hidden once the house is complete until the completion event ran). The other fields (`+8 moves`, `+0xc speed`) duplicate PEOPLE2. `--chara` dumps the 55 rows. `_GET_NPC_PARTS_NO(v,&p)` returns `VILLAGER_INFO[v].+0x48`.
Presence rule (`EdCheckVillager` 0x185c50, `EdInitVillagerOnOff` 0x186360, `EdInitVilagerPosition` 0x186510): `parts_no < 0` -> present; else the part must be *placed* (`GetPartsObject(p) != 0`) and `info.obtained > 0` and `info.elem[+0x4c].installed != 0`; then `if (CheckComplete(p) && !GetCompEvent(p) && +0x5c != 0) -> hidden`. The standing point is the part's **`person` function record** (type 1, `+0x20` = villager number) transformed by the placed instance's pose (`CEditGround::GetPeoplePos` 0x1a2d10; yaw = frame yaw + record yaw), so villagers follow their house when it is moved/rotated; positions are re-applied by `EdInitVilagerPosition` every time edit mode is left. `GetPartsObject(i)` 0x1a1880 = lowest placed slot whose type is i.
Example [VD]: Norune villager 0 (Llama, `p47a`) -> part 0, element 4 (id 40); villager 12 (Renee, `p10a`) -> part 0, element 3 (id 52) ...

---------------------------------------------------------------------------------------------------------------------------------
## 3. Game modes, entering and leaving edit mode [R: `EditLoop` 0x1797e0]

`GameMode`: 0 init, 1 walk, 2 talk, 3 clock change, **4 edit**, 5 transition into a menu (4 frames, then a frame-buffer snapshot `frame_image` and `EdInitModeFinish` 0x172d30 returns the real mode), **6 edit menu**, 7 transition from walk mode, **8 walk-mode item menu** (`menu_mode` 2 = main menu `BattleMenu` [Item, Weapon, Allies, Georama Parts, Leave dungeon, Options, ...], 3/4 shops, 5/9 item select, 6 name entry, 7 map move, 8 fishing exchange, 10 fish record), 9 pause (walk), 10 pause (edit, Start), 0xb door, 0xc interior, 0xe event, 0x10 fishing.
Edit mode is available in towns only: `(EdDebugMoveFlag > 0 || MapNo < 5)` and not in view mode and no `change_time_event`.

* **Walk -> edit**: Select (0x100, key-mode bit 2) in `GameMode 1`: `move_count = 30`; the walk camera (`MainCamera` 0x1d34540) pos/ref/angle are copied to `EditCamera` (0x1d34840); `ECursorFrame` is put on the hero; `NowSelectAngle = 0`, `EditMenuStatus = -1`, `NowSelectParts = -1`, `DrawPartsNameCount = 30`, `GameMode = 4`. Message 110 "Edit Mode" is shown for 0x78 frames at the start of every town visit (`EdWalkToEditMes`).
* **Edit -> walk**: Select again, only when no build effect is running (`CheckEffect == 0`) **and** `CheckEditToWalk(&pos)` 0x17fd20 succeeds (otherwise nothing happens). The hero is moved to `pos` (cursor x/z, y = ground + 1), camera state copied back to `MainCamera`, `GameMode = 1`, then, still in the same frame:
  1. `GrdData(MapNo)+0x64 == 0` and the number of non-zero `request[0..23]` == `total_requests` -> `FadeOutToEvent(0x83, level 9)` (event 131 "all requests fulfilled", present in e01..e05 `event.stb`; its script sets the flag with `_SSET_REQUEST_EVENT_FLAG(town,1)` [VD e01 line 0x01bf9c]). Note: with `total == 0` it would fire immediately; every town's `*Request` sets a non-zero total.
  2. `MapNo == 1` and game flag 0x14 == 0 and `request[16] != 0` (Matataki: the river runs unbroken through the three areas) -> `FadeOutToEvent(0xb, level 10)` (the higher level wins if both are due).
  3. `EdInitVilagerPosition(...)` re-places every villager (2.7).
  `FadeOutToEvent(id, level)` 0x177b70 (needs `EdDebugEventEnable`) stores `goto_cmp_event = id`, `goto_cmp_event_level = level` (a new request only replaces a pending one when its level is higher; the id-only calls pass the default level 0) and starts a 60-frame fade-out to black; when the fade is finished the main loop (GameMode 1 or 4) calls `RunEvent(id)` and fades in over 60 frames (a negative id leaves the loop instead); there is no time cost. Time never advances in modes other than 1 (`AddNowTime` only when `GameMode == 1`), so editing is free. Pad 2 (second controller) is read by `Down2/On2`: debug keys only (Cross = `Clear`, Circle/Triangle = save/load `gdata0.edt`, D-pad-L/R/Select/L1R1 = clock, `MainEditMode` 0x17dea0); a port ignores them.
* **Edit -> menu**: Triangle (0x10) or Circle (0x20) in `GameMode 4`: `EdInitMenu(1)` -> `GameMode 5` -> (4 frames) `EditMenuInit` -> `GameMode 6`. If `EditMenuStatus == 0` (a part is selected) the menu opens straight on the Atla board.
* **Menu -> edit** (`GameMode 6` returns non-zero): `GameMode = 4`, `NowSelectParts = -1`, then `EditMenuStatus == 5` -> `FadeOutToEvent(DAT_01d19be0 + 200)` (dead for towns 0..4), `== 3` keep -1, `== 0` `NowSelectParts = DAT_01d19bd4` (the part chosen on the board).
* **Start** in edit mode opens the pause menu (`GameMode 10`), Start again returns.

---------------------------------------------------------------------------------------------------------------------------------
## 4. The edit loop

### 4.1 Per-frame order (`GameMode 4`) [R]
`SetAutoRepeat(pad, mask 0xf000 (D-pad), delay 20, repeat every 5 frames)`; `NowCamera = EditCamera`; `MainEditMode` (lighting `EdSetLightParam`, wind, object animations, clock, day/night frames - see georama.md 6.4); **`EditMode()` 0x17de60** = `CEditGround::EffectTask` (build effect), `MakePartsBox`, **`MoveEditCursor()` 0x1800c0**, `EditPartsObjectOnOff()` (also `RequestCheck`); camera `Step`; `EditAreaClip`. Drawing (`MainDraw`, mode 4): `depth_of_field = 0`, `edit_mode_lighting = 1` (the fixed edit lighting/fog `TEIGI_EDIT_FOG`), no sky, `edit_mode_draw/edit_mode_grd_draw = 1` (the grid models are drawn; cells with code 0x81 have no grid quad), then `DrawPartsCursor` for pass 1 and 2.

### 4.2 Controls [R]
| input | effect |
|---|---|
| left stick | move the cursor freely (also inside the areas): `delta = (lx*cos a + ly*sin a, ly*cos a - lx*sin a) * 10` units/frame (x `0.6` in e05), `a` = camera angle |
| D-pad | one **cell** step: the pressed axis of the stick vector is replaced by `+-unit` (`GetUnitSize` of the area under the cursor; up = ly -unit, down = +unit, right = lx +unit, left = -unit) and the vector goes through the same camera-angle rotation as the stick, so the step is camera relative; auto-repeat 20/5 frames. The step is cancelled if the target is outside every area; afterwards the cursor snaps to the centre of its cell (`GetGrid + unit/2`) - only after a D-pad step, stick movement is not snapped (placement uses the cell that contains the point) |
| Cross (0x40) | place the selected part at the cursor (status 0/1), or pick up the part under the cursor (status 3) |
| Square (0x80) | status 3 only: delete (put back to stock), SE 0xe |
| L2 (0x01) / R2 (0x02) | rotate the part to place: `NowSelectAngle` += 1 / -= 1, range -1..2 (wraps), the preview turns by `AngleInterpolate(cur, target = pi*angle/2, 0.1)`, a new turn starts only after the previous finished |
| Triangle / Circle | open the edit menu (3) |
| Select | back to walk mode (3) |
| Start | pause |
| right stick X | camera yaw `AddAngle(0.03 * -rx)` per frame; L1 / R1 (only while rx == 0): yaw +1 deg / -1 deg per frame (constants `+0.0174533` for L1(0x04), `-0.0174533` for R1(0x08)) |
| right stick Y | ignored: the camera height/distance are overwritten each frame (4.6) |
There is **no undo, no manual altitude control and no "view mode"** inside edit mode (`EdCheckViewMode` is the first-person camera of walk mode). Altitude changes only come from parts with raising footprint cells (4.4). `camera_dist_mode` is a dead local (always 0). The rotation-lock flag of `BLD_PARTS` (`CMapParts+0x114`) is not tested anywhere in the edit code.

### 4.3 Cursor position and bounds [R, `GetMaxHeightCursor` 0x189a10, `LimitEditCursorPos` 0x189b80]
`EDITAREA_RECT` rectangles (`EditMapInfo+0x182d0`, 0x20 each, up to 8: `max corner` then `min corner`, `min.y` = cursor height): after the move the position is clamped: if inside a rect -> keep, y = highest `min.y` of the rects containing it; else if the previous position was inside a rect -> clamped (component-wise min/max) to the nearest of those rects; else clamped to the nearest rect by `|dx_edge|+|dz_edge|`; no rect -> y = 0. Then, if the cursor is inside an area and below that cell's altitude, `y = GetAlt(cell)` (`origin.y + alt*unit_alt`). Rects [VD]: e01 (-720,-720)..(680,610) y0; e02 three rects (-1100,-500)..(-450,900), (-900,-800)..(720,-130), (500,-350)..(1200,1000) y0; e03 four rects with y 70/0/170/0 (matching the three areas at y 70, 0, 170 plus a connector); e04 (-800,-800)..(800,800); e05 (-240,-320)..(240,320) y 100.
Sampling for placement: the part is placed in the cell containing the cursor point (`GetAreaCode` = first area whose rectangle contains it); the cursor y is cosmetic.

### 4.4 Selection state machine (`EditMenuStatus`) [R]
* `-1` just entered / after leaving the menu with Circle: nothing selected (`NowSelectParts = -1`), Cross does nothing. Help bar message 100/101 ("{Triangle} Georama Menu {Select} Walking Mode", 101 when `CheckEditToWalk` would succeed).
* `0` a new part chosen on the board: Cross places copies (`SetMapParts`) until `placed == max`, then -> status 3.
* `1` a part was picked up in status 3 (it left the map: `placed--`): Cross places it again, then -> status 3.
* `3` move/remove: help bar 130 "{Cross} Move {Square} Finish" (the text; the code: Square removes the part under the cursor, Cross picks it up; `SetFocusParts` highlights the part under the cursor `CEditGround+0x15f14`, not for protected river cells of e02 = `CheckDelete`).
* `5` only produced for the dummy town 5.
Help bar for status < 2: message 120 "{fbfe}    {Cross} Place {L2}{R2} Rotate" where `{fbfe}` = the selected part's name (`GetAtraMsgNo`), at (40,380), 560x50. Placed/removed sounds: SE 0xd (0x14d in e05), SE 0xe, menu open SE 1. Hovering a placed part shows its name (`EditNameMes`, message `GetAtraMsgNo(MapNo, type)`) after the cursor rests (`DrawPartsNameCount`, hidden for 30 frames after entering, reset to 0 on every movement).

### 4.5 Placing, removing, validity [R; footprint/`CheckParts` basics are in georama.md 6.2]
`CEditGround::SetMapParts(x,y,z, type, rot)` 0x1a0470 returns the slot or -1:
1. refused while a build effect runs (`CheckEffect`), type outside 0..23, no template, cursor outside every area, template without `EDITPARTS_INFO`.
2. `CheckParts` (footprint inside the area, free cells: a cell with a non-zero footprint code blocks if it already has a part unless the *new* part is not a road and the old one is a road; river: centre altitude <= 0 and no bridge(3)/lake(4)/on-river(5) neighbour; lake: altitude <= 0 and no river/bridge/on-river within 1 cell ring; on-river (5): only on a river cell; **grid-only digit cells are checked for occupancy too**).
3. kinds 3 (bridge) and 5 (on-river): require a river piece under the cursor (`SearchPartsID`) that is of kind 2, has variant number 1 (`+0xe8 == 1` = `RIVER 1` = the **straight** piece, shape 2 [I]) and is not in `CheckDelete`'s protected cells; the piece's slot is reused and the new part inherits the river piece's rotation; for a bridge the base *bridge* template is used. Otherwise -1.
4. `if (info.placed == info.max) return -1; info.placed++`. A free slot among 128 is taken.
5. a non-road part over road cells deletes those road pieces (`SearchPartsExtra == 1`, `DeleteMapParts`).
6. copy of the 0x2a0-byte template, `SetRotY(rot)`, position = cell corner (`GetGrid`) + half a cell when the rotated width/height is odd, `y = GetAlt`, then the footprint is written into the area grid (`CEditArea::SetMapParts` 0x16e010: code, and for codes >= 0x80 part number/slot/kind; for codes 1..9 `AddAlt(+n)`), roads/rivers re-pick their shape for the cell and its 4 neighbours (`SetRoadParts` / `SetRiverParts`, tables below).
7. Success: `SetBuildEffect(slot)` (only for kind 0): 15 frames in which the building drops from +50 units above its final y (`EffectTask`); placement and leaving edit mode are blocked meanwhile.
`CEditGround::DeleteMapParts` 0x1a1290: refused on `CheckDelete` cells (e02 only, `GetMapNo()==1`: area 2 cells (4,0),(3,7); area 1 cells (3,5),(11,3); area 0 cells (5,0),(2,7) - the fixed river entry/exit cells, for every kind but bridges); bridge/on-river parts turn back into a plain river piece (`placed--` of the bridge type); other parts: `placed--`, grid cleared (`CEditArea::DeleteMapParts` 0x16e210: codes >= 0x80 reset to -1, digit cells `AddAlt(-n)` and parts standing on them are re-seated at the new altitude), neighbour roads/rivers re-shaped; the slot is cleared and the part number/rotation returned for pickup.
Shapes (`SetRoadParts` 0x16e8b0 / `SetRiverParts` 0x16e470; code = `shape<<4 | rot`, the piece is copied from the template `ROAD/RIVER (shape-1)` and `SetRotY(rot)` with code rot 3 meaning rotation index -1; neighbours n0 = (x,z-1), n1 = (x+1,z), n2 = (x,z+1), n3 = (x-1,z); road neighbour = road; river neighbour = river(2), bridge(3) or on-river(5)). count 0: shape 5 rot 0. count 1 (shape 6, dead end): road rot = n0?2 : n1?1 : n2?0 : n3?3 (last true wins, same for river). count 2 opposite pair: shape 2 rot = n1 ? 1 : 0 (horizontal : vertical); count 2 corner: shape 1, road rot = {n1&n2:0, n2&n3:3, n3&n0:2, n0&n1:1}, river rot = {n0&n1:0, n3&n0:1, n2&n3:2, n1&n2:3}. count 3 (shape 3, T): road rot = n1==0?3 : n2==0?2 : n3==0?1 : 0; river rot = n0==0?3 : n1==0?2 : n2==0?1 : 0. count 4: shape 4. e02 only: `GetPartsExtra` pretends a river exists in six cells just outside the areas so border pieces connect. Variant model = `ROAD n`/`RIVER n` of mapinfo (`shape-1`).
Only two part types raise terrain **[VD]**: e02 parts 18/19 (4x4 mounds, centre 2x2 cells raise by 1 / 2 altitude steps = 50 / 100 units); Matataki requests 5 and 7 test for it.

### 4.6 Preview (ghost) and cursor drawing [R: `DrawPartsCursor` 0x1a3f30, `CPartsCursor::Draw` 0x1a5cb0]
Drawn twice (texture passes 1 and 2) when `NowSelectParts` is 0..23: the area under the cursor is checked with `CheckAreaRect` (footprint inside the area; else only the cursor point follows); `valid = CheckParts && placed < max` (for on-river parts (kind 5) additionally: not on a `CheckDelete` cell and the river piece under the cursor must be variant 1; quirk: for bridges (kind 3) `CheckParts` is false on a river, so their ghost is always shown in the invalid tint although `SetMapParts` accepts them). The **ghost** is the part's `+0xfc` frame (highest available LOD) when valid, `+0x100` frame otherwise; when invalid the ambient is replaced by a dark constant (`MGSetAmbient`/`MGSetPLight`, a "red/dark" tint). Ghost position: cell centre, smoothed toward the target by 1/8 of the distance per frame (snap below 1 unit), y = ground + 5 + 50, yaw = `NowCursorRotY`. Around the footprint a frame made of half-cell tiles is drawn (`CPartsCursor` at `CEditGround+0x20744`, three models `gedit/e01/mds/e01a03_0.mds` corner, `e01a04_0.mds` edge, `e01a05_0.mds` inner, scaled by `unit/100`, `2w x 2h` tiles, rotations by multiples of 90 degrees); its colour follows the same dark tint when invalid. `cursor01/02.mds`, `bic.mds`, `ibox_0/1.mds` (loaded by `LoadTexture` 0x180c00) are **walk-mode** helpers (villager "can talk" marker, hero arrow, treasure markers), not edit-mode cursors. [?] the model of `ECursorFrame` itself (it carries the cursor position `+0x220`; nothing is drawn for it when no part is selected).

### 4.7 Camera [R]
Edit camera = `CCameraFollow` 0x1d34840: follow point = cursor position; eye = follow + `(dist*sin a, height, dist*cos a)`, ref = follow (`Step` 0x1247d0: effective angle `+0x2dc` chases the target angle `+0x2d8` with `AngleInterpolate(.., max(1, speed/2))`, camera speed 8.0 normally (`SetSpeed(0x41000000)`), 10.0 while `move_count > 0`, i.e. the first 30 frames after a mode change).  Each frame `MoveCamera` (right stick/L1/R1, 4.2) and then `SetDistance(1000)`, `SetHeight(1000)` (arrays `{1000,2000,4000}`, `{1000,1400,2000}` index 0); in e05 (`MapNo == 4`): distance `0.2 * 1000`, height `0.6 * 1000`, cursor scale 0.6 and place SE 0x14d. The camera never gets closer; view is the classic 45 degree look-down at distance 1000.
Walk camera for comparison: `MainCamera` follow distance `camera_near_dist`, height 5 (`EdInitCameraParam`).

### 4.8 `CheckEditToWalk` 0x17fd20 (may the player leave edit mode here?) [R]
Collects ground polygons around the cursor (`PickUpPoly` box +-100 in x/z), then for four sample points P, P+(0.1,0.5976), P+(-0.581,-0.2976), P+(0.351,-2.6776) casts a ray down from y+500; each must hit (`CheckHit`), the hit height must equal the edit-grid altitude of that point within 0.1 and the point must be inside an edit area (area code >= 0), otherwise the answer is "no". A road (kind 1) under the cursor point overrides the result with "yes". The output position is the cursor point with `y = hit + 1`. So you cannot leave while the cursor stands on a building, water, a terrain mound or outside the areas. (`EdDebugMoveFlag > 0` skips the tests.)

### 4.9 Edit menu (GameMode 6) [R: 0x210ab0.. `EditMenuLoop` 0x210ea0]
Entries (`GetEditMenuMax` 0x2102e0: 6 if the manual flag `GetGameFlagForManualMenu` is set, else 5), `EditSwitch` values in `{9,10,11,12,13,14}`, icons `EditMenuIconID = {13,10,11,5,4,6}`, help texts `commenu/a_usa/emenu.pak:allmenu.mes`:
| # | EditSwitch | entry | help text (ids 21..26) |
|---|---|---|---|
| 0 | 9 -> 3 | Config&Assembly = the Atla board (2.6) | "Configure&assemble georama parts." |
| 1 | 10 -> 4 | Move&Remove: closes the menu and sets `EditMenuStatus = 3` | "Move&remove georama parts." |
| 2 | 11 -> 5 | Georama Analysis (screen below) | "Analyze current town status." |
| 3 | 12 | Save (the normal save menu; calls `EditSave` first) | "Save game progress." |
| 4 | 13 | Options | "Change game configurations." |
| 5 | 14 | Manuals | "Instruction on controls." |
Up/Down (0x9000/0x6000) move, Cross selects, Circle (0x20) leaves (`EditSwitch 0xf`, slide-out 22 frames, `EditMenuStatus = -1`). The menu cursor starts on the first entry each time.
**Georama Analysis** (`ToAnalyzeEdit`, `AnalyzeBarDraw` 0x212320): three animated bars, all in %: (1) overall = `(#obtained parts + #installed elements + #tray chips) * 100 / (#parts with max>0 + #defined elements)`; (2) complete parts `/` parts with max>0 (obtained ones only count if `CheckComplete`); (3) requests = `min(100, #non-zero request[i] * 100 / total_requests)` (`AnalyzeRequestPer` 0x212270; 100 if total < 1). It is the only in-game display of the request progress.

### 4.10 Texts used by edit mode (`gedit/system/editsys_<lang>.mes`) [VD]
100/101 `{Triangle}Georama Menu {Select}Walking Mode` (icon/colour code `fd10` / `fd11` at the end), 110 `{Select}Edit Mode`, 120 `{fbfe}\n{Cross}Place {L2}{R2}Rotate`, 130 `{Cross}Move {Square}Finish`; fishing 200..210; 1000..1210 villager names. Glyph codes (inferred from usage): fd00 Select, fd04/fd05 L2/R2, fd06 Circle, fd07 Triangle, fd08 Cross, fd09 Square.

---------------------------------------------------------------------------------------------------------------------------------
## 5. Completion events (200..223)
* Event number = `200 + part type`. Present in `event.stb` for exactly the parts with a non-zero table `w4` [VD]: e01 200..208, e02 200..207 + 214, e03 200..209, e04 200..211 (208..211 have `w4 == 0`: script-triggered), e05 200,203,204,209,210.
* **w4 == 2** (`MoveChara` 0x17f460, every walk frame after 60 frames in the map): the part under the hero (`GetParts(hero)`), if `info.+0x14 == 2`, `!(flags & 1)` and `CheckComplete` -> `FadeOutToEvent(partType + 200)`. (Windmill in Norune, Kye&Momo and Mushroom house in Matataki, ...)
* **w4 == 1**: started by the scripts: the villager's talk function 256 checks `_CHECK_COMPLETE_PARTS(p)` and `_SGET_CMP_EVENT(p)` (e.g. e01: Gaffer's Buggy: `if complete && !cmp_event -> _FADEOUT_TO_EVENT(207)`), the map entry function 128 does similar checks. The event itself calls `_SSET_CMP_EVENT(p,1)` at its end (bit 0 of `info.+4`), which makes the villagers with `hide_until_event` appear and stops the repeat.
* Event 131 (0x83): all requests (3); 11: Matataki river (3); 134/133/132, 150, 300+: other story/entry scripts (see events.md).

---------------------------------------------------------------------------------------------------------------------------------
## 6. Requests (the villagers' arrangement wishes)

### 6.1 Mechanism [R]
`CEditGround::RequestCheck()` 0x1a5fe0 runs after every `EditPartsObjectOnOff` (each edit-mode frame, `EditLoad`, GameMode 8):
1. `request[0..23] = 0`; the placed slots (0..127 in slot order, `partNo >= 0`, type `+0xf0` in 0..23) are grouped by type into `L[type][0..63]`; `L(p)` below = `L[p][0]` = **lowest slot** of that type (null if none).
2. call `Norn/Matatagi/Queens/Muska/YellowRequest` according to `CEditGround+0` (the map number); each writes `EditPartsInfo.total_requests` and sets `request[i] = 1` for the satisfied wishes.
3. `for i in 0..23: if !CheckComplete(i) request[i] = 0` - a wish only counts when that house is complete (all tips installed).
The value is consumed by `_SCHECK_REQUEST(part, &out)` (0x190770: `request[part]`; the talk scripts choose "thanks" vs "I wish ..." texts: e.g. e01 fn 256 `_SCHECK_REQUEST(v8)`; the wording of each wish is in `<map>talk_<lang>.mes`), `_SGET_REQUEST(town1, &pct)` (0x195890: percent), the Analysis bar, the edit-exit event 0x83 and, for Matataki, event 0xb.
Request index == house type for houses (request i belongs to part i) except the extra Matataki indices 14 and 16 (14 = Wise Owl Shop part, 16 = river continuity, index not equal to a part with that wish).

### 6.2 Primitives (all cell coordinates are in the area of the part, `(cx,cz)` = `CEditArea::GetPos(part position)`) [R]
* `rect_part(p, m)` (`GetRectParts(rect, part, margin)` 0x1a62c0): `(cx-(w>>1)-m, cz-(h>>1)-m, w+2m, h+2m)` with the *rotated* `w,h`.
* `rect_cell(p, dx, dz)` (`GetRectParts(rect, part, dx, dz)` 0x1a6410): the single cell at footprint-local offset `(dx,dz)` of the **unrotated** footprint (`W,H` with rotation 0, offsets recentred by `W>>1,H>>1`), then rotated with the part's rotation (rot -1: `(-dz,dx)`, 2: `(-dx,-dz)`, 1: `(dz,-dx)`, 0: `(dx,dz)`) plus a parity correction `(ox,oz)` in {0,-1}; `dz = -1` / `dz = H` mean one row in front/behind.
* `rect_dir(p, d, k)` (`GetRectDirParts(rect, part, dir, depth)` 0x1a6750): strip of depth k next to side `(d + rot)` of the part (wrapped to -1..2: 0 = -z side, 2 = +z side, 1 = -x side, -1 = +x side), as wide/tall as the part.
* `has(rect, type, area)` (`CheckPartsRect` 0x1a61e0): some part of type `type` covers a cell of `rect` in area `area` (`GetPartsRect` 0x16fb50, no clipping, cells with partsID >= 0).
* `check_rot(a, b, k)` (0x1a7890): `wrap(rot(a)+k) == rot(b)`.
* `river_chain(area,x0,z0,x1,z1)` (`CheckRiverChain` 0x16fd00): 4-neighbour flood fill over cells of kind 2/3/5.
* "alt" = `GetAlt_i` > 0 (altitude steps of the part's cell).
Missing trailing arguments in the decompile are 0 (checked in the disassembly 0x1a7f40..).

### 6.3 The wishes (`total` = value of `total_requests`) [R, decompile + disassembly]
**Norune** (total 8, `NornRequest` 0x1a69c0): R0: house 0 has rot == 1. R1: house 1 inside rect (10,0,4,5). R2: house 2 **not** inside (2,8,9,4). R3: house 3 within 2 cells of the pond (type 12). R4: house 4 within 4 cells of house 6. R5: house 5 within the 4-deep strip at side 2 of the windmill (type 8) and `GameIntFlag(0) >= 100`. R6: house 6 **not** within 4 cells of house 1. R7: at least 2 parts of type < 7 within 4 cells of house 7 (itself excluded by type).
**Matataki** (total 10, needs areas 0,1,2; indices 0..7, 14, 16; `MatatagiRequest` 0x1a6d90): R16: river continuity `chain(a0:(5,0)->(2,7)) & chain(a1:(3,5)->(11,3)) & chain(a2:(4,0)->(3,7))`. R0: house 0 in rect (0,0,4,1) of area 0 or rect (0,5,1,1) of area 1. R1: >= 4 trees (type 15) within 1 cell of house 1. R2: a watermill (type 11, 12 or 13) within 3 cells of house 2. R3: Wise Owl Shop (14) within 4 cells of house 3. R4: house 4 in rect (0,5,7,3) of area 2. R5, R7: house 5 / 7 on raised ground. R6: house 6 within 3 cells of house 3. R14: >= 14 parts of type 16/17/11/12/13 (river, bridge, watermills) within 1 cell of the Wise Owl Shop.
**Queens** (total 10; `QueensRequest` 0x1a73f0): R0: house 0 in rect (0,0,1,7) of area 0 or (0,0,1,8) of area 2. R1: house 1 within 3 of the Fountain (10). R2: house 2 within 2 of part 8. R3: house 3 within 2 of house 1. R4: house 4 in another area than house 9 (or house 9 absent). R5: house 5 has rot 0. R6: house 6 rot -1 and within 3 of the Leaning Tower (11). R7: house 7 is in area 0. R8: a road (13) on `rect_cell(house 8, 0, 3)`. R9: house 9 within 1 of house 8.
**Muska Lacka** (total 9; `MuskaRequest` 0x1a7920): `front(p, t)` = `has(rect_dir(p, 2, 14), t, 0)` (a type-t part in the 14-deep strip in front of p). R0: house 0 in (1,0,6,3), totem 8 in front, `check_rot(h0, t8, 1)` and the completion event of part 0 already played (`GetCompEvent`). R1: totem 10 in front of house 1, `check_rot(h1,t10,1)`. R2: totem 9 / house 2, k 0. R3: house 3 within 2 of the lake (11), totem 9 in front, k 2. R4: totem 9 / house 4, k 1. R5: totem 10 / house 5, k 0. R6: totem 8 / house 6, k 0 and house 6 rot == 1. R7: totem 8 / house 7, k 2. R10: totem 10 inside rect (2,0,3,14) with rot 0.
**e05 "Yellow"** (total 13; `YellowRequest` 0x1a7f40; parts snap together; satisfied parts additionally show their `setuzoku` ("connection") frame when `request[i]` and y < 1.0, otherwise it is hidden): R_i with `check_rot(a, b, 0)` and cell tests: R0: rot(h0)==rot(p7) and p7 on `rect_cell(h0,0,2)` and `(1,2)`; R1: p5 on `rect_cell(h1,0,-1)` (rot equal to p5); R2: p6 on `rect_cell(h2,0,-1)`; R3: p7 on `rect_cell(h3,2,1)` but not `(2,0)`; R4: p7 on `rect_cell(h4,-1,1)` but not `(-1,0)`; R5: p5 on `rect_cell(p3,1,2)` with rot(p5)==rot(p3); R6: p6 on `rect_cell(p4,0,2)`; R7: part 7 placed; R8: p7 on `rect_cell(p8,0,-1)` and `(1,-1)`; R9: p9 on `rect_cell(p8,0,1)`; R10: p10 on `rect_cell(p8,1,1)`; R11: p9 on `rect_cell(p11,0,-1)` (rot(p11)==rot(p9)); R12: p10 on `rect_cell(p12,0,-1)` (rot(p12)==rot(p10)).
The Python version (`georama_requests.py`) is the executable form of this text.

### 6.4 Validation [VD]
Evaluating the developer layouts `gdata0.edt` with `georama_requests.py` (completion flags forced): **e02 holds all 10 wishes, e04 all 9, e05 all 13**, e03 9 of 10 (R0 only), e01 4 of 8 (R2,R4,R6,R7; the other wishes disagree with that snapshot, which is an older design - georama.md 5.1). Three complete, independent matches confirm the primitives (`rect_dir` orientation, `rect_cell` parity/rotation, first-instance rule).

---------------------------------------------------------------------------------------------------------------------------------
## 6b. Script interface (ext functions touching this system) [R]
| ext | address | behaviour |
|---|---|---|
| `_CHECK_COMPLETE_PARTS(p, &out)` | 0x190450 | `CheckComplete(p)` on the live `EditPartsInfo` |
| `_SGET_CMP_EVENT(p,&out)` / `_SSET_CMP_EVENT(p,v)` | 0x1906b0 / 0x190710 | bit 0 of `info[p].flags` |
| `_SCHECK_REQUEST(p,&out)` | 0x190770 | live `request[p]` (already filtered by completeness) |
| `_SGET_REQUEST(town1,&pct)` | 0x195890 | percent from the **saved** `GrdData` (non-zero requests of 24 * 100 / `GrdData[0]`, 100 when equal, 0 for an invalid town): it only reflects the state of the last `EditSave` / board save |
| `_SSET_REQUEST_EVENT_FLAG(town1,v)` | 0x1954d0 | `GrdData+0x64 = v` |
| `_SATRA_PARTS_GET(town1,p)` / `_SATRA_CHIP_GET(town1,chip)` / `_SGET_ATRA_PARTS_NUM(town1,&n)` | 0x1957d0 / 0x195830 / 0x195990 | 2.3; n = number of obtained parts |
| `_GET_NPC_PARTS_NO(v,&p)` | 0x18e9f0 | `VILLAGER_INFO[v].+0x48` (2.7) |
| `_SGET_NPC_TALK_MES(npc,&v)` / `_SSET_NPC_TALK_MES(npc,v)` | 0x18f890 / 0x18f910 | `GrdNPCData(NowEditMap,npc)+8` |
| `_GET_EDIT_PARTS_POS(p,&x,&y,&z[,&rx,&ry,&rz])` | 0x1904c0 | position (and with 7 args rotation) of the first placed part of type p (0 if none) |
| `_HOBJ_EDITPARTS(handle,index,"frame")` | 0x1909a0 | binds an object handle to a frame of `ObjParts[index]` (placed-part scenes) |
| `_DELETE_ROBO_PARTS()` | 0x190870 | `EdDeleteE05RoboParts` 0x17ecf0: town 4 only: for the first 14 part records `obtained = placed = max = 0`, tray cleared, placed parts of town 4 removed from the save (used by the story scene `s13/in/s2201`) |
Talk scripts (function 256, e.g. e01 0x83d4) use message ids `base + k` of `<map>talk_<lang>.mes` with `base = villager*20 + 20` (+1 first chat, +2 / +4 line without / with the wish fulfilled (`_SCHECK_REQUEST`), +3 / +5 line for an incomplete / complete house (via the script helper `fn_2018` = completion-event test), +7..+11 random chat (`rand % 5`), +12 after `GameIntFlag(0) >= 100`; exact roles [I]), selecting by `_SCHECK_REQUEST(part)`, `_CHECK_COMPLETE_PARTS(part)` and `_SGET_CMP_EVENT(part)`; the villager -> part map used by the script (`v8`) equals `EditCharaData`.

---------------------------------------------------------------------------------------------------------------------------------
## 7. Initial state of a new game's Georama
* Save: all zero (1.1). `EditInit` 0x178380 (not the debug path): `CEditPartsInfo::Initialize(t)` (LoadGroundData) then `EditLoad()` -> empty placement list -> `CEditGround` has **no placed parts**; `RemakeGrid`. The debug path (`old_main_mode == 7 && padrup == 0`) instead marks the first 22 part records obtained and every element installed and loads `gdata0.edt` (complete developer town).
* Visible in e01 at the start: only the static scenery of `mapinfo.cfg` (GROUND `e01g02..g06`, BUILD `e01h06` at (0,47,793) and `e01a02` at (0,0,600), the water sheet), not placeable; **no rubble objects exist as placeable parts** - what looks like ruins is part of the static ground models.
* Available parts at the start: **none**. Everything comes from the Atla of the dungeon of the same index (2.4): the first dungeon floors contain a guaranteed first house + its first tips, trees, road and river. A house needs its tips installed on the Atla board (Build menu) before it is complete; person tips (the villager's chip) make the villager appear in town.
* Start-up timing: `GameMode 0 -> 1`, fade-in, `RunEvent(StartEventNo or 0x80)`; the first-visit scripts use the map flags of georama.md 6.1.

---------------------------------------------------------------------------------------------------------------------------------
## 8. Port notes (C++)
* Needed data at run time: `EditPartsData`, `EditElementData`, `EditCharaData`, `AtraAppearData` (all extractable from the ELF with the offsets above), `atrames.bin` for names, `mapinfo.cfg`/PTS for footprints. `georama_edit_tbl.py --json` produces the tables.
* Minimal object model: `PartInfo[24]` (1.2) per town inside `SaveData`, placement list of 128 slots, `EditArea[4]` grids with `{partNo, alt, partID, code, kind}`; implement `check_parts`, `set_map_parts`, `delete_map_parts`, `set_road/river_parts`, `request_check`, `check_complete`, `object_on_off`, the Atla board rules (2.6) and the loop of section 4 with the constants given.
* Order of operations that matters: obtained/installed/placed/max are independent; `complete` ignores them; requests are zeroed for incomplete parts; `EditPartsObjectOnOff -> RequestCheck` runs every edit frame; exit-event checks run once when leaving edit mode.

---------------------------------------------------------------------------------------------------------------------------------
## 9. Unresolved / open
1. Names of the Muska Lacka (town 3) and e05 (town 4) parts/tips: `atrames.bin` (all languages) only has ids up to 1514 (towns 0..2). The messages for 1600+ must be in another bank [?].
2. Exact meaning of `EditPartsData.w2` (`info+0x10`) and `EditElementData.+8` / tile rows 0..39 (part plates) [?]; `GrdNPCData+4`.
3. Which scripts start the completion events with `w4 == 1` for each house (talk function 256 / entry 128): only the Norune Gaffer case was traced; 208..211 of e04 have `w4 == 0`.
4. `ECursorFrame` model, the colours of the invalid ghost tint (`__1159` constants at 0x26ae80), `CPartsCursor` frame order [?].
5. e05 tip 15 (third element of part 7) is never dropped by the dungeon lists and no `event.stb` grants it [?]. `GameIntFlag(0)` (Norune R5) meaning [?]; `GrdData` bit/flag use of `DAT_01d19be0`/status 5 is dead except town 5.
6. Whether the shared-frame assumption of 2.5 (toggle on the template) is exactly how copies are drawn [I].
7. Which villager choice (#3) sets `GrdNPCData.flags |= 2` in each town's talk menu (the code path is `TalkMode` with `DAT_01d3d478 == 0`, `talk_select == 3`).

## 10. Corrections / answers to open points of the other notes
* `notes/events.md` row 131 (0x83): the condition is **all requests of the town satisfied** (number of non-zero `request[i]` == `GrdData[0]`) and the request-event flag still 0 - not "every part group placed". Row 200..223: see section 5 (walk-in for `w4==2`, scripts for `w4==1`; the Atla-menu status-5 path is dead for towns 0..4).
* `notes/georama.md` 8.1 (element ids, `+0x14`, `+0x1C`): the PTS `+0x14/+0x1c` and element ids are not used at run time; the real data are `EditPartsData` w4 (completion kind) and the element blocks (2.1). 8.3 (new game): 7. 6.1 step 7: `EditLoad` = `CEditPartsInfo::Load` + `CEditGround::Load(int, CSaveData*)` (1.1).
