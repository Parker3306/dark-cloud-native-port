# Event / cutscene script runtime (town, interiors, story maps, dungeon events)

Status: read from `work/decomp/all_dun.c` (main ELF + dungeon overlay) and checked against the real `.stb` files, with a Python re-execution of the VM (`tools/dc/stbrun.py`) for call sequences.
Nothing here has been compared with the running game yet (no PCSX2 session was used). "VERIFIED-DATA" = checked against disc files, "READ" = read from the decompile, "INFERRED" = deduced, "UNKNOWN" = not resolved (section 9).

Companion files: `notes/event_ext.csv` (one row per external function, 373 rows), tools `tools/dc/event_usage.py` (usage matrix, correct ext names), `tools/dc/stbtext.py` (pseudo-code listing of any event `.stb`), `tools/dc/stb_closure.py` (ext functions needed by a set of event functions), `tools/dc/stbrun.py` (headless run of one function with a logging null host).
`tools/dc/stbdis.py` labels ids with the MONSTER table first, so for event scripts every id below 256 (e.g. 10, 11, 12, 33, 35, 133 ...) is mislabelled there; use the tools above or `event_usage.py --dis <path> [fid]`.

## 1. The three layers

```
main loop (mode)  0 title | 2 town/village (EditLoop) | 3 dungeon (GameLoop) | 5 opening | 7 menu ...   (MapJump(n) picks the mode: n<200 town, 200..299 dungeon n-200, 400 opening, 800+ title/menus)
 town mode:  GameMode 1 walk | 2 talk | 3 clock change | 4 Georama edit | 0xe EVENT | 0x10 fishing | 0xb/0xc entering / inside a building (EditInLoop has its own copy of the loop)
 EVENT = one CRunScript (VM, 0x1D4A430) running a function of the map's event.stb, resumed once per frame by EdEventMode()
```
The same Ed* event machinery (EdSetEventScript / EdEventInit / EdEventMode / all `_NAME` ext functions) is reused by the dungeon ("system script": `dun/script/dNN/event.stb`, gameTask 400/0x191 in `MoveChara`), with `NPCUnit` (6 slots) and `CharaMain` instead of the village's `EdVillager` (10 slots) and `MainChara`.

## 2. VM and ext-call contract (READ)

* A `.stb` is the bytecode already ported in `dccore/script.*`. One `yield` op = "come back next frame" (60 per second). The host calls `run(fid)` once and `resume()` once per frame. `run` returns -1 for an unknown function id; the game treats "-1 / finished at once" as "event did not start" (`EdEventInit` returns 0). If the script yields, `EdEventInit` returns 1 and the map goes into event mode (town GameMode 0xe, interior GameMode 4).
* Ext function C signature: `int f(RS_STACKDATA* args, long nargs)`; `args` = first argument slot (the function id slot is not included), `nargs` = number of real arguments. The VM pops the arguments and **pushes nothing**; results come back only through pointer arguments (`&v` : slot type 3) via `SetStack(args+8*i, value)`, which writes the 32-bit value only and keeps the slot's own type. The return value only matters for a diagnostic printf when it is 0 (a function that returns "false" because an argument is missing or an index is bad). **No ext function blocks.** All waiting is done in script code by polling (`_LOAD_SYNC(&v)`, `_LOAD_SND_SYNC`, `_CHECK_FADE`, `_ASQ_CHECK`, `_CHECK_MES*`, `_GET_PADDOWN`) inside `while (...) { yield; }` loops. Modal UI (item menu, name entry, world map, fishing menus) is requested by setting `menu_mode`/`DAT_01d567d8`; the engine then simply does not call `resume()` until the modal screen is finished.
* Argument access: `GetStackInt` (float slots are truncated toward zero, otherwise the raw word), `GetStackFloat` (int slots converted, float slots raw), `GetStackString` (the slot value is the address of a NUL-terminated string in the loaded program; in dccore `ScriptArgs::get_string`). Variable arity is common: the function looks at `nargs` (2- vs 3-argument forms are listed in the CSV).
* Position helpers: `GetPosition(args)` reads 3 float args x,y,z and applies the event world matrix (see `_SET_WORLD_COORD`: p_world = RotY(ry) * p_local + (x,y,z)); `SetPosition` / `SetRotation` write results back inverse-transformed. `GetWorldRot` adds `ry` to a yaw. All angles are radians; yaw is measured with `atan2(dx, dz)` (0 = +Z, positive towards +X), the same convention as `MoveChara`.
* Ext tables: `EdSetEventScript` clears `ext_func[0..0x5db]` and installs table EVENT (0x269C50, 306 entries, ids 0..1000); the dungeon calls `AddSystemEventScript` afterwards, which adds table DUNGEON (0x26B310, 67 entries, ids 1010..1101, dungeon only). Monster scripts use a third, separate table (0x2918A0, ids 10..253; `BtSetEventScript`, 256 entries). A duplicate id aborts the game at load. Names and counts are in `event_ext.csv`.
* Button bits seen by scripts: `_GET_PADON/PADDOWN/PADUP` return the CGamePad mask with the OK and cancel bits **swapped** (`exch_ok_cancel`): script bit 0x20 = OK (engine 0x40 / cross), 0x40 = cancel, 0x1000 up, 0x4000 down, 0x2000 right, 0x8000 left, 0x10 triangle, 0x80 square, 0x800 start, 0x100 select (INFERRED from the engine's own uses; EdTalkMode tests 0x40 for confirm, 0x20 for cancel, 0x1000/0x4000 for the menu cursor).


Notation in `event_ext.csv` (`args` column): names without decoration are ints read with `GetStackInt` (slot numbers `chara`/`slot`/`win`/`handle`/`chan`, ids, flags, frame counts); `x,y,z`, `rx,ry,rz`, `sx,sy,sz`, names ending in `_f`, `speed`, `angle`, `yaw`, `f` are floats read with `GetStackFloat` (a script int literal is converted); `"text"` = string; `&name` = output pointer slot written with `SetStack`; `[...]` optional trailing arguments (the function branches on `nargs`); `...` = repeated. "Needs >= N args" in the semantics means the function returns 0 and does nothing when called with fewer. `state_touched` names the engine global/object (`DAT_xxxxxxxx` = game address) or save structure the call reads/writes.

### 2.1 Script function ids (which function of event.stb is run, and by whom)

| id | started by | notes |
|---|---|---|
| 128 (0x80) | town `EditInit` tail: `RunEvent(StartEventNo >= 0 ? StartEventNo : 0x80)`; interior `EditInInit`: `start_event_no = EdInteriorStartEvent` (default 0x80) | **map entry script**, present in almost every map. Found in the machine code (`EditInit` is truncated in the decompile after the `SetBuff` call; tail disassembled at 0x1796C0..0x1797B0). |
| StartEventNo | `_MAP_JUMP(map, event)` second arg -> `MapJump(map-1, event)`; `_GOTO_INTERIOR(parts, "name", event)` third arg | values found in the scripts: 300+k (k = 0..5; story scene on entering a town/sub-map, e.g. returning from a dungeon: d01 `_MAP_JUMP(1, 300)`), 200 (`_GOTO_INTERIOR(-1,"i03h09",200)`), 150 (`s13`: `_GOTO_INTERIOR(-1,"s2201",150)`), -1/absent = 128. |
| 150 | `start_event_no = 0x96` three frames after the raw button 0x80 (square, `CGamePad::Down2`) is pressed on the walking screen (`event_next_1100`), after `EdEventAllClear`; also reachable from the debug main menu (`MapJump(map_988[i], event_989[i])`) | present in 100+ scripts; in the s77..s99 maps it is the ONLY function and no script jumps to those maps, so they are test/gallery cutscenes. Which retail path starts the first Norune scene after the opening is UNKNOWN (e01 function 128 with new-game flags runs only the 3 trivial helpers; candidates: a flag-conditioned branch in 128/300, or the square hot key). |
| 256 (0x100) | `EdMoveChara`: OK pressed next to an NPC (`EdSearchNearNPC` + `EdTalkModeInit`) | NPC talk selector: reads `_GET_TALKNPC_INFO_ID`, sets `_SET_TALK_MES(...)` / `_SET_TALK_SELECT_MES(...)` and ends with `_SET_RETURN_CODE(9)`, **never yields** (finishes in one call). |
| NPC auto-talk | villager info field `+0x60 > 0` and `+0x64 != 0` (position-based villagers) | `RunEvent(info.+0x60)` |
| 11, 12, 13 | event point type 1 (door) whose event-param `+0x24 > 0` -> `EdInitGotoInterior` returns that id | "enter building with a scene" events. Their last act is `_GOTO_INTERIOR(...)` (return code 4). |
| 130 (0x82) | event-point type 1 whose name matches a special string | |
| 131 (0x83) | `FadeOutToEvent(0x83, 9)` when every Georama part group of the map is placed (`GetGrdData.+0x64 == 0`) | |
| 132 (0x84) | `change_time_event` (clock crossed a day-period boundary in towns 0..3) | |
| 133 / 134 | fishing: cancel / square pressed during the cast phase | |
| 11 (0xb) | `FadeOutToEvent(0xb, 10)` after leaving Georama edit mode in map 1 while game flag 0x14 is clear | also started by door event points (see 11,12,13 above) |
| 200 .. 223 | `FadeOutToEvent(partsNo + 200)` when a Georama part group becomes complete (`CheckComplete`, also checked while walking) and Georama menu status 5 | per-building completion scenes |
| 300 .. | event points (type 3, id `+0x1c > 0`) and StartEventNo | story events |
| system 1, 2 | `RunSystemEvent(1)` ladder (`EdInitHashigo` fills `_GET_V_ARG/_GET_I_ARG/_GET_F_ARG`), `RunSystemEvent(2)` world-map jump (`DAT_01d3d5d0` = chosen map) | in `gedit/system/event.stb` (ids 1 and 2 only) |
| dungeon 100 | floor start (`gameTask 400` with `DAT_01d5676c = 100` right after the floor was generated) | chooses map, doors, limit zone, calls `_LOAD_MONSTOR`, ends with `_GO_DUNGEON` |
| dungeon 90 | called by floor-start flow for pre-built floors | `_LOAD_DUNGEON_MAP2; _SET_RANDOM_MAP; _LOAD_MONSTOR; place player at "in_door"; _GO_DUNGEON` |
| dungeon 5, 15, 16 | 5 = player died (game over scene), 15/16 = item-box trap events (`iventActive`, kinds 5 -> 16 else 15) | |
| dungeon 160, 161 | CDungeonEventMan marker ids (cfg `PT_MARKER`): exit stairs / gate key door | OK on marker -> `_GET_ACTION_MODE`=1; item use -> mode 2 |
| dungeon 200/201 | back-floor entrances (`ura_*` markers) | |
| dungeon 250/251, 350/351, 375 | locked doors / character rooms / special | |
| dungeon 300..302, 102, 111, 520, 800 | `inter` / special maps' markers | |
| dungeon 400..405, 500 | boss-floor markers (boss keys, boss door, boss start `event01_2` = 500) | |
| dungeon 510 | boss defeated: boss monster script calls monster-ext 111 `_RUN_SCRIPT(510)` (sets `NowMonstorUnit+0x94`; `MoveChara` then fades out and runs it) | |
| dungeon `_CLEAN_MONSTOR_SCRIPT_NO(fid)` | when the floor has no live monster left | |
| dungeon `_RUN_SCRIPT_NO(fid [,arg])` | request 5: run another function of the same event.stb after the current one finishes | |

Per-map function id sets are in Appendix A (generate with `event_usage.py`); e.g. `gedit/e01/event.stb` = {1,11,102,128,130..134,150,200..208,256,300}; interiors = {128,150,200,256,(300)}; `s*` story maps = {150,300+}.

## 3. Town / interior runtime (READ)

### 3.1 Map load (`EditInit`, 0x178380)
Map names: `MapNo` 0..8 -> `e0N` (N = MapNo+1), 11..19 -> `s0K`, 20+ -> `sKK` (K = MapNo-10). Data dir `gedit/<map>/`: `event.stb` (this document), `<map>_<lang>.mes` (event message bank, language suffix `_1` English, `_3` French, `_4` German, `_5` Italian, `_6` Spanish), `<map>talk_<lang>.mes` (NPC talk bank, towns only), `mapinfo.cfg`, `scene.scn`, `fconv.bin`, `gdata0.edt`, `chara/*.chr` (packs addressed by the scripts), `in/<interior>/` (interiors with their own event.stb and messages). `gedit/system/event.stb` is loaded as `EdSystemEventData` (system events 1 and 2). `LoadScript` loads `event.stb` + `<map>_<lang>.mes`; every stb embeds a fallback message bank at the file offset stored in header word +0x14 (e.g. e01: 39-short table, 6,916 bytes of text); that embedded bank is used only when the language `.mes` file could not be loaded (`param_2 == 0`), both end in `SetBuff(EditEventMes1, bank)`. The talk bank is loaded in `EditInit` into `EditMes1` (fallbacks: map-less file, then `gedit/e01/fconv.bin`).
At the end of `EditInit`: `RunEvent(StartEventNo or 0x80)`; the event then starts in `EditLoop` (it reads `start_event_no`).

### 3.2 Starting an event
`RunEvent(id, camera)` only stores `start_event_no = id` (refused when another is pending, except `simple_event`) and copies the current camera into the event camera (`0x1D35140`, FollowOff, roll 0). Each `EditLoop` frame, `if (EdDebugEventEnable && (start_event_no > 0 || start_system_event > 0))`: `EdEventInit(id, buffer, EdEventData|EdSystemEventData)`:
1. resets all 8 message windows (default sizes 0x46 x 10 cells, `InitMesWinTbl`, name slots -1, value slots 0, cursor -1, colour/auto-page defaults),
2. `CRunScript::reload(program)`, clears `DAT_01d3d4a8` (exclamation), `DAT_01d3d214` (post-event camera mode), `DAT_01d3d4a4 = -1`, colour filter/bg/light flags, `DAT_01d3d618 = 0` (result code), `DAT_01d3d200 = DAT_01d3d204 = 1` (BGM stop/play on map jump); saves the current point-light matrices and ambient so they can be restored (`_LOAD_LIGHT -1`),
3. `EdInitEventParamSimple` (NPC flag arrays: draw/shadow/footstep = 1, collision/stop/before = 0 ; 10 ASQ channels re-initialised on a pool of 0x10a nodes; sprite tables; object handles; `ClearObjAnime(-1)`; `CCharacter::Initialize` of the scratch item character),
4. `RunEvent(program, id)` = `CRunScript::run(id)`; returns 1 if it yielded (event mode entered) else 0 (script missing or finished immediately; if the finished script set result code 9 the loop still goes to talk mode).
GameMode becomes 0xe (town) / 4 (interior) and `EdInitMesParam` re-creates the message windows.

### 3.3 Per-frame event step (`EventMode` -> `EdEventMode`, 0x197AD0)
Order inside one frame while `event_pause == 0`:
1. motion/animation freeze flags (`motion_stop_flag`), `ReadBG()` (background file loader pump: this is what makes `_LOAD_SYNC` become 0),
2. `Step()` of every non-null message window (`DAT_01d3d3d0[0..7]`),
3. if `menu_mode == 0`: `EdResumeEvent()` (VM resume until the next `yield`/end); then if the script set `menu_mode` (!= 0) the modal screen is started (`EdInitMenu`) and `motion_stop_flag = 1`. Otherwise (menu active) the menu runs and on completion writes its result through the pointer the script gave (`p_use_item` for menu_mode 5 and 9, `p_jump_map_no` for 7, `MenuMapJumpMode + 1`) and `menu_mode = 0`. menu_mode values: 5 use-item list, 6 name registration, 7 world map, 8 fishing-point change, 9 change bait, 10 fish ranking.
4. `Play()` of the 10 ASQ channels, `ObjAnimePlay()` of the 16 object animations,
5. if `result code (DAT_01d3d618) > 0`: `EdEventFinish` (clears light/bg overrides, `DeleteReference` of hero frames) and pad auto-repeat off,
6. projection (`DAT_01d3d20c`), background colour,
7. camera targets: `follow_chara` (SetFollow to hero/NPC position + k*height), `sync_camera_ref_chara` (+offset +world matrix), `sync_camera_ref_obj`, `sync_camera_pos_obj`,
8. returns: `DAT_01d3d618` (the result code) if set, else 1 when the VM finished by itself (CRunScript+0x34 != 0), else 0.
Then `EventMode` maps the code: **default (1,2,3..)**: GameMode 1 (walk); if `DAT_01d3d214 > 0` re-enable the follow camera behind the hero (yaw + `DAT_01d3d218`); if `< 0` convert the final scripted camera into follow parameters; if `DAT_01d3d610 >= 0` `FadeOutToEvent(id)` (chain to another event); **4** `GotoInterior(name, parts, ..., start event)`; **5** `GotoDungeon` (`goto_dungeon = 1` -> `MapJump(NowEditMap + 200)`); **8**/**10** leave the loop (map jump / quit); **9** GameMode 2 (talk); **0xb** GameMode 0x10 (fishing). Result codes are set with `_SET_RETURN_CODE` (3), `_GOTO_INTERIOR` (4), `_GOTO_OUTSIDE` (7), `_MAP_JUMP` (8), `_GOTO_FISHING` (0xb). `EdEventNPCStep` (after the script) steps the hero (unless `DAT_01d3d23c` stop flag) and every NPC that is not stopped: `CNPCharacter::Step`, which plays its ASQ-driven motion, cloth and ambient fade, then `CNPCharacter::ClothStep`.
Skip: `_SKIP(1)` marks the section after it skippable; while the event is paused (`event_pause`) pressing confirm calls `EdEventSkip` = `CRunScript::skip()` (the VM runs to the next `endskip` op ignoring ext calls and yields) and cancel resumes. Who sets `event_pause` is UNKNOWN (only `EdEventPause` toggles it and nothing calls it except the paused branch itself; probably the start-button handler in MainDraw through an alias).

### 3.4 Walking and the triggers that start events (`EdMoveChara`, 0x16A160)
Every walk frame (after movement/collision):
* `EdSearchNearNPC(hero, EdVillager[10])`: nearest NPC with `info >= 0`, active flags (`+0x146c`, `+0x1470`), |dy| <= hero height, horizontal distance <= `k * npc.+0xb0 + 5` (k = `DAT_002a1a48`), and (the NPC faces the hero within the tolerance `DAT_002a1a4c` (AngleCmp) OR the NPC's info flag `+0x6c` OR its villager flag `+0x147c`); of all candidates the nearest wins. Sets NPC `+0x1468 = 1` (cursor/"!" over the NPC).
* `EdGetEvent(event points of the map, hero pos, hero yaw)` picks the nearest `ED_EVENT_POINT` (0x90 bytes each, from `mapinfo.cfg`) whose trigger radius (`+0x60`) contains the hero. Fields used: `+0x10` type, `+0x0c` attached Georama part or -1, `+0x50` position, `+0x70` facing, `+0x1c` event id, `+0x20` item, `+0x24` event-param id, `+0x30` name. Types: 1 door/building entrance (needs facing within `DAT_002a19f0`; OK -> `GotoInterior(name, ...)` or, if `+0x24 > 0`, `RunEvent(+0x24)`), 2 treasure/pick-up spot (OK -> `EdGetItem(item +0x1c, count +0x20)` + item-get message + chest open effect, sets map flag `+8`), 3 auto event (stepping in runs event `+0x1c`), 4 / 5 ladder down / up (OK -> `RunSystemEvent(1)` with arguments set by `EdInitHashigo`).
* Auto-talk NPCs, OK on an NPC -> `RunEvent(0x100)`, OK on an "event poly" (collision attribute under the hero: `Chara+0x1220`) -> `RunEvent(attribute)`.
Key points for a port: the choice between events is by priority in this order: event point (types above) > auto-talk NPC > OK-talk NPC > event poly.

### 3.5 Talk mode (GameMode 2, `TalkMode`/`EdTalkMode`, 0x199090)
State set by `EdTalkModeInit(npc, info)`: `talk_chara_info_id` (VILLAGER_INFO index), `DAT_01d3d430` (NPC slot), `talk_mode = 0`, `TalkMesMake = 1`, `talk_camera = rand()%3` if the NPC is a "villager" (`+0x147c`), `talk_select = 0`, `DAT_01d3d478 = -1` (reset by the script).
Script function 256 runs first (see table) and fills the list `DAT_01d3d434[]` (`_SET_TALK_MES`) and `DAT_01d3d478/474` (`_SET_TALK_SELECT_MES(kind, header)`), then returns code 9 -> GameMode 2. Per frame `EdTalkMode`:
* hero and NPC turn to face each other (`AngleInterpolate` by `DAT_002a1870`; interior NPCs flagged `+0x68` keep their pose); camera: ref = midpoint(hero, NPC) + 15 up; position = ref + RotY(angle) * `vv_2757[talk_camera]`,
* if `DAT_01d3d478 < 0`: `talk_mode = 1` immediately and the window shows message `list[0]`; else `talk_mode = 0`: the window shows the header message `DAT_01d3d474`, whose text LINES are the choices; the cursor line = `talk_select` (written to the window cursor field `+0x1740`); up (0x1000) / down (0x4000) move it with wrap, confirm when the window `State()==3` (message fully shown) starts `talk_mode 1` with message `list[talk_select]` (confirm while the text is still being drawn only sets the reveal speed to 0 = show everything); cancel closes (return 1),
* `talk_mode 1`: confirm or cancel (mask 0x60): `State()==3` closes the window and ends the talk (returns 1); `State()==5` (page break) calls `GoNextPage`; otherwise (still printing) the reveal speed is set to 0. NPC motion 3 (talk gesture) while talking, motion 0 while choosing.
* kinds (`DAT_01d3d478`): 0 plain choice list (selecting entry 3 marks the NPC's `GrdNPCData.+0` bit 1), 1 / 2 / 3 / 4 shop-style lists: the chosen entry returns 2 (opens menu `goto_menu = 3`), 3 (`goto_menu = 4`) or 4 (`RunEvent(list[0])`) after `EdSetShopNo(list[...])`; exact shop semantics are in `EdInitMenu` (UNKNOWN beyond the code numbers).
* window placement: `AutoSetSub(window, A, B)` with A,B = hero/NPC (speaker first when the NPC speaks) -> screen positions via `GetScrPosFromChar`; talk_mode 0 uses tail type 0 / position mode 8 only when the caller asked, talk_mode 1 uses tail type 1 (speech tail at the NPC) and position mode 0 (auto).
Example (VERIFIED-DATA): for Norune info id 0 function 256 yields `_SET_TALK_MES(20)`; the talk bank `e01talk_1.mes` holds NPC k's lines in ids `20*(k+1) .. 20*(k+1)+19` (e.g. 60..72 for k=1) and the 4-line menu headers 10..13 (text `Hello.\nHow should I rebuild Norune?\n...`, each line preceded by two spaces for the cursor).

### 3.6 Leaving / entering
`_MAP_JUMP(n[, ev])` (code 8) selects `MapJump(n-1, ev)`: n 1..5 towns, `n+11` sub-map `s<n>`, `n+200` dungeon n (mode 3, `main_select_menu_no`), 800/801 title/menu modes. `_GOTO_INTERIOR(parts, "i01h01"[, ev])` (code 4): fade out 100 frames, load `gedit/<map>/in/<name>/` (map data `mapinfo.cfg`... by the interior loop `EditInLoop`), `_GOTO_OUTSIDE(ev)` (code 7) returns to the town and runs `ev` (-1 none). `_NAME_REGISTRY(chara)` opens the name-entry screen (modal).

### 3.7 Time of day
`NowTime` 0..12 (SaveData time); `EdGetTime()` buckets 0..3 select light/ambient sets and villager schedules; `_GET_CLOCK/_SET_CLOCK` use 24 h floats converted by `ConvertTime/InvertTime`; the clock advances only in GameMode 1; crossing a bucket boundary in towns 0..3 triggers event 0x84 when idle (`change_time_event`).

## 4. Message windows (`ClsMes`, `.mes` banks)

### 4.1 Banks and windows
* A bank = one `.mes` image (or `meswin/_systeme.bin`, `meswin/sysarticle_N.mes`, `gedit/system/editsys_N.mes`, `opdat/optext_N.mes`, the bank embedded in an event.stb at header word +0x14). Layout (VERIFIED-DATA on 56 town files + system banks): `u16 n` = number of shorts of the id table + 1, `u16 text_bytes` = byte size of the text part, then `(n-1)/2` pairs `{u16 id, u16 offset}`; message text starts at short index `n + 1 + offset`; a message ends at code 0xFF01. The tail of every file after `text_bytes` is a constant 31,752-byte scratch block (all zero except two shorts `0x0030`, `0x3000`): the game keeps it as the per-window glyph-cell table (`ClsMes +0x17a8`); an English reimplementation can ignore it. Note the existing `MessageBank::parse` in `dccore/text.cpp` already reads `n`/offset the same way (`start = table + 1 + offset`).
* Windows: `GetMes(win)` = `DAT_01d3d3d0[win]` (win 0..7; a null pointer makes every `_MES_*`/`_SET_MES_*` call return 0 and do nothing). Town: win 0 = `EditMes1` (talk window, bank `<map>talk_N.mes`, driven by `EdTalkMode`, not by scripts), win 1 = `EditEventMes1` (bank `<map>_N.mes`, used by scripts: nearly all `_MES_MAKE(1, ...)`). Dungeon: win 1 = `BtEventMes0`, win 2 = `BtEventMes1` (bank embedded in `dNN/event.stb`). The system message bar (`EditSystemMes`: item got, skill got, bag up) is separate (`ItemGetMes`, `TecGetMes`, `MaxUpMes`, `ClearSystemMes`).
* One `ClsMes` object is 0x17C0+ bytes. Fields that matter (offsets, INFERRED from usage): `+0x00,+0x04` text origin; `+0x08,+0x0c` cell width/height in pixels (default 11 x 20; style 4/5: 13 x 20); `+0x18,+0x1c` widest line (cells) / line count of the current message; `+0x20` number of glyph entries; `+0x34` frame on/off (fade & balloon frame); `+0x38` text effect (0 steady, 1 small tremble, 4 strong tremble: set by code 0xF6xx); `+0x44,+0x48,+0x4c,+0x50` window rectangle (640x448 screen; default size min 202x138); `+0x54` position mode (0 automatic, 1..9 fixed cell of a 3x3 grid, 1 = top-left); `+0x58` tail type (0 none, 1 speech tail), `+0x5c,+0x60` speaker screen position, `+0x64,+0x68` tail anchor, `+0x6c,+0x70` tail widths, `+0x74..+0x88` tail polygon; `+0x8c` fade speed, `+0x90` openness 0..1, `+0x94` want-open flag, `+0x98` window flags; `+0xa4` current reveal speed (glyphs per tick, 0 = instant), `+0xa8` default speed (config text speed `DAT_002a186c` normal / `DAT_002a1890` fast); `+0xac` waiting-for-key flag, `+0xb0` auto-page flag, `+0xb4` float reveal accumulator, `+0xb8` glyphs revealed, `+0xbc` current page start, `+0xc0` resume index; `+0xc4` glyph table (up to 700 entries of 8 bytes: `s16 code, s16 x, s16 y, u8 colour, u8 wait`), `+0x16a4` entry count, `+0x16a8` default colour, `+0x16ac` current colour, `+0x16b0` pause counter, `+0x16b4/+0x16b8` auto-page timer/limit, `+0x16bc` id of the open message, `+0x16c0` balloon style, `+0x16c4` show next-page arrow, `+0x16e0..+0x1704` ten "name" argument slots, `+0x1708..` eight value slots, `+0x1728` the printed number, `+0x172c` plus sign flag, `+0x1730` show zero, `+0x1734` narrow digits, `+0x1738/+0x173c` space width / justify width, `+0x1740` choice cursor line (-1 none), `+0x1744` smoothed cursor y, `+0x17a0/+0x17a4` text bank / system bank pointers.

### 4.2 Script API and life cycle (READ)
```
_MES_MAKE(win, id [, number [, slot0, slot1, ...]])   open message `id` of the window's bank. 3rd arg -> +0x1728 (FBFF), arguments 4.. fill the name slots FBFE,FBFD,FBFC,FBFB,FBF2.. with SYSTEM-BANK message ids (item/place names); the camera view matrix is refreshed first.
_SET_MES_POS(win, n)        0 auto, 1..9 fixed grid cell; call BEFORE or AFTER _MES_MAKE (the helper fn(win,id,pos,x,y,..) used by the scripts does MAKE, POS, AUTOSET).
_SET_MES_AUTOSET(win, chA, chB)  or (win, xA,yA,xB,yB)   automatic placement between two characters (-1 hero) or two screen points; the tail points at A.
_SET_MES_SHIPPO(win, tail [, w1 [, w2]])  tail type (0 none, 1 speech tail), widths.
_SET_MES_FUKIDASHI(win, style)  1..5 balloon presets (see CSV), other = plain window.
_SET_MES_DRAWSPEED(win, f)   glyphs per tick (float!). 0 = whole page at once. `_MES_CLOSE` restores the default speed.
_SET_MES_OKURI(win, v) / _SET_MES_WIN_FLAG(win, v) / _SET_MES_CURSOR(win, line)
poll:  _CHECK_MES(win,&v) v=1 when closed (State 0) | _CHECK_MES_COMPLETE v=1 when all glyphs shown (State 3) | _CHECK_MES_WAIT v=1 at a page break (State 5)
_MES_NEXTPAGE(win)  = GoNextPage (resume after FF03)      _MES_CLOSE(win)  start closing (+0x94 = 0, id forgotten)
```
`ClsMes::State()` (READ): 0 closed (`+0x90 <= 0`), 1 opening, 4 closing, otherwise (fully open) 5 if `+0xac` (waiting for a key at FF03), 2 while `+0xb8 < +0x20` (still revealing), 3 when everything is revealed. Typical script loop (VERIFIED on e01 fn_4bd4/fn_4264 and the dungeon `fn_50ec`): MAKE; wait; `repeat { yield; CHECK_MES/CHECK_MES_COMPLETE/CHECK_MES_WAIT; if (pad & 0x20) { if waiting: NEXTPAGE; else if complete: CLOSE; break } }`.
Per tick `Step()` (called from `EdEventMode` for all windows, from `EdTalkMode` for win 0) fades `+0x90` by `+0x8c` toward open/closed and recomputes the tail polygon; `MyTextureMake()` advances the reveal: `+0xb4 += +0xa4`, then consumes glyph entries until `floor(+0xb4) == +0xb8`; special entries: LF/space advance the counter silently, PAGE (0xFF03) sets `+0xac = 1` (state 5), the per-glyph wait byte sets `+0x16b0` (frames of pause). With `+0xb0` (auto page) and timer `+0x16b4 >= +0x16b8` the page advances by itself.

### 4.3 Control codes (16-bit, VERIFIED against the English banks unless noted)
Frequencies are over the 56 English town banks + `_systeme.bin`, `sysarticle_1`, `system14_1`, `optext_1`.

| code | meaning | seen |
|---|---|---|
| FD21..FD3A / FD3B..FD54 / FD6F..FD78 | A-Z / a-z / 0-9 (glyph = "gaiji" cell, width table in `EditPartsData+0x491c`, proportional) | 82k |
| FD55 ' , FD57 " , FD58 ! , FD59 ? , FD6C `,` , FD6D `.` , FD6E / , FD5C + , FD5D - (the digit printer uses FD5C/FD5D for the signs) , FD61 ( , FD62 ) , FD6B : , FD60 % , FD5F (read as '&' by text.cpp; in `<day banner>` "Adventure Day N <FD5F> Afternoon" it is a separator, INFERRED) , FD5A FD5B FD5E (FD5B is a bullet/asterisk in e02talk; all three occur in creature gibberish in s03; unverified) | punctuation | |
| FD00..FD17, FD1F | pad-button / cursor icons ("Walking <FD00>", "use item with <FD09>") and `FD1F` (suffix after item names); other-language accented letters share this range | rare |
| FF00 | line feed (x = 0, y += line height) | 1855 |
| FF01 | end of message | 1977 |
| FF02 | space (width = cell width, or `+0x1738` fixed space width, or the justified width from F7xx) | 15.7k |
| FF03 | page break: reveal stops, State 5, `GoNextPage` continues | 503 |
| FAFA..FAFF | hero name 0..5 (Toan, Xiao, Goro, Ruby, Ungaga, Osmond slots) = the player-entered name from SaveData (`NameRegistTbl`, 11 codes) | 153 |
| FBFE, FBFD, FBFC, FBFB (and FBF2..FBED) | inline SYSTEM-bank message whose id was passed to `_MES_MAKE` as argument 4, 5, 6, 7 (8..13) | 50 |
| FBFF | the number in `+0x1728` printed with digit glyphs (plain decimal, '+'/'-' glyphs FD5C/FD5D available) | 17 |
| FBFA..FBF3 | value slot 0..7 (`+0x1708 + 4*k`), same formatting | 7 |
| FCxx | text colour: FC00 = window default colour, FC01..FCFE = clut index xx+1, FCFF = special clut 0xFF | 62 |
| F9xx | skip xx pixels horizontally (F908 used as indent) | 19 |
| FExx | pause xx ticks after the previous glyph (not present in English data) | 0 |
| F8xx | fixed space width xx; F7xx justify the line to (xx*4) pixels (spaces stretch, `CalcSpaceW`); F6xx text-shake style xx | 0 |
| FB00..FBF2 minus the above | direct system-bank message id | 0 |

Text layout is hand-wrapped in the data (LF every ~35 glyphs) so the host does not need word wrapping; width of a line = sum of glyph widths. `text.cpp` extension needed: LF/PAGE as separate tokens (currently both become '\n'), FCxx/F9xx/FExx/F8xx/F7xx/F6xx skipped, FBxx slots and FBFF number insertion, FD00..FD17 icons, all of FD21-FD78 mapped as above.

### 4.4 Choices (VERIFIED on e01 `fn_4264`, dungeon `fn_4bdc`)
No engine support beyond the cursor: the script makes a message whose text lines ARE the choices (each preceded by two spaces), loops `GET_PADDOWN` / `GET_APAD` itself (up 0x1000 / down 0x4000 / stick), keeps the index, calls `_SET_MES_CURSOR(win, index)` every change, and on OK (script bit 0x20) returns the index (`_SET_MES_CURSOR(win, -1)`, `_MES_CLOSE`). The engine draws the selected line highlighted and a 32x32 pointer sprite (texture `0x299b98`) left of it at `y = line_height * line` (smoothed). The talk-menu (3.5) uses the same cursor field. Yes/No questions use `SET_MES_DRAWSPEED(win, 0)` first so the question appears at once.

### 4.5 System messages
`_ITEM_GET_MES(item [,kind])` -> `ItemGetMes(item, kind, 0x28, 1)` -> `SetSystemMes(msg, ...)` with msg = 10 (item id <= 0x50), 20 (> 0x50), +10 more when > 0x90, 10 for weapons (> 0x100); slot FBFE receives the item NAME message id (`item+100`, for weapons `GetCommonItemInfo(item).+6 + 100`) and value slot FBFA the kind/count (-1 = none). `_SKILL_GET_MES(n)` (`TecGetMes`) and `_ADD_MAXITEM_MES(n)` (`MaxUpMes`) are the same kind of one-line bar messages from `sysarticle_N.mes`. `_INIT_SYS_MES` closes it. While a system message is open `run_event_check` blocks menus and new events. Item names come from the system bank: weapons `id-156+...` / other items `id+100` (see `text.cpp item_text_id_name`).

## 5. Scene, characters, camera, objects, ASQ (what the scripts drive)

### 5.1 Characters (hero = slot -1, NPC slots 0..N-1)
* Slots: `GetChara(-1)` = hero (`DAT_01d3d21c`: `MainChara` in the village, `CharaMain` in the dungeon), `GetChara(0..N-1)` = NPC array `DAT_01d3d228` (stride 0x14A0): town `EdVillager` N = 10 (these are also the talkable villagers), interiors the same, dungeon `NPCUnit` N = 6. Out-of-range -> the call returns 0 and does nothing. In scripts every "chara" argument means this slot number.
* `CNPCharacter` fields touched by scripts (READ): `+0xb0,+0xb4,+0xb8` body radius / height / depth (`_SET_NPC_BODY_SIZE`; height also positions speech balloons and the follow camera), `+0xbc` main `CFrame*`, `+0xc0` second frame (weapon/shadow), `+0xce0..+0xcec` ambient r,g,b and alpha (alpha 0..128 fades in/out by 1..N per `Step`, `+0x1484/+0x1488`), `+0xc60/+0xc64/+0xc68` motion request {speed (-1 = table speed), mode flags, motion number}, `+0xc70` motion state (3 = finished), `+0x11d0` standing on ground, `+0x1220` collision event id under the character (hero), `+0x1440/+0x1444` villager info ids, `+0x1468` "talkable cursor" flag, `+0x146c` active, `+0x1470` visible, `+0x1474` step/draw enable (0 = fades out), `+0x147c` villager flag (camera presets), `+0x148c` texture block.
* Per-event flag arrays set by `_NPC_DRAW`, `_NPC_COL`, `_NPC_DRAW_SHADOW`, `_SET_NPC_FOOT_SOUND`, `_NPC_STOP`, `_NPC_DRAW_BEFORE` (hero entry kept in a separate scalar, index -1): draw (default 1), collision with the hero (0), shadow (1), foot sound id (1; 2 enables foot-step sound), stop (0: 1 = EdEventNPCStep skips Step/Cloth for it), draw-before-BG (0). All are reset by `_INITIALIZE`.
* Loading (all asynchronous in the game, all synchronous-enough for a port): `_SET_CURRENT_DIR("gedit/e01/chara/")` sets the prefix of relative names; `_LOAD_CHR_FILE("a.chr", "b.chr", ...)` queues background reads of up to 16 `.chr` PACK files (see `dccore/pack.*`) into consecutive 64-byte aligned slots `chr_file[0..15]`; poll `_LOAD_SYNC(&v)` until 0 (script helper: `v=1; while(v){yield; _LOAD_SYNC(&v)}`). `_ACTIVE_FILE_BUFFER(file, buf)` selects which loaded pack (`get_pack_file`) and which bump allocator (0 event buffer, 1 extra buffer) the following `_LOAD_*` use. `_LOAD_CHARA(slot, "name.cfg")` fetches the pack entry `name.cfg` (a base.cfg-style description: `MODEL`, `IMG`, `KEY` lines, handled by `dccore/character.*`) plus the models/motions/images it names from the same pack, builds the CCharacter (vtbl +0xbc), sets texture block `DAT_01d3d3c4 + slot` (0x36+slot in town, 0x20+slot in the dungeon), ambient alpha 128, draw/shadow flags 1. `_LOAD_CHARA(-1, ...)` replaces the HERO model (the event hero is often a different pose model, e.g. `e25c01d.cfg`; the game later reloads the normal one with `_LOAD_MAIN_CHARA("chara/c01d.chr","info.cfg",0)`). `_LOAD_CHARA_TEXTURE`, `_LOAD_TEXTURE(k, entries..)`, `_LOAD_SPRITE_TEXTURE`, `_LOAD_BG_SPRITE_TEXTURE` load extra TIM2 entries from the active pack into texture blocks 0x2d+k (sprites 0x2d, background sprites 0x2e). `_DELETE_CHARA(slot)` / `_INIT_CHARA` free them. `_LOAD_VILLAGER`/`_LOAD_IN_VILLAGER(slot, info, "file" [,x,y,z,yaw])`/`_APPEAR_VILLAGER_*` place the schedule-driven villagers (VILLAGER_INFO table, 0x90 bytes each, derived from `mapinfo.cfg`, `EdSelectVillager` picks who is present for the current time).
* Moving them: `_SET_NPC_POS/ROT/SCALE`, `_SET_NPC_MOTION(slot, motion [, speed [, mode]])`, `_TURN_NPC`, `_TURN_CHARA` (turn_chara = face point at angular speed), `_SET_NPC_ANIME(slot, texanime, on)` (face/texture animation tables), `_INIT_NPC_CLOTH`/`_NPC_CLOTH_STEP` (cloth sim), `_SET_NPC_AMBIENT`, `_SET_NPC_PLIGHT`. Motion numbers are the `KEY` indices of the character's cfg. Mode flags (`+0xc64`, INFERRED from the motion code in notes/native-plan.md): bit 1 freeze, bit 2 hold last frame (play once), bit 4 restart/force even if the same motion; scripts use 0, 2, 4, 6. Speed argument <= 0 keeps the cfg speed.
* Most per-frame motion is NOT scripted frame-by-frame: scripts queue ASQ commands (5.4) and the engine moves and animates the character.

### 5.2 Scenes (animated "stage" models)
`_LOAD_SCENE(k, "name.cfg" [, speed])` creates a CCharacter in `SceneData[k]` (k = 0..2 seen; the decompiled `GetScene` ignores the stride) from a pack entry exactly like `_LOAD_CHARA`, but its camera pointer (`+0x310`) is set to a dummy camera. A scene is a model + motion in which named frames (objects such as `chara01a`, `chara02`, `cam02`, `cone2`) are animated and whose motion also contains the camera tracks (`.mot` track types 0x1e position, 0x1f target, 0x20 roll, 0x21 FOV from notes/native-plan.md). Scripts bind things to it:
* `_SYNC_SCENE_CAMERA(k)`: `FollowOff`, scene camera pointer := the event camera, so the scene's camera tracks drive the event camera; `_RELEASE_SCENE_CAMERA(k)` undoes it.
* `_SYNC_SCENE_CHARA(k, "frame", chara)`: `CFrame::SetReference(chara frame, scene frame "frame")` (and the second frame): the character's transform becomes relative to the animated scene node; `_RELEASE_SCENE_CHARA(chara)` removes the reference. `_SYNC_CHARA_CHARA(a, "frame", b)` does the same between two characters (b hangs on a's named frame, `_RELEASE_CHARA_CHARA(b)`); `_SYNC_CHARA_ITEM(chara, "frame", itemSlot)` hangs the hand-held item model (loaded by `_LOAD_ITEM_FILE/_LOAD_ITEM`: texture block 0x28, one item slot) on a frame; `_SET_SCENE_POS/ROT(k, ...)` place the whole stage.
* Typical script (dungeon 1 exit stairs, function 160): load `out01.chr`, `_LOAD_CHARA(1,"out01.cfg")` (a prop whose motion moves nodes), `_SYNC_CHARA_CHARA(1,"chara02",-1)` (hero rides node `chara02`), `_HOBJ_CHARA(h,1,"cam02")` + `_SYNC_CAMERA_POS_OBJ(h)` and `_SYNC_CAMERA_REF_OBJ(...)` (camera follows nodes), `_SET_NPC_MOTION(1, 0, 0.3, 2)` (play the prop's motion once), poll until done.
* Host needs: a per-slot skinned/animated model with a frame (joint) name -> world matrix query, parent-reference (attach to another model's joint), and camera tracks evaluated from the motion.

### 5.3 Camera
One event camera (`DAT_01d3d210` = `EventCamera` 0x1D35140 in the town, `SubCamera` in the dungeon) with two modes:
1. direct: `_SET_CAMERA_POS/REF(x,y,z)` store NextPos/NextRef; `_SET_CAMERA_SPEED(n)` (INTEGER argument) = glide factor (0 / 1 = jump: `_SET_CAMERA(pos, ref, speed)` with speed <= 1 snaps and runs one camera step) otherwise the camera moves a fraction toward Next* each tick (`CCamera::Step`, same smoothing as the gameplay camera: speed 8..14 in the dungeon).
2. follow: `_SET_FOLLOW_CAMERA(chara, height, dist, angle, speed)` / `(x,y,z, height, dist, angle, speed)` = `CCameraFollow` (ref point + height + distance + angle with the same easing); `_ADD_CAMERA_ANGLE/HEIGHT/DIST`, `_CAMERA_STEP` (re-aim at `follow_chara`).
Helpers: `_SYNC_CAMERA_REF_CHARA(chara [,dx,dy,dz])`, `..._OBJ`, `_SYNC_CAMERA_POS_OBJ` (applied every frame at the end of `EdEventMode`), `_SET_CAMERA_ANGLE(yaw [,pitch])` / `_SET_CAMERA_ROTATE(yaw)` (re-aim / orbit), `_SET_CAMERA_ROLL`, `_SET_PROJECTION(f)` (FOV-like value, applied every frame while > 0), `_ITEM_GET_CAMERA(f)` (item-fanfare shot). Event end: `_RESET_CAMERA(mode)` / `_RESET_CAMERA_ANGLE(angle)` choose how the gameplay follow camera is rebuilt (mode > 0: behind the hero; < 0: keep the scripted position). All coordinates go through the `_SET_WORLD_COORD` matrix.

### 5.4 Action sequences (ASQ)
`_ASQ_INIT(ch)` / `_ASQ_SYNC_CHARA(ch, chara)`: 10 channels (`CActionSeq`, 0xD0 bytes), each bound to one character (the channel starts from that character's current position/yaw). The `_ASQ_*` calls append nodes (shared pool of 0x10A nodes of 0x20 bytes) to three singly-linked queues per channel; `CActionSeq::Play` (once per frame inside `EdEventMode`) consumes them (READ):
* MOVE queue: `SET_POS` (type 1, instant), `MOVE(x,y,z,ticks)` (2, linear interpolation over `ticks`; `MOVE(ch,ticks)` re-uses the last target), `MOVE_STEP(x,y,z,speed)` (ticks = ceil(dist/speed)), `ROT_REF(x,y,z,speed)` (4: yaw eases toward the direction to the point), `ROT_MOVE(speed)` (5: yaw eases toward the current movement direction), `ROT_ANGLE(angle,speed)` (6: ease to an absolute yaw), `CLEAR_ROT` (7: rotation mode off), `WAIT_ROT` (8: queue blocks until the current rotation reached its target within `DAT_002a1894`), `SET_ROT` (10, instant yaw), `DELAY_ROT(n)` (0xb), `MOTION_TRG(n)` (0xe) and `ANIME_TRG(n)` (0x12) = "start the motion / texture-animation queue now". One MOVE node is processed per tick; instant nodes (set pos/rot, rotation modes, triggers) are consumed back-to-back in the same tick.
* MOTION queue: `MOTION_PLAY(ch, motion [,speed [,flags]])` kind 5 loop, `MOTION_STOP` kind 6 (play once, hold last frame: motion mode 2), `MOTION_NEXT` kind 7 (play once; this node leaves the queue when the character's motion with that number is within the blend tail of its end, so the next motion node starts seamlessly). A motion only starts after a MOTION_TRG reached it. Applying a node sets the character's motion request (`+0xc68 = motion`, `+0xc64 = (kind 6/7 ? 2 : 0) | (motion changed ? flags : 0)`, `+0xc60 = speed`).
* ANIME queue: `ANIME(ch, texanime, on=1, frames, off_after)` (0xf): turns a TEX_ANIME set (face expression) on/off immediately or for `frames` ticks.
* After each Play the character's position and yaw (wrapped to +-pi) are written with SetPosition/SetRotation.
* `_ASQ_CHECK(ch, &v)`: v = 1 while anything is pending (`CheckEnd` = move queue empty AND no pending kind-7 motion at the head AND anime queue empty), else 0. Scripts wait with `while (v) { yield; _ASQ_CHECK(ch,&v) }` or with fixed tick counts (`fn_c34(n)` = n yields).
Everything here is deterministic integer/float arithmetic on the node data and can be ported 1:1 (about 250 lines); the host only receives `set_position(slot,xyz)`, `set_yaw(slot,a)`, `set_motion(slot,no,speed,flags)`, `set_texture_anime(slot,no,on)`.

### 5.5 Objects, sprites, effects
* Object handles: 32 slots (`ObjHandle`, 0x38 bytes: `+0` map-object, `+4` character, `+8..+0x34` up to 12 frame pointers) bound by `_HOBJ_FIXPARTS/EDITPARTS/INTERIORPARTS(handle, index, "frame")` (Georama part objects), `_HOBJ_CHARA(handle, chara, "frame")`, `_HOBJ_ITEM`, `_HOBJ_BT_HOBJ` (dungeon group). Operations: `_OBJ_DRAW(flag, handles..)`, `_SET/GET_OBJ_POS/ROT/SCALE`, `_SYNC_OBJ_OBJ`, `_RELEASE_OBJ_OBJ`, `_SET_OBJ_FOG`; 16 object animations (`_OBJ_ANIME_INIT`, `_OBJ_ANIME`) tween position/rotation of a handle. The dungeon has its own table `BtObjHdl` (32 x 0xC) filled by `_GET_OBJHDL(handle, partIndex|-1, "frameName")` = frames of the generated floor model (doors "ndoor_r", "out_door_l", "in_door", "in_2", "out_2", ...), moved/hidden with `_SET_OBJHDL_POS/ROT/DRAW_FLAG`, read with `_GET_OBJHDL_POS/ROT`; `_GET_NPC_OBJHDL(handle, npc)` binds a dungeon NPC.
* 2D: `_INIT_SPRITE`, `_DRAW_SPRITE`, `_DRAW_BG_SPRITE` (draw-list sprites with a source rect from a texture block), `_SP_INIT/_SP_SET_TEX/POS/RGBA/ROT/MOVE/DRAW` (16 persistent sprites), `_DRAW_BACK(off, r,g,b)` (clear colour), `_SCREEN_FILTER(r,g,b,a)`, `_FADE_IN/OUT(frames,r,g,b)` (full-screen overlay, alpha step 128/frames per tick, `_CHECK_FADE(&v)`), `_DRAW_DAY/_INIT_DRAW_DAY` ("Day N" banner), `_MAP_TITLE_OFF`, `_SET_LIGHT/_SET_LIGHT_COLOR/_SET_AMBIENT/_SAVE_LIGHT/_LOAD_LIGHT` (4 directional lights + ambient, restored when the event ends), `_SET_DOF_LEVEL`, `_DRAW_THUNDER`, `_SET_WIND`, `_DRAW_SHADOW`, `_DRAW_EDIT_WATER`, `_DRAW_WATER_SURFACE`, `_SET_CLIP_POINT(x,y,z,r)` (Georama draw clip sphere).
* Sound: BGM/SE/ambient/voice loads are asynchronous (`_LOAD_BGM`, `_LOAD_SND_SYNC` poll); all play/stop calls map one-to-one onto a sound host (ids are the game's own bank numbers).
* Game state: flags (`_SSET/SGET_GAME_FLAG` bytes, `_SSET/SGET_GAME_INT_FLAG` ints, `_SSET/SGET_MAP_FLAG` per-map bits with inverted polarity, `_SET/GET_WORK_FLAG` 32 scratch ints, dungeon-run flags `_SET/CHECK_EVENT_FLG` 50 bytes), party and items (`_SITEM_GET/CHECK/CHECK_ALL/LOST`, `_SGET_EQUIP_WEAPON`, `_SEQUIP_DEFAULT_WEAPON`, `_SSKILL_GET`, `_SADD_MAXITEM`), Georama (`_CHECK_COMPLETE_PARTS`, `_SGET/SSET_CMP_EVENT`, `_SCHECK_REQUEST`, `_SGET_REQUEST`, `_SGET_ATRA_PARTS_NUM`, `_SATRA_*`), clock (`_SSET_TIME`, `_SGET_TIME`, `_SADD_TIME`, `_GET_CLOCK`), visited maps / quest dungeons (`_SADD_VISIT_MAP`), NPC talk progress (`_SGET/SSET_NPC_TALK_MES`: a per-Georama-NPC counter in `GrdNPCData.+8` that the scripts use to rotate dialogue). Their storage lives in `CSaveData` (`user_status.h` / the save format in dccore); no behaviour beyond get/set.

## 6. Dungeon story events (`CDungeonEventMan`, system script) (READ + VERIFIED on dun/script/d0N/event.stb)

* Script file: `dun/script/d0N/event.stb` (N = 1..7, `BtSystemScriptLoad(selectMapNo)`), run by the SAME `CRunScript` and `Ed*` machinery as the town, with EVENT + DUNGEON ext tables, windows 1 and 2 (`BtEventMes0/1`), 6 NPC slots (`NPCUnit`, texture block 0x20+k), hero = `CharaMain`, camera `SubCamera` (initialised from the gameplay camera so cuts start where the player looks). The embedded message bank (header +0x14: 66 messages for d01) is the only bank.
* Start (`MoveChara`, `gameTask 400`): inputs are `DAT_01d5676c` = function id, `DAT_01d56774` = "heavy event" flag (non-zero: delete textures/effects/monsters' view data, switch `read_buffer` to the bigger event area before loading scene models), `DAT_01d56764` = action mode (0 stepped on, 1 OK pressed, 2 item-use button), marker position/rotation -> `_GET_EVENT_POS/ROT`. `BtSystemScriptInit` then `EdEventInit(fid, buffers, BtEventData)`; if the function yields, `gameTask = 0x191`: every frame `EdEventMode(NowCamera)`; at the end (`result > 0`) `BtSystemScriptAfter` (event mode off, `EdEventAllClear`, rebuild the follow camera per `_RESET_CAMERA` / `_RESET_CAMERA_ANGLE`, `NowCamera = MainCamera`) and `gameTask = 0`.
* Who raises an event (checked each frame during normal play, `gameTask 0`):
  1. **Marker under the player** (`SearchDataSlotPos`): slot data `+0x20` = event id (cfg `PT_MARKER "name",radius,id,on,on,on,on`; the four ON/OFF flags are read by `CDungeonEventMan::SetupEvent`, see notes/native-plan.md M4 for the verified slot layout), `+0x2c` required character (-1 any), `+0x24` auto-start (fade out 120 ticks then event, state 500 -> 400), `+0x38` heavy flag, `+0x30` accepts the item-use button. Auto slots start when stepped on, the others on OK (action mode 1) or the 0x80 button (mode 2). `_SET_EVENT_SW(id, on)` switches all slots of an id (e.g. 160/161 stairs and gate, 400 boss door -> 500 boss start). Marker ids in the shipped cfgs: 102, 111, 160, 161, 200, 201, 250, 251, 300-302, 350, 351, 375, 400-405, 500, 520, 800.
  2. **Monster script**: monster ext 107 `_STATUS_SET_EVENT(id)` / 111 `_RUN_SCRIPT(id)` store `NowMonstorUnit+0x94`; the loop fades out 120 frames (`EdFadeOut`) then runs the id (bosses call `_RUN_SCRIPT(510)` when they die; 9 boss scripts: c12a..c23a).
  3. `CheckEventFlag2` (story flag/kill triggers), 4. floor cleared (`_CLEAN_MONSTOR_SCRIPT_NO(id,arg)`: runs when `GetMonstorNum() < 1`), 5. death (id 5, fade then game over), 6. trapped treasure box (id 15, or 16 for kind 5), 7. `_RUN_SCRIPT_NO(id[,arg])` from inside an event (request 5).
* **Deferred requests** (`DAT_01d567d8`, set by ext functions, executed by the loop while the event is still active; jump table `_4416` is not visible in the decompile, the mapping below is deduced from the handler bodies): 1 `_ITEM_USE_WINDOW(&result, ids...)` -> `BtMiniItemSelect` (item picker restricted to `ids`, result item id or -1 written to `*DAT_01d567c0`, then the script resumes), 2 `_SET_URA_DUNGEON` -> swap Main/Ura (back-floor) `CDungeonMap` + event manager + monster unit (`gameTask 0xa0`), 3 `_OPEN_ENTRANCE_WINDOW(&floor)` floor picker, 4 `_GO_DUNGEON` end of the floor-start flow (continue into play), 5 `_RUN_SCRIPT_NO`, 6 `_OPEN_ESCAPE_WINDOW(&result)` ("leave the dungeon?" with Yes/No, `BtEscape`).
* **Floor start (function 100)** (dccore already runs it for the map name): `fn 100` -> `_GET_FLOOR_LEVEL/_GET_OLD_FLOOR_LEVEL` (old = -1 on first entry, then the entrance window may pick the floor) -> `_SET_FLOOR_LEVEL` -> `_SET_DUNGEON_MAP("d01main_a", 1)` -> `_ALL_DRAW_FLAG(0)` -> `_LOAD_DUNGEON_MAP2` -> `_SET_RANDOM_MAP` (build with the seed from the generator already ported) -> `_LOAD_MONSTOR` (`BtLoadMonstor(0)` + `BtArrengeMonstor` = the already-ported placement) -> door/gate objects by frame names (`ndoor_*`, `out_door_*`, `in_door`: moved to open/closed positions from game flags 102.. and `_SET_EVENT_SW`) -> optional stair/"arrive" cutscene using `dun/dNN/stair_mos/in01.chr` -> `_GO_DUNGEON`. Traced with the Python VM for d01: `GET_FLOOR_LEVEL, GET_OLD_FLOOR_LEVEL, SET_FLOOR_LEVEL, SET_DUNGEON_MAP, ALL_DRAW_FLAG, LOAD_DUNGEON_MAP2, ALL_DRAW_FLAG, SET_RES_LIMMIT_ZONE, SET_RANDOM_MAP, LOAD_MONSTOR, GET_OBJHDL x4 / SET_OBJHDL_POS x4, SET_OBJHDL_DRAW_FLAG, FADE_IN, ... stair cutscene`.
* Per-event examples (d01): 160 exit stairs (`out01.chr` stair scene, fades, then `_SET_FLOOR_LEVEL`+`_RUN_SCRIPT_NO(100)` to load the next floor); 161 gate: mode 1 shows "needs the gate key" (`fn_50ec(1, 200, ...)`), mode 2 `_ITEM_USE_WINDOW(&v, 195)` -> if the right key then `_SET_EVENT_SW(161,0)`, `_SET_EVENT_SW(160,1)` and the gate objects slide; 200 trap door to the back floor (mode 1: Yes/No `fn_4bdc(1,302,8,0,0,2)`, then a scene with the `ura.cfg` hero and the door frames `udoor_l/r`, ending in `_SET_URA_DUNGEON`, INFERRED from the tail); 250/251 locked door with key `0xd8+dungeon`; 350/351 character rooms (`_GET_NOW_USER_ID`, `_SET_MAIN_CHR2`, `_MES_MAKE(1, 1900+id)`); 400 boss key door (`_ITEM_USE_WINDOW(&v, 207)` then `_SET_EVENT_SW(500,1)`); 500 boss start: close `boss_door`, play the boss intro (flag 32 first-time scene) ; 510 boss defeated (`_SSET_GAME_INT_FLAG(0,100)` + clear scene); 5 death; 15/16 box traps; 90/102/103/110: pre-built-floor setup and story scenes of dungeon 1.
* Scripts only drive presentation + a small set of state changes; the rest (marker detection, door open/close collision, chests, gate key, exit) is already in `dccore/event_world.*`. To replace the stand-in "show a message" behaviour, the host needs the services of section 8.

## 7. Opening sequence (NOT the STB VM)
`MapJump(400)` (new game from the title) selects main-loop mode 5 = `OpeningInit/OpeningLoop` (title overlay, `opening.cpp`, not decompiled in `all_dun.c`: the functions with these names in the dump belong to the dungeon overlay that shares the load address). Its data (all VERIFIED present): `opdat/opening.scr` (55 KB text script, Shift-JIS comments), `opdat/opinfo.cfg` (stage description: ambient, 3 point lights, fog, `SKY`, `CRD`, `GND`, `BLD`, `OBJ_ROT`, `FIRE`, `GRD_IMG`...), `opdat/optext_N.mes` (subtitle bank, same .mes format), `opdat/norn*.cfg`, `norn*/`, `toan.cfg`, `seirei.cfg`, `chara/*.chr`, `scene/`, `dungeon/*.mds`+`dungeon.pak`.
`opening.scr` is a flat list of timed commands (frames): `WAIT n`, `OBJ_DISP obj,ON|OFF` (23 stage objects), `OBJ_MOVE obj,key` and `CAMERA key,startFrame,endFrame,speed` (play a recorded track segment), `OBJ_MOTION obj,m`, `OBJ_MOTION2 obj,a,b` (blend), `OBJ_STEP obj,speed` (motion speed), `OBJ_EYE/OBJ_MOUTH obj,anim,frames`, `OBJ_TALK obj,msg,frames,x,y,tailx,taily` (speech balloon from `optext`), `MESSAGE n,msg,x,y,w,h`, `NEXT_MES n`, `MES_CLR`, `WAIT_KEY`, `SE ch,vol,no,pan`, `SE_STOP`, `SOUND_FADE`, `FADE_IN/OUT ch,speed`, `SPRITE n`, `BEEM`, `BOM`, `SCENE_LOAD n`, `SCENE n`, `LOAD_OBJ a,b`, `END` (counts over the file: WAIT 362, OBJ_DISP 185, OBJ_MOVE 135, OBJ_MOTION 129, CAMERA 125, OBJ_EYE 115, OBJ_STEP 67, OBJ_MOTION2 42, SPRITE 33, OBJ_TALK 31, OBJ_MOUTH 27, MESSAGE 27, SE 27, WAIT_KEY 24, BEEM 16, FADE_IN 11, FADE_OUT 11, MES_CLR 9, SCENE_LOAD 9, SCENE 9, BOM 7, NEXT_MES 5, SOUND_FADE 4, LOAD_OBJ 3, SE_STOP 2, END 1). The meaning of each argument must be taken from the (undecompiled) interpreter; the shapes above are exact. The first in-game scene after the opening is NOT part of this: `OpeningLoop` returns and the main loop does `MapJump(0, -1)` -> Norune map 0 -> event 128, then event 150 for the Norune intro (see tests below).

## 8. Implementation plan

### 8.1 Architecture
`dccore` gets an `EventRuntime` (new, pure C++, no Godot) that owns one `ScriptVm` + the event state of sections 3-6, implements the ~370 ext functions by decoding arguments (as in `event_ext.csv`) and calling a `EventHost` for everything that is presentation, file access or save data. The VM already runs the bytecode; what is missing is (1) the dispatch table, (2) the per-frame `EdEventMode` order of section 3.3, (3) the pieces of engine state the scripts read back (result code, ASQ queues, message window state machine, talk list, camera mode flags, flag arrays), (4) the town-side triggers of section 3.4/3.5. A `NullHost` that answers every poll instantly ("loads done, fade done, ASQ idle, message complete then closed, pad OK pressed") lets whole scripts run headless; a run of function 128 / 256 with `tools/dc/stbrun.py` shows this is enough for the map-entry and talk scripts to terminate (new game flags). That headless mode is also the regression harness: for every map run 128, 256 (all info ids), 150, 300.. and diff the ext-call log against golden logs.

```cpp
namespace dccore::ev {

enum class Kind { Town, Interior, Dungeon };
struct Vec3 { float x, y, z; };

// ---------- the runtime (dccore) ----------
class EventRuntime {
public:
    EventRuntime(Kind kind, std::shared_ptr<ScriptProgram> stb, EventHost& host, MessageBank embedded_bank);
    // EdEventInit: false if the function does not exist or finished at once (result_code() may still be 9 = talk)
    bool start(int function_id);
    // one frame of EdEventMode (3.3); returns false when the event is over
    bool step();
    void skip();                              // START -> confirm: ScriptVm::skip()
    int  result_code() const;                 // DAT_01d3d618: 3 normal end, 4 interior, 5 dungeon, 7 outside, 8 map jump, 9 talk, 0xb fishing
    const NavRequest& nav() const;            // interior name / start event / map number / floor requested by the script
    void modal_done(int value);               // host reports the end of a modal screen (menu_mode 5..10, dungeon requests 1,3,6)
    // talk mode (GameMode 2) state shared with the host UI
    TalkState& talk();                        // list DAT_01d3d434[], kind, header, select index
};

// ---------- what the front end implements ----------
class EventHost {
public:
    // --- time / saves (CSaveData / UserStatus) ---
    virtual int  game_flag(int i) = 0;         virtual void set_game_flag(int i, int v) = 0;
    virtual int  int_flag(int i) = 0;          virtual void set_int_flag(int i, int v) = 0;
    virtual int  map_flag(int i) = 0;          virtual void set_map_flag(int i, int v) = 0;     // polarity inverted by the runtime
    virtual int  item_index(int item) = 0;     virtual bool give_item(int item, int kind) = 0;  virtual void lose_item(int item) = 0;
    virtual int  party_size() = 0;             virtual float clock() = 0;  virtual void set_clock(float) = 0;
    virtual GeoramaQuery& georama() = 0;       // CheckComplete(parts), CompEvent get/set, Request, atra counts, parts object pose
    virtual VillagerQuery& villagers() = 0;    // in-village check, appear on/off/in/out/move, load, talk-message counters
    virtual DungeonQuery& dungeon() = 0;       // floor, old floor, character, quest/status, event flags, gate key id (also usable by dungeon.cpp)
    // --- loading (all return immediately; poll answers come from the host) ---
    virtual void set_dir(const std::string&) = 0;
    virtual void begin_load_packs(const std::vector<std::string>&) = 0;       // _LOAD_CHR_FILE
    virtual bool loads_pending() = 0;                                         // _LOAD_SYNC / _LOAD_SND_SYNC
    virtual bool load_character(int slot, const std::string& cfg_entry, int file, int buffer) = 0;   // slot -1 = hero
    virtual bool load_scene(int scene, const std::string& cfg_entry, float speed) = 0;
    virtual void load_texture_entries(int block, const std::vector<std::string>&) = 0;
    virtual void delete_character(int slot) = 0;
    // --- character / scene state (all coordinates already transformed by the event world matrix) ---
    virtual void set_position(int slot, Vec3) = 0;     virtual Vec3 position(int slot) = 0;
    virtual void set_rotation(int slot, Vec3) = 0;     virtual Vec3 rotation(int slot) = 0;        // yaw = y
    virtual void set_scale(int slot, Vec3) = 0;        virtual void set_body_size(int slot, float r, float h, float d) = 0;
    virtual void set_motion(int slot, int motion, float speed, int flags) = 0;
    virtual bool motion_near_end(int slot, int motion, float tail_frames) = 0;                   // for ASQ kind-7 nodes
    virtual void set_texture_anime(int slot, int no, bool on) = 0;
    virtual void set_flags(int slot, DrawFlags) = 0;   // draw/shadow/collision/stop/before/foot-sound/ambient/alpha/point light
    virtual void attach(int slot, const std::string& frame, Ref parent) = 0;                    // sync scene/chara/item; Ref = {scene|chara|item, frame name}
    virtual void detach(int slot) = 0;
    virtual Handle bind_frame(HandleKind, int index_or_slot, const std::string& frame) = 0;     // _HOBJ_* / _GET_OBJHDL
    virtual void handle_set(Handle, HandleOp op, Vec3 v) = 0;     virtual Vec3 handle_get(Handle, HandleOp op) = 0;
    virtual void handle_visible(Handle, bool) = 0;
    virtual void sync_scene_camera(int scene, bool on) = 0;
    // --- camera ---
    virtual void camera_direct(Vec3 pos, Vec3 ref, float speed, bool snap) = 0;                // _SET_CAMERA / _POS / _REF / _SPEED
    virtual void camera_follow(Vec3 target, float height, float dist, float angle, float speed) = 0;
    virtual void camera_target(CameraTarget kind, Ref ref, Vec3 offset) = 0;                    // chara/object sync for ref and pos
    virtual void camera_misc(float roll, float projection) = 0;
    virtual CameraPose camera() = 0;                                                           // GET_CAMERA_*
    virtual void camera_restore(int mode, float angle) = 0;                                    // after the event
    // --- message windows (host owns glyph drawing; the runtime owns the state machine of 4.2 or the host does and answers State()) ---
    virtual int  mes_open(int win, int msg_id, int number, std::span<const int> slots) = 0;
    virtual int  mes_state(int win) = 0;                                                       // 0 closed 1 opening 2 printing 3 complete 4 closing 5 page wait
    virtual void mes_page(int win) = 0;  virtual void mes_close(int win) = 0;
    virtual void mes_style(int win, MesStyle) = 0;                                              // pos mode, tail, fukidashi, speed, okuri, flags, cursor, autoset points/chars
    virtual void system_message(SysMes kind, int a, int b) = 0;                                 // item get / skill / bag up / clear
    // --- 2D / screen ---
    virtual void fade(bool out, int frames, int r, int g, int b) = 0;   virtual bool fade_done() = 0;
    virtual void screen_filter(Rgba) = 0;  virtual void background(bool off, Rgb) = 0;  virtual void lights(const LightSet&) = 0;
    virtual void sprites(const SpriteOp&) = 0;  virtual void day_banner() = 0;  virtual void weather(WeatherOp, float, float, float, float) = 0;
    // --- sound ---
    virtual void sound(const SoundOp&) = 0;    // bgm/se/ambient/voice load, play, stop, volume, fade; pan/vol from position
    virtual bool bgm_fading() = 0;
    // --- input ---
    virtual uint32_t pad_on() = 0; virtual uint32_t pad_down() = 0; virtual uint32_t pad_up() = 0;   // engine bit layout; the runtime swaps OK/cancel
    virtual void stick(float& lx, float& ly, float& rx, float& ry) = 0;
    // --- modal screens and navigation (answered later with modal_done) ---
    virtual void open_modal(Modal kind, std::span<const int> args) = 0;   // use-item list, name entry, world map, fishing menus, entrance window, escape window
    virtual void navigate(const NavRequest&) = 0;                         // map jump, interior, outside, dungeon, fishing
    virtual void dungeon_op(const DungeonOp&) = 0;                        // set map, load map2, load monsters, random map, event switch, spawn monster, atra/box, door state, ura swap, kill player, bomb, status error
    virtual void mini_game(const EbOp&) = 0;                              // EB_* timing mini game
};
}
```
Notes: (a) Slot -1 / handle kinds / "Ref" are exactly the game's concepts of 5.1/5.5; (b) the runtime keeps `follow_chara`, camera sync targets, flag arrays, ASQ queues, the world matrix and the talk list itself, so a host only sees resolved calls; (c) a `GodotEventHost` maps these onto Node3D animation players, CanvasLayer text boxes (draw glyphs from the font of `meswin/gaiji.img` or a stand-in font) and the existing `DcModels` loaders; (d) the message window state machine of 4.2 is small enough to live in `dccore` (about 150 lines: reveal accumulator, page break, pause, fade 0..1, State()); only the drawing is the host's.

### 8.2 Priorities
**A1: walk around Norune (e01) and talk to a villager.** Needed before any cutscene: `EventRuntime` core + `NullHost` semantics, plus the town-side logic (sections 3.4-3.5: `EdSearchNearNPC`, `EdGetEvent` on the `mapinfo.cfg` event points, talk mode with choices, door -> interior). Ext functions actually executed by functions 128 (map entry) and 256 (talk) over all 5 towns and all their interiors with new-game flags (74, traced): `_GET_TALKNPC_INFO_ID _SGET_DUNGEON_STATUS _SET_RETURN_CODE _SGET_NPC_TALK_MES _SSET_NPC_TALK_MES _SET_TALK_MES _SET_TALK_SELECT_MES _CHECK_PLACE _CHECK_COMPLETE_PARTS _CHECK_IN_VILLAGER _SGET_CMP_EVENT _SGET/SSET_GAME_FLAG _SGET_GAME_INT_FLAG _SSET_MAP_FLAG _GET/SET_WORK_FLAG _SITEM_GET _GET_OLD_MAPNO _FADE_IN _GET_BGM_NO _LOAD_BGM _LOAD_SND_SYNC _PLAY_BGM _PLAY_SE _INITIALIZE _CLEAR_VILLAGER_BUFF _CLEAR_EVENT_BUFF _SET_CURRENT_DIR _LOAD_CHR_FILE _LOAD_SYNC _ACTIVE_FILE_BUFFER _LOAD_CHARA _INIT_CHARA _LOAD_VILLAGER _APPEAR_VILLAGER_ON/IN _SET_NPC_POS/ROT _NPC_DRAW _NPC_COL _NPC_DRAW_SHADOW _RESET_CAMERA _RESET_CAMERA_ANGLE _SET_CAMERA_POS/REF/SPEED _GET_CAMERA_POS/REF _SET_PROJECTION _HOBJ_FIXPARTS _SET_OBJ_ROT _OBJ_DRAW _DRAW_EDIT_WATER _DRAW_WATER_SURFACE _ASQ_INIT/SYNC_CHARA/SET_POS/SET_ROT/MOVE/ROT_MOVE/MOTION_PLAY/MOTION_NEXT/MOTION_STOP/MOTION_TRG _SET_MES_POS/SHIPPO/AUTOSET _MES_MAKE/CLOSE _CHECK_MES/_COMPLETE/_WAIT _GET_PADDOWN _SKIP`. (The first-visit intro of Norune, function 150 and map 0 flag paths, adds the camera, ASQ, scene and sprite families = A2.)
**A2: cutscenes inside towns/interiors (the 108 town/interior/story scripts):** everything in sections 5.3-5.5 (camera, ASQ, scene sync, object handles, sprites, lights, fades, sound). By use count the top families are ASQ (`_ASQ_MOTION_TRG` 3294 calls, `_ASQ_MOTION_PLAY` 2966, `_ASQ_INIT` 2318, `_ASQ_SYNC_CHARA` 2023), camera (`_SET_CAMERA_REF` 1705, `_SET_CAMERA_POS` 1644), fades (800/667), `_PLAY_SE` 828, `_LOAD_CHARA` 914, `_SET_NPC_MOTION` 1019, messages (~4000 calls over `_MES_*`). Do ASQ + camera + messages first: ~85% of calls.
**B: dungeon floors:** the closure of {100, 90, 160, 161, 200, 250, 251, 350, 351, 500, 510} over d01..d07 is 185 distinct ext functions, most of them presentation (`_GET_OBJHDL` 399 calls, ASQ, camera). The state-changing ones to implement first (all trivial): `_GET/SET_FLOOR_LEVEL, _GET_OLD_FLOOR_LEVEL, _SET_DUNGEON_MAP, _LOAD_DUNGEON_MAP2, _SET_RANDOM_MAP, _LOAD_MONSTOR (already in dungeon.cpp/monsters.cpp), _SET_EVENT_SW, _ALL_DRAW_FLAG, _GET_OBJHDL + _SET_OBJHDL_POS/DRAW_FLAG` (door objects, `FloorLayout::door_open` / `DcDungeon::open_door` exist), `_GET_ACTION_MODE, _ITEM_USE_WINDOW, _OPEN_ENTRANCE_WINDOW, _OPEN_ESCAPE_WINDOW, _GO_DUNGEON, _RUN_SCRIPT_NO, _GET_NOW_USER_ID, _SET_MAIN_CHR2, _CHECK/SET_EVENT_FLG, _SGET/SSET_GAME_FLAG, _SET_LIMMIT_ZONE, _SET_MONSTOR_ID, _SET_URA_DUNGEON, _SET_DEAD_FLAG, _SET_FLOOR_TITLE`. With a headless host (cutscenes fast-forwarded: fades/ASQ/scenes finish at once) events 160/161/200/250/350/500/510 execute their state changes immediately, which is exactly what `dccore/event_world.*` approximates today by hard-coding; switch it to run the real script and keep the hard-coded paths as the cross-check.
**C: opening:** separate small interpreter for `opdat/opening.scr` + `opinfo.cfg`; blocked on reading the title-overlay interpreter (not in `all_dun.c`), but the command vocabulary is only 26 words (section 7) and can be replayed with timed segment playback of the model tracks in `opdat/*`.
Suggested order: (1) EventRuntime + NullHost + ext decoding + flag storage + tests running every function in headless mode (compare ext logs with the tracer), (2) message state machine + talk mode + Godot text box, (3) NPC/hero placement + motions + fades + BGM names, (4) ASQ port, (5) camera + scenes + object handles, (6) sprites/lights/filter, (7) dungeon requests and events, (8) opening.

### 8.3 Parts of `native/dccore/src/text.cpp` to extend (notes only, as requested)
Return tokens instead of a string (`Glyph(ch)`, `LineFeed`, `PageBreak`, `Colour(n)`, `Skip(px)`, `Pause(n)`, `Name(i)`, `SysSlot(k)`, `Value(k|number)`, `Icon(n)`), map FD21..FD78 as in 4.3 plus `FD5D -> '-'`, keep `names` substitution for FAFA..FAFF, resolve FBFE/FBFD/FBFC/FBFB/FBF2.. through the system bank id given by the script, FBFF/FBFA..FBF3 through the numeric slots, treat `FCxx` as colour not text. The 31,752-byte tail of each file must be ignored (stop at `text_bytes`).

### 8.4 Validation recipe (not run here)
`tools/dc/diff_ext.py`/`record_attack.py` already hook `CRunScript::ext` (0x23DD00) over PINE for monsters; the same hook records `{id, args}` of every event ext call in the running game (town and dungeon share the function). Replaying the recorded button/flag inputs through `EventRuntime` + NullHost and diffing the id/argument stream validates a script end to end (positions are exact floats; camera/ASQ values are the strongest check).

### 8.5 Tooling added
`tools/dc/event_usage.py` (ext usage per file, `--json`, `--dis` listing with correct names), `tools/dc/stbtext.py` (pseudo-code with variable names `vN`, ext names, labels), `tools/dc/stb_closure.py` (ext set needed by given function ids), `tools/dc/stbrun.py` (headless trace of one function with answers for the polls), `notes/event_ext.csv`.

## 9. Not resolved (where to look)
1. Dungeon request dispatcher table `_4416` (maps `DAT_01d567d8` 1..6 to handlers): jump table in the overlay near 0x01DB4C44 (`MoveChara`); needs a read of overlay bytes.
2. Who toggles `event_pause` and reads `skip_enable` (START during a skippable event): probably the MainDraw/`EditLoop` START handling through an alias; search for stores to 0x1D3D?? flags next to `event_stop`.
3. The opening interpreter (title overlay) and its argument meanings; the stage objects 0..22 of `opinfo.cfg`.
4. Exact Georama-side data: `mapinfo.cfg` event point field layout (other agent), `VILLAGER_INFO` (0x90 bytes) layout, shop/menu kinds of `EdInitMenu(3/4)` and `EdTalkMode` return codes 2/3/4.
5. Pixel-level `ClsMes`: gaiji width table (`EditPartsData+0x491c`), colour cluts (`MesWinClut`, FC01/FCFF), balloon textures (`fuki*.img`), tail polygon rules, `Color2Clut` presets of `Preset()`; the auto placement (`AutoSet`) tie-breaking looks lossy in the decompile (a store of -1.0 is overwritten).
6. `CFrame::SetReference` composition order, `CCamera::SetNextPos/SetNextRef/Step` interpolation formula for `_SET_CAMERA_SPEED(n)`; `CNPCharacter::PlaySeq` (the separate villager walk-route sequences, not used by event scripts).
7. `_SET_NPC_AMBIENT` one-argument form copies a constant default vector; `_EVERY_TALK_EVENT` and `_SET_TALK_CAMERA` are not used by any script; ext 5 `_NEXT_EVENT` and 9 `_EXIT_CODE` write variables no event code reads.
8. EB mini-game (ids 950-958): only the call shapes are known (rhythm prompts timed from a character's motion ranges, max 64 keys, key = CGamePad mask); scoring/drawing in `EBLoop` not documented.
9. Triggers not located for dungeon functions 90, 103, 110, 150 (150 initialises the exit-gate/door state from game flags like 102; 102 itself is a `PT_MARKER` id). Candidates: `CMonstorUnit::CheckEventFlag2` (its symbol is not in the decompile dump), monster-script ext 107/111 values, `_RUN_SCRIPT_NO` chains in other floors' scripts.
10. The retail trigger of the first Norune scene after the opening (see the table row for 150).

## Appendix A: function ids per event.stb (files with the same set are grouped)

| function ids | files |
|---|---|
| 128,150,200,256 | e01/in/i01h02 e01/in/i01h03 e01/in/i01h04 e01/in/i01h07 e01/in/i01h10 e01/in/i01h11 e02/in/i02h01 e02/in/i02h02 e02/in/i02h04 e02/in/i02h05 e02/in/i02h06 e02/in/i02h07 e02/in/i02h08 e02/in/i02h10 e03/in/i03h04 e03/in/i03h05 e03/in/i03h07 e04/in/i04h03 e04/in/i04h05 e04/in/i04h08 e04/in/i04h09 |
| 150,300 | s11 s12 s14 s17 s33 s35 s39 s40 s94 s99 |
| 150 | s17/1 s79 s86 s87 s90 s93 s95 s96 s97 |
| 150,300,301 | s16 s37 s88 s92 s98 |
| 128,150,200,256,300 | e03/in/i03h10 e04/in/i04h01 e04/in/i04h04 e04/in/i04h06 |
| 11,12,13,128,256 | s13/in/s1901 s13/in/s2001 s13/in/s2101 |
| 12,128,150,200,256 | e02/in/i02h03 e03/in/i03h081 e03/in/i03h08 |
| 128,150,300 | s31 s38 s41 |
| 1,2,128,150 | s26 s27 |
| 11,128,150,200,256 | e01/in/i01h01 e01/in/i01h05 |
| 128,150 | s15 s24 |
| 128,150,256 | e01/in/i01h011 e01/in/i01h051 |
| 128,256 | s04/in/s07 s04/in/s08 |
| (none) | e02/in/i02c09 |
| 0,1 | test.stb |
| 0,102 | test4.stb |
| 1,2 | system |
| 1,2,3,4,5,12,128,150,256,300,301,302,303,304,305,310 | s13 |
| 1,2,3,4,11,12,13,14,128,131,132,133,134,150,200,201,202,203,204,205,206,207,214,256,301 | e02 |
| 1,2,3,11,12,128,131,132,150,200,201,202,203,204,205,206,207,208,209,256,300,301,302,303,304,305 | e03 |
| 1,2,11,12,13,128,131,132,133,134,150,200,201,202,203,204,205,206,207,208,209,210,211,256,300,301,302,303 | e04 |
| 1,2,11,12,128,133,134,150,300,301,302,303,304,305 | s09 |
| 1,2,128 | s51 |
| 1,2,128,150,256,300,301,302,303 | s32 |
| 1,11,12,128,150,256,301 | s01 |
| 1,11,102,128,130,131,132,133,134,150,200,201,202,203,204,205,206,207,208,256,300 | e01 |
| 1,11,128,150 | s23 |
| 1,11,128,150,256,300,301,302 | s28 |
| 1,11,128,150,256,300,301,302,303,304,305,306,307 | s04 |
| 1,11,128,150,300 | s03 |
| 1,128 | s50 |
| 1,128,150,300,301,302 | s30 |
| 2,3,4,5,128,131,150,200,203,204,209,210,256,301,302,303 | e05 |
| 5,15,16,90,100,102,103,110,150,160,161,199,200,250,300,301,302,350,351,400,500,510 | dun/script/d01 |
| 11,12,128,150,300 | s25 |
| 11,128,150,200,256,300 | e03/in/i03h06 |
| 11,128,150,256 | e03/in/i03h09 |
| 11,128,150,256,300 | e04/in/i04h02 |
| 11,128,150,256,300,301 | s09/in/s1001 |
| 11,128,150,256,301,302,303 | s13/in/s2201 |
| 12,128,150,256 | e01/in/i01h06 |
| 12,128,256 | s01/in/s02 |
| 15,16,90,100,101,150,160,161,199,200,201,250,251,350,351,360,361,365,366,370,375,404,405,500,510 | dun/script/d05 |
| 15,16,90,100,101,160,161,196,197,198,200,201,250,350,351,360,361,365,366,370,400,401,402,403,510 | dun/script/d04 |
| 15,16,90,100,101,160,197,198,199,200,250,350,351,360,361,365,366,370,375,510 | dun/script/d06 |
| 15,16,90,100,101,160,199,200,250,251,510 | dun/script/d07 |
| 15,16,90,100,105,111,150,160,161,170,200,250,300,350,351,360,361,400,500,510,520,800 | dun/script/d02 |
| 15,16,90,100,160,161,195,196,197,198,199,200,250,350,351,360,361,365,366,400,510 | dun/script/d03 |
| 128,150,199 | s91 |
| 128,150,256,300 | s04/in/s18 |
| 128,150,256,300,301 | s04/in/s06 |
| 128,150,256,300,301,302 | s04/in/s05 |
| 128,150,300,301 | s29 |
| 128,300,301 | s36 |
| 150,198 | s77 |
| 150,199 | s78 |
| 150,200 | s36/in/i03h09 |
| 150,300,301,302,303 | s34 |
