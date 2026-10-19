# Fishing mini-game (GameMode 0x10), fish tables, exchange, Mardan/Garayan

Sources: decompile `work/decomp/all_dun.c` (functions named in `notes/symbols.csv`, `tools/dc/decomp_index.py show <name>`), event scripts `data/gedit/{e01,e02,e04,s09}/event.stb` (printed with `tools/dc/fishscripts.py <map>` / `tools/dc/stbtext.py`), data read from the ELF with `tools/dc/elfdata.py` / `tools/dc/fishdata.py`.
Tags: **READ** = read from the decompile/scripts, **DATA** = value read out of the ELF / disc files, **INFERRED** = my reading, not run in the game. Nothing here was checked against the running game (no PCSX2 run).
New helper scripts: `tools/dc/fishdata.py` (C-string reader), `tools/dc/fishscripts.py` (prints the fishing functions of a town event script).

Overview in one paragraph: fishing is **not** a separate overlay. It is GameMode 0x10 of the town loop; all game logic is one state machine inside `EdMoveChara` (0x16a160, variable `chara_fishing`, 13 states), the fish are 6 `CFish` objects (`fishing.cpp`), the line is a 24-point rope + float tetrahedron + hook triangle (Verlet, `FishLineStep`). The game has **no line tension and no fight mini-game**: it is a timing game (press Cross while a fish is "hooked", a window of 20..49 frames); the fight afterwards is a fixed cut-scene of >= 2 s. Size, fish kind, fishing points (FP) are decided when the fish is created / hooked.

## 1. Entering and leaving

### 1.1 Where fishing exists (READ, scripts)
| map | pond data (`_LOAD_FISHING_DATA(set, x_min, z_min, x_max, z_max, water_y, ground_y)`) | fish set | event that opens the board |
|---|---|---|---|
| `e01` Norune | rect = Georama part 12 (`LAKE_PARTS`, `_GET_EDIT_PARTS_POS(12,..)`) +-100 in x/z; water -13, ground -30; fish area +-50 around the lake; player placed at (40,0,96) facing pi | **0** (4 fish) | function 11 (see 1.2) |
| `e02` Matataki | two ponds, argument v0 = 0/1: pond 0 rect (-1340,-140)..(-750,200), water -13, ground -45, fish area `_INIT_FISH(-1181,-10,-964,50)`; pond 1 rect (550,1000)..(1200,1200), `_INIT_FISH(650,1120,1050,1209)` | **v0+1 = 1** (pond 0) / **2** (pond 1), 5 fish | internal function `fn_f674` called from the TALK_EVENT 12 NPC |
| `e04` Queens | rect = Georama part 11 +-160; water -5, ground -30; fish area +-50; player at (65,2.26,69) | **4** (4 fish) | function 12 (TALK_EVENT 11) |
| `s09` | rect (-1500,833)..(-1313,1200), water 0, ground -30, `_INIT_FISH(-1500,933,-1390,1200)`; player at (-1350,7,841) | **3** (5 fish, fish are **not drawn under water**: `draw_under_water = (set != 3)`) | function 12 |

(e01/e04 ponds exist only as the Georama LAKE part the player has built/placed; the rect is computed from where it stands. Which exact event point triggers function 11/12 and whether the board only exists with the lake placed is not checked: open question 1.) The fishing menu text is in `<map>_<lang>.mes`: message 20 = 4 choices `Fish / Exchange FP / Fishing log / Quit fishing`, 21 = `Seems like you can fish here but you don't have a fishing pole.`, 22 = `Continue fishing / Quit fishing`; the catch window is message 2000..2005 of `<map>talk_<lang>.mes` (`{name} ({size}cm) caught!! <PAGE> Fishing Points +{fp} / Total Points {total} / Record {name} {record}cm`).

### 1.2 The board menu (function 11/12, READ)
`_GET_PADDOWN & 32` (OK) on the event point (otherwise `_DRAW_EXCLAMATION_MARK`), then a 4-line selection window (message 20): 0 = Fish, 1 = `_GOTO_FP_CHANGE` (menu_mode 8, the FP exchange), 2 = `_GOTO_FISH_RANKING` (menu_mode 10, the fish log), 3 = leave. "Fish" requires key item **185 "Fishing Rod"** (`_SITEM_CHECK(185)`; granted by `gedit/e01/in/i01h05/event.stb` function 200 `_SITEM_GET(185)`); without it message 21.

### 1.3 Start-up script (internal function, e.g. `fn_e5d8` in e01) (READ)
`_INITIALIZE; _FADE_OUT(30); _CLEAR_VILLAGER_BUFF; _NPC_DRAW(0,-1)` (hide the walking hero), **`_LOAD_MAIN_CHARA("chara/c01d_turi.chr","c01d_turi.cfg",1)`** (Toan with the rod; `c01d.chr` is restored on exit), `_LOAD_VOICE_SET(6)`, then
- `_LOAD_FISHING_DATA(set, ...)` = `FishingInit; FishingLoad(texblock 8); FishingLoadFish(set, tex 0x36)`; rect via `FishingSetRect` (stored in `fishing_rect` 0x1d549d0 = max corner, 0x1d549e0 = min corner, y from -1000 to 1000); `FishingSetWaterLevel(water, ground)`; the collision polygons of the Georama ground inside the rect (`CEditGround::PickUpPoly`, max 0x400 polys, stored by `FishingSetCPoly`) are used for the fish/hook/float floor collision;
- `_INIT_FISH(x0,z0,x1,z1)` = `FishingInitFish`: remembers the fish area `fish_rect` and puts all fish at its centre, y = water - 12;
- `_SET_NPC_POS/ROT`, `_GOTO_FISHING` (ext 997) and `_FADE_IN(60)`.
`_GOTO_FISHING` = `FishLineInit(rod tip position)` (frame `"sao"` of the hero's model) and `DAT_01d3d618 = 0xb`; `EventMode` case 0xb sets **GameMode = 0x10**. From then on `MainMode` runs `EdMoveChara`, `FishingDrawCheck` (GameMode 0x10, or 9 with oldGameMode 0x10 = pause) draws line and fish; message window `0x1d1b550` is drawn; START/pause works (GameMode 9 with `old_mode` restore).
**Leaving**: in state 1 (idle) Circle (0x20) sets event id 0x85 = **133** and Square (0x80) event **134**, which are run as town event functions (`RunEvent(DAT_01d19708)`):
- 133 (Circle): 2-line window (message 22): 0 `Continue fishing` -> `_SET_RETURN_CODE(11)` (back to GameMode 0x10); 1 `Quit fishing` -> `fn_ee30` (`_FADE_OUT(30)`, restore `chara/c01d.chr` / `info.cfg`, **`_EXIT_FISHING`** = `FishingExit`: returns the equipped bait to the bag (`EdGetItem(bait,1,-1)`), re-initialises the 6 fish, `FishingInit`) and `fn_a3ec` (fade in, return to town walking).
- 134 (Square, change bait): `_GOTO_CHANGE_ESA(&result, ...)` = `p_use_item = &result; EdUseItemInit; EdSetUseItem(list at 0x269ae0); menu_mode = 9` -> the "event item select" screen filtered to the **13 baits** `193 197 199 166 167 168 169 170 186 187 188 189 190` (the 8 ids the script puts in its arguments are ignored; this is the static list `@820` 0x269ae0, DATA). The chosen item is **removed from the bag** (flag); `v0 > 0` -> `_LOAD_ITEM_FILE(v0)`, `_LOAD_ITEM(0)` (loads the item model into the event buffer 0 = `DAT_01d3d42c`), `_SET_FISHING_ESA(v0)` = `FishingLoadEsa(item, EsaFrame = DAT_01d3d42c, texblock 0x28)` (first returns the previously attached bait to the bag; `esa_type` = index of the item in `esa_info`), then `_GOTO_FISHING` again (re-enters mode 0x10; `chara_fishing` restarts at 0, fish positions persist, fish AI states are reset by `FishingInitFishStatus`).

### 1.4 Loaded files (DATA)
- `chara/fishing.pak` (FishingLoad: `hari.mds` = hook model -> `HookFrame`, `uki.mds` = float model -> `UkiFrame` (given frame attribute 0x200), `sao.chr` = rod model; the hero's own `maru_uki` / `turibari` meshes are hidden with attribute 2, the rod/float/hook come from these separate meshes and the live rope).
- `chara/c01d_turi.chr` (hero with rod; motion keys below), cfg inside the pack `c01d_turi.cfg`; `chara/f00s.chr` (+`info.cfg`): the single **silhouette fish model** used for every fish under water (texture block `fish_texb` = 0x36, scaled per fish); the real fish model `chara/f%02da.chr` (`name_419` table 0x296570: kind k -> f(k+1)a; **f09a does not exist** (kind 8 is a dummy)) is read in the background only when a fish is hooked (state 7, `StartReadBG; LoadFileBG`) and attached by `FishingBattleToAngleFish` (texture `fish_texb + 1`, `info.cfg` of that pack). Sound set 0x2f (`SndSPSeLoad`).
- Menu art: `commenu/a_<lang>/fishmenu.pac` (`fishing.img`, `fishmes.bin`: FP exchange, messages 201-217 + item descriptions 500+), `fishrec.pak` (`fishrec.img` + same `fishmes.bin`: fish log). System help text: `gedit/system/editsys.bin` messages 200-203, 210 (below). Fish names: `meswin/_systeme.bin` messages 30-37 and 40-49 (via `FishMsg`, below).

### 1.5 Controls (READ, `EdMoveChara`; pad bits: 0x40 Cross, 0x20 Circle, 0x80 Square, stick LX)
| state (`chara_fishing`) | meaning | input |
|---|---|---|
| 0 | init: `FishingInitFishStatus`, no angle fish | -> 1 |
| 1 | **idle, rod ready**; the hero can still **walk** with the stick (movement is zeroed for state >= 2) inside the pond rectangle (`FishingPickUpPoly` adds the 8 triangles of the `fishing_rect` box as collision polygons for the hero: invisible walls, INFERRED); after 0x78 frames the help bar shows message 201 (`Cast / Change Bait / Quit : <bait name>`) or 200 (`... :No hook`) | Cross -> 2 (cast); Square -> event 134; Circle -> event 133 |
| 2 | start cast: hero motion 4 | -> 3 |
| 3 | cast animation; at frame 90 SE 400; when the motion ends (`+0xc70 == 3`) -> 4 | |
| 4 | **line out / waiting**. Hero motion 5 (idle), motion 0xe after 600 frames, 0xf at 1200 (cosmetic). Left stick X turns the hero (`yaw = AngleInterpolate(yaw, yaw + 0.1*(-LX), 0.01)`) which drags the rod and with it the line; camera follows the mid point float/hero. Bite check each frame: `FishingFishStatus` 7 -> state 6, 8 -> state 7 | Cross -> 5 (reel in); also forced to 5 after 30 frames when `FishingCheckUkiHook` (float or hook outside the pond rect, or hook/float higher than water + 5) |
| 5 | reel in: hero motion 7, the float is pinned to the rod tip with a lerp that grows 0.02 per frame after frame 30 (SE 402 when the hook leaves the water); at the end of the motion -> 1 | |
| 6 | **a fish is nibbling** (status 7): the hook is jerked by `FishPullHook(-0.15*rnd)` each frame (below -0.1: ripple effect + pad vibration), help message 202/203 not shown (the AngleHelp function is an empty stub). Fish status no longer 7 -> 4; status 8 -> 7 | **Cross too early -> 9**: reel in, SE 402, **20 % chance the bait is lost** (`FishingDeleteEsa`, message 210 `Bait ran out.`) |
| 7 | **a fish is hooked** (status 8): `FishPullHook(-(0.1 + 0.05*rnd))`, pad vibration; | **Cross -> 10** (SE 400, hero motion 0xc; `FishingBattleFish(index)`, `SetBattleMode`, starts the background read of the fish model). If the status leaves 8 (fish got away) -> 4 and **30 % chance the bait is lost** |
| 8 | (set after state 10) BGM fade out (0x3c frames), hero motion 10, `fishing_mes = 0` -> 0xb | |
| 9 | reel-in after a mistake, back to 1 at motion end (FishingAngleFish(-1), status reset) | |
| 10 | **"fight"**: the bait is deleted (`FishingDeleteEsa`), constant random pad vibration; after >= 0x78 frames and when the background read is done the fish model is attached (`FishingBattleToAngleFish`), SE 403, -> 8. **No tension, no failure**: once Cross was pressed in state 7 the catch is certain | |
| 0xb | catch presentation: camera angle = hero yaw, height 10, distance 40; `fishing_mes` 0: `FishingGetAngleFishSize` -> bookkeeping (section 3) + `MakeMesWin(2000)`; 1: wait for the message, 2: BGM fade in, -> 0xc, hero motion 9 | |
| 0xc | release: at frame 0x78 the fish disappears (SE 404), at motion end -> state 0 (fish AI reset, bait is gone: the player must pick a new bait; with `esa_type < 0` the cast still works but nothing can bite, see 2.4) | |

Hero motions of `c01d_turi.chr` (DATA: `KEY start,end,speed` of `c01d_turi.cfg`): 0 (25-35) stand, 3 (10-20), 4 (40-80, 0.27) cast, 5 (214-224) waiting, 6 (225-240) fast reel-in, 7 (80-105) reel, 8 (110-120), 9 (125-163) show fish/joy, 10 (170-185), 0xc (245-320... `KEY 245,320`? index 11/12 are 245-320 and 325-370: 0xb, 0xc = fight/catch), 0xd/0xe/0xf (245-255 turn, 258-278, 279-320). The mapping index -> meaning above is **INFERRED** from the state machine only.

SEs (`SndSePlay`): 400 (0x190) cast / hook-set, 401 splash (float entering water, with 4 `EffectHamon` ripples), 402 (0x192) reel/line, 403 (0x193) fish pulled out, 404 (0x194) fish released. Camera: `SetHeight(40)` every frame; during 4/6/7/10 etc. `SetFollow` on the average of float and hero position.

## 2. The fish

### 2.1 Fish table `fish_info` (0x296050, 18 records x 0x48 bytes, DATA)
Record = `{float ref_len, float min_len, float max_len, int fp_min, int fp_max, float attract[13]}`; copied into the `CFish` at +4. `attract[b]` is indexed by the bait index `esa_type` (table in 2.4). Name = message `FishMsg[id] + 30` in `_systeme.bin` (`FishMsg` 0x292aa0 = `10..19, 0..7`). Mardan/Garayan = ids **5 and 17** (the only two with `case 5 / 0x11` in the catch code).

| id | name (msg) | file | ref len | min len | max len | FP min | FP max | attraction per bait idx 0..12 |
|---|---|---|---|---|---|---|---|---|
| 0 | Bobo (40) | f01a | 17 | 10 | 20 | 20 | 50 | 1 0 0 0.2 0.2 0.2 0 0.2 0 0 0 0 0 |
| 1 | Gobbler (41) | f02a | 19.5 | 8 | 16 | 10 | 30 | 0 0.5 0.5 0.2 0.2 0.2 0 0.2 0 0 0.5 0.5 1 |
| 2 | Nonky (42) | f03a | 25.5 | 8 | 19 | 8 | 25 | 0 0.5 0.5 0.2 0.2 0.2 0 0.2 0 1 0.5 0.5 0 |
| 3 | Kaji (43) | f04a | 35.4 | 12 | 24 | 30 | 60 | 0.5 0 0 0.2 0.2 0.2 0 0.2 0 0 0 0 1 |
| 4 | BakuBaku (44) | f05a | 28 | 8 | 16 | 10 | 40 | 0 0.5 0.5 0.2 0.2 0.2 0 0.2 0 0 0.5 0.5 1 |
| 5 | **Mardan Garayan** (45) | f06a | 21 | 10 | 16 | 200 | 400 | 0 0 0 0 0 0 **1** 0 0 0 0 0 0 |
| 6 | Gummy (46) | f07a | 20 | 6 | 12 | 15 | 40 | 0 1 0.5 0.2 0.2 0.2 0 0.2 0 0.5 0.5 0.5 0 |
| 7 | Niler (47) | f08a | 20 | 6 | 10 | 20 | 50 | 0 0.5 1 0.2 0.2 0.2 0 0.2 0 0.5 0.5 0.5 0 |
| 8 | (dummy, msg 48, no model f09a, never spawned) | - | 1 | 1 | 2 | 1 | 2 | all 0 |
| 9 | Umadakara (49) | f10a | 21 | 8 | 16 | 100 | 200 | 0 0 0 0 0 0 0 0 1 0 0 0 0 |
| 10 | Tarton (30) | f11a | 20 | 8 | 16 | 40 | 80 | 0 0.5 0.5 0.2 0.2 0.2 0 0.2 0 0.5 1 0.5 0.5 |
| 11 | Piccoly (31) | f12a | 20 | 6 | 12 | 20 | 40 | 1 0.5 0.5 0.2 0.2 0.2 0 0.2 0 0 0.5 0.5 0 |
| 12 | Bon (32) | f13a | 15 | 6 | 12 | 10 | 30 | 1 0.5 0.5 0.2 0.2 0.2 0 0.2 0 0 0.5 0.5 0 |
| 13 | Hamahama (33) | f14a | 21 | 10 | 20 | 35 | 70 | 0 0 0 0.2 0.2 0.2 0 0.2 0 0 0 0 1 |
| 14 | Negie (34) | f15a | 21 | 8 | 16 | 15 | 40 | 0 0 0 0.2 0.2 0.2 0 0.2 0 1 0.5 0.5 0 |
| 15 | Den (35) | f16a | 20 | 8 | 20 | 20 | 40 | 0 0 0 0.2 0.2 0.2 0 0.2 0 0.5 1 0.5 0 |
| 16 | Heela (36) | f17a | 20 | 8 | 14 | 20 | 40 | 0 0 0 0.2 0.2 0.2 0 0.2 0 0.5 0.5 1 1 |
| 17 | **Baron Garayan** (37) | f18a | 21 | 10 | 30 | 600 | 1000 | 0 0 0 0 0 0 0 0 0 0.5 0 0 0 |

Notes: the exchange/fish-log menu uses 6 "families" (`fishmes.bin` 210-215: Bobo, Gobbler, Nonky, Kaji, Baku Baku, MardanGarayan) only as names of weapon/exchange items, not for the 18 kinds. The fish log, catch window and FP exchange all use the 18 names above.

### 2.2 Which fish live in which pond (`FishingLoadFish`, 0x1a88f0, READ)
`Fish` = array of 6 `CFish` (0x2410 bytes each, `Alloc 0xd8c`... but only `FishNum` are used: **5**, or **4 for sets 0 and 4**). For every slot, using `rand()`:
- time factor `N` from the clock (`EdGetTime(SaveData+0x1d0)`, 4 periods; hours: period 0 = [11.5, 2.5) wrapping, 1 = [2.5, 5.5), 2 = [5.5, 8.5), 3 = [8.5, 11.5)): period 0 -> N = 50, 1 -> 20, 2 -> 35, 3 -> 25 (default 30).
- **set 0** (Norune): `floor(3.99999 * rnd)`: 0 -> kind 1, 1 -> 2, 2 -> 6, 3 -> 7 (Gobbler, Nonky, Gummy, Niler; 25 % each).
- **set 1** (Matataki pond 0): `r = int(100*rnd)`: r < 35 -> kind 1; < 70 -> 4; < 80 -> 9; < 100 -> 10 (Gobbler 35 %, BakuBaku 35 %, Umadakara 10 %, Tarton 20 %).
- **set 2** (Matataki pond 1): `rand() % N == 0` -> rare slot: `rand() % 5 == 0` -> kind 17 (Baron Garayan) else kind 5 (Mardan Garayan); else `rand() % 3`: 0 -> kind 2, 1 -> 4, 2 -> 6.
- **set 3** (s09): `rand() % 100`: < 20 -> 0; < 40 -> 3; < 60 -> 11; < 80 -> 12; else 13.
- **set 4** (Queens): rare slot as set 2 (1/N, then 20 % Baron / 80 % Mardan); else `rand() % 100`: < 40 -> 14; < 70 -> 15; else 16.
So Mardan Garayan only appears in the Matataki pond 1 and Queens (per slot chance 1/N * 80 %; with 4-5 slots and N = 20..50 that is roughly 6-20 % that at least one is in the pond; the pond is re-rolled each time `_LOAD_FISHING_DATA` runs = every visit). Fish 9 (Umadakara, bait Carrot 186) appears only in set 1; fish 17 only in sets 2/4.
`LoadFish(fish, kind, tex, alloc, ...)`: loads the silhouette `f00s.chr`, copies the 0x48 byte record, `SetScale`, `fish[0] = kind`.

### 2.3 Size and fishing-point value (READ)
`CFish::SetScale` (0x240d60): `r = nrnd()` (sum of 12 `rnd()` minus 6, ~N(0,1)); `len = min_len + r * (max_len - min_len) / 4` if r >= 0, else `min_len + r * (max_len - min_len) / 8`; clamped to `[0.5*min_len, max_len]`. Model scale = `len / ref_len`; collision radius value = `len / 25`.
`CFish::GetFP` (0x240e80): if `len >= min`: `fp_min + (fp_max - fp_min) * (len - min) / (max - min)`; else `fp_min - (fp_min * (min - len) / (min - 0.5*min)) * 0.5`; truncated to int.
Size reported/stored/displayed = `(int)(len * 10)` (the catch window prints it followed by "cm"; the log prints the same integer).

### 2.4 Baits (`esa_info` 0x26ae90, 13 x {item id, radius}, DATA; item names from `work/tables/items.csv`)
| bait idx | item id | name | attract radius |
|---|---|---|---|
| 0 | 193 | Evy | 50 |
| 1 | 197 | Mimi | 25 |
| 2 | 199 | Prickly | 25 |
| 3 | 166 | Throbbing Cherry | 25 |
| 4 | 167 | Gooey Peach | 25 |
| 5 | 168 | Bomb Nuts | 25 |
| 6 | 169 | Poisonous Apple | 25 (**the only bait Mardan Garayan wants**) |
| 7 | 170 | Mellow Banana | 25 |
| 8 | 186 | Carrot | 25 (only Umadakara f10a) |
| 9 | 187 | Potato cake | 25 |
| 10 | 188 | Minon | 25 |
| 11 | 189 | Battan | 25 |
| 12 | 190 | Petite Fish | 42 |
The bait is chosen with the item list of 1.3 (the bag is `UserStatus+0x4360`); it is taken out of the bag when selected and given back when the bait is changed or fishing is exited; it is destroyed by the 20 %/30 % rules of states 6/7 and unconditionally at the start of the fight (state 10). One rod only (key item 185, no rod types in the code). The bait's pull is `attract[bait idx]` of each fish (table 2.1), radius = column above. With no bait (`esa_type < 0` or no `EsaFrame`) `SetFoodPos(0, ..., -1)` is used and `FindFood` is always false: **nothing bites without a bait**.

### 2.5 Fish AI (`CFish::Step` 0x240480, per frame per fish, READ)
Fields: `+0x4c` move mode (0 stand/glide, 1 swim, 10 = shown on the hook, 0xb = battle), `+0x50` bite state (-1 none, 6 interested in bait, 7 nibbling, 8 hooked, 9 leaving, 0xb battle), `+0x54` move timer, `+0x58` state timer, `+0x74` target heading, `+0x80` target speed, `+0x84` speed, `+0x88` bait idx (-1 none), `+0x8c` bait radius, `+0x90` bait (hook) position (hook y - 3, set each frame by `FishingStepFish` with `GetHookPos`).
- **Bite state machine**: evaluated when `state == -1` (every frame) or when the state timer `< 0`; on evaluation `timer = rand()%30 + 20` (20..49 frames; `GetActCnt2`) and:
  - state -1: `FindFood` -> 6. `FindFood`: bait idx >= 0, radius > 0, 3D distance(hook, fish) <= radius, and `rand_check(0.3 * attract[bait])` (`rand_check(p)`: `rand()%1000 < p*1000`).
  - state 6: `EatFood` -> 7 (distance <= 1.0 and `rand_check(0.5 * attract)`); `LeaveFood` -> 9 (`rand_check(0.1)`, timer halved; LeaveFood is true at once when there is no bait). Both can fire; Leave is evaluated last and wins.
  - state 7: `BiteHook` -> 8 (identical test to EatFood); `LeaveHook` -> 9 (`rand_check(0.1)`, wins).
  - state 8: -> 9 with `timer *= 2` (so a hooked fish stays hooked **20..49 frames** = the reaction window, 0.33-0.8 s; then it goes into leave mode, which lasts 40..98 frames, then back to -1).
  - state 9: -> -1.
  State 0xb (battle) is set by `SetBattleMode` (move mode 0xb).
- **Movement** (all in the horizontal plane, y fixed at water - 12; the hook hangs ~8.3 below the float, so it is ~depth of the fish and the 3D distance test is effectively 2D): `move timer` expires every `rand()%100+20` frames (+30 when moving): mode = 1 first time then `rand()&1` (swim or stand); if swimming: heading = `AngleLimit(2*pi*rnd)`, target speed `0.2 + 0.5*rnd/2`... (`DAT_002a1a8c + rnd*0.5` = 0.2-0.45). State 6: heading toward the bait (`atan2(dx, dz)`), target speed 0.1, speed 0 within distance 1. State 7: face the bait, speed 0. State 8: face the bait +- 0.2*rnd, speed 0, motion 1 (thrash). State 9: swim away (heading + pi), speed 0.5 (the code sets `+0x80 = 0x3f000000`), state 0xb: random heading, speed 0.5, turn rate and acceleration doubled. Turn: `yaw = AngleInterpolate(yaw, target, 0.05)`; speed ramps by 0.01 per frame toward the target; position += (sin yaw, 0, cos yaw) * speed. Motion: move mode 0 -> motion 6, 1 -> motion 0, 2 -> motion 1, states 8/0xb -> motion 1 (f0Xa/f00s motions: 0 swim 10-24 @0.25, 1/2 fast swim, 3 .. 5, 6 idle glide 173-187 @0.2).
- **Walls**: each fish keeps its pond-floor polygons (`SetCPoly`) and does `CheckHit` on its movement segment (blocked moves are cancelled) plus `CheckWidth` with radius `2*scale`, so fish stay inside the lake mesh; no explicit fish rect clamp beyond that (the rect is only the initial spawn centre).
- Only the hooked fish matters for the player; `FishingFishStatus` returns 8 if any fish is in state 8 (index of that fish), else 7 if any is in state 7, else -1 (battle 0xb is returned too but only used by the camera code).

### 2.6 Line, float and hook physics (`FishLineInit/Step/Draw`, READ; presentation only)
There is **no tension value**. The rope is just visuals + the bite geometry: `point[24]` (0x1d55e30) Verlet rope, segment length `distp = 1.6667`; point 0 is pinned to the rod tip each frame (`FishLineStep(rod tip, ...)`), point 18 (`0x1d55f50`) is the float anchor, point 23 (`0x1d55fa0`) the hook anchor. A 4-point tetrahedron `ukip[4]` (6 links `uki_link`) follows point 18 for the float; a 3-point triangle `hookp[3]` (3 links `hook_link`) follows point 23. Per frame: add velocity, gravity -0.1 to y velocity of every point, 16 relaxation iterations (rope links 0.5/0.5, last link pulls more: factor 0.9), float/hook anchor coupling, optional pinning (`set_uki_pos` / `set_hook_pos` with lerp factor: 1.0 in state 1, a ramp in 5 and 9, -1 = free in 2/3/4/6/7), then velocity = (p - old_p), damping x0.96 (rope; x0.999 for point 23), ground clamp at `LineGroundLevel = min(UkiGroundLevel, HookGroundLevel)` (= pond `ground_y` or the first collision polygon below float/hook, probed 40 above), x0.4 on contact; below the water surface x0.4 damping and buoyancy +0.1 (+0.3 for the float tetrahedron points with a depth-proportional term, damping 0.6, extra +0.3 y up when submerged). `pull_hook` is added to the hook anchor y each frame (the nibble/bite tugs). Splash/ripple (`EffectHamon`) when the float crosses the surface. Drawing: 24 GS line-strip points (`FishLineDraw(0)` under the water surface, `(1)` above) plus the uki/hari meshes oriented along the line.
**How the cast works**: there is no launch velocity in the code; the rope is free while the cast animation (motion 4, 40-80 frames at 0.27 = ~150 ticks) swings the rod tip (the free rope inherits the tip motion). Max reach is the rope length ~ 23 x 1.667 = 38 units from the tip plus the hook drop. The pond rectangle is +-100 and the hero stands at the edge of it. Pressing Cross while the float/hook is outside the rect, or hook/float above water + 5 after 30 frames, reels in automatically.

## 3. Rewards and bookkeeping (state 0xb, `EdMoveChara`, READ)
On catching (`FishingGetAngleFishSize(&size10, &fp)` -> `size10 = (int)(len*10)`, `fp = GetFP`, returns kind):
1. If kind is 5 or 17 (Mardan/Baron Garayan): `SaveData+0x12f94 += 1` (**lifetime Mardan flag/count**, read by `GetMardanGareyanFlag`, `_CHECK_MARDAN` ext 1086 in all 7 dungeon event scripts function 15 (story check) and by the fish log (golden icon when != 0)) and `SetFishMardanGarayanNum(+1)` = game-int flag **0x14** (`SaveData+0x10668 + 0x14*4`), clamped 0..10000: **the current counter used for weapon build-up**.
2. `AddFishingPoint(fp)`: `SaveData+0x1dc += fp`, clamped 0..9999.
3. `SetFishingRank(size10, kind)` (`CSaveData::SetFishingRank` 0x157dc0): record list of **64 slots x 0x10 bytes at `SaveData+0x1e0`** = `{int kind, float size, 8 bytes unused}`; takes the first empty slot (size <= 0), otherwise **overwrites the smallest entry without comparing it to the new size** (so a tiny catch can replace a larger record once all 64 slots are used; original behaviour), then bubble-sorts all 64 by size descending. `GetFishingRank(i)` returns a pointer to the record or 0 if empty.
4. Catch window `MakeMesWin(2000)` in the town's `talk` bank with variables name (`GetFishMsgNo(kind)`), size, FP gained, total FP; the fish is NOT added to the item bag: **fish are never items**. The only inventory effect is the bait removal.
5. Fishing points (FP) are spent only at the "Exchange FP" menu (menu mode 8, `FishingExchangeKey` 0x1f1880; list `exitemlst` 0x2929d0, see `notes/menus_screens.md` 6.3; Mardan weapon 278 costs 2500 FP and takes the last row).

### 3.1 Fish log (menu mode 10, `InitFishRecordView` 0x1f2e80 ... `FishRecordViewBoard` 0x1f35f0)
Shows the **top 20** of the 64 records (5 rows visible, `FishRecordMenu` 0..19, d-pad +-1, shoulder keys +-5), each row: rank number, size number (`(int)record.size`), fish picture/name: the selected row's name is shown in a message window (`MakeMesWin(200)` with the 5 name message ids from `GetFishMsgNo(record.kind)`); a trophy icon at the lower right appears when `SaveData+0x12f94 != 0`. Circle closes. No other data (no per-species best table, no catch counts).

### 3.2 Mardan Eins / Twei (weapon build-up tie-in) (READ)
- Fish counter = game int flag 0x14 (`GetFishMardanGarayanNum`, `SetFishMardanGarayanNum`, `ClearFishMardanGarayanNum` = set to 0).
- Weapon **278 (0x116) MardanEins** and **279 (0x117) MardanTwei** are Toan weapons. `EnableBuildUpModelSpecial` (0x2353a0): for a weapon with id 278 or 279 the build-up result is `id + 1` and it is allowed only if `GetFishMardanGarayanNum() >= threshold[id]` with the table `@433` (0x2a21d0, DATA) `{278: 5, 279: 15}`. `CWeaponLevelUp::SetBuildUp` (0x236c30) then calls `ClearFishMardanGarayanNum` (counter back to 0). So Eins -> Twei costs 5 Mardan/Baron Garayan catches, Twei -> next (280 AriseMardan) costs 15 more; the counter is not consumed by anything else.
- `AlreadyGetMardanWeapon` (0x1f13a0): game flag **0xca** (`SetAlreadyGetMardanWeapon(1)`): flag 0 -> returns 2 if `SaveData+0x12f94 <= 0` (weapon not offered; row hidden) else 1 (offered in the exchange as the extra last row, `GetExchangeItemList(0x20)`, 2500 FP; the number of normal rows is `DngStatus+5` (party size) + 0x19..0x1a); flag 1 -> 0 (already bought, hidden). Buying (`FishingExchangeKey`): `GetItem(278)`, FP deducted, `SetAlreadyGetMardanWeapon(1)`, `ClearFishMardanGarayanNum`.
- Story use: `_CHECK_MARDAN` in dungeon event function 15 returns `SaveData+0x12f94` (see `notes/dungeon_events.md` row 1086).

## 4. Prioritised implementation plan (what to read, what to simplify)
Data tables to read straight from the ELF (all in `SCUS_971.11`; I can also dump them to a CSV with a small script): `fish_info` 0x296050 (18 x 0x48), `esa_info` 0x26ae90 (13 x 8), bait-select id list 0x269ae0, `FishMsg` 0x292aa0 (18 bytes), `name_419` 0x296570 (file names), `exitemlst` 0x2929d0 (35 x {id,cost}), build-up thresholds 0x2a21d0 {5,15}. Fish set tables 2.2 are code constants (copy them). Per-map pond parameters come from the scripts (1.1): implement them as data (map, set, rect from the Georama lake part or fixed numbers, water, ground, fish area, start position/yaw). The fish files `chara/f00s.chr` and `f01a..f18a.chr` are normal characters (`dccore::load_character`).

Priority order:
1. **Data + bookkeeping (no 3D)**: `CSaveData` accessors (FP at +0x1dc cap 9999; rank list 64 x 16 bytes at +0x1e0; Mardan lifetime int at +0x12f94; game int 0x14; flags 0xca), `GetFP`/`SetScale`/size rules, catch -> bookkeeping (section 3), build-up threshold, exchange list. A headless **simulation** (a function `fish_step`, 6 fish, hook position input) can be tested immediately: the AI is 25 lines.
2. **Fish AI** exactly as in 2.5 (use the game's `rand()`/fdlibm trig already in `dccore`, `rnd = rand()/2147483648.0f`). Exact reproduction of the rand sequence needs the PS2 `rand`; not required for gameplay.
3. **Scripts**: the 4 town maps' functions 11/12/133/134 + the start-up functions run on the existing script VM if ext functions 24/25/26/994-998 are implemented as hooks (`_LOAD_FISHING_DATA`, `_INIT_FISH`, `_GOTO_FISHING`, `_EXIT_FISHING`, `_SET_FISHING_ESA`, `_GOTO_FP_CHANGE`, `_GOTO_FISH_RANKING`, `_GOTO_CHANGE_ESA`). Need the menus: item-select with the 13 bait ids, FP exchange and fish log (spec in `notes/menus_screens.md` 6.3 + 3.1).
4. **The hero state machine** (table in 1.5) with simple presentation: hero with `c01d_turi.chr`, motions 0/4/5/7/..., a float entity.
5. **Line/float**: simplify. Replace the Verlet rope + tetrahedra by: during cast, a ballistic float arc from the rod tip to a point `reach = 25..35` units in front of the hero (yaw from the hero, adjustable with LX in state 4), landing at water level; the hook hangs `8.3` below the float (so 3D distance to fish at water-12 is ~horizontal); float bobbing by `pull_hook`; reel in with a lerp to the tip. The only gameplay-relevant outputs are the hook position (for the fish AI) and `FishingCheckUkiHook` (out of rect -> auto reel in). Keep `LineGroundLevel` simple (pond ground_y). A rope renderer can be a polyline of 24 interpolated points if wanted.
6. **Fish rendering**: one silhouette model (`f00s`) tinted/dark, scaled `len/ref_len`, drawn only below the water surface (except set 3: not drawn); the real model only for the catch presentation (`f%02da.chr`, motion 1 thrash, shown at the rod hook, `FishingDrawFish` angle mode).
7. Polish: help bar messages 200/201/210 (`gedit/system/editsys.bin`), SEs 400-404, BGM fade during the catch, camera (height 40 normal; catch: height 10, distance 40/30, angle = hero yaw), pad vibration (state 6/7/10), ripple effects.

Can be simplified safely: rope physics, per-polygon fish collision (use the lake polygon / rect), `rand` sequence, Georama-lake rect (use fixed per-map numbers), the SE/vibration, the camera blending, the dummy kind 8 and the unused `AngleFish`/`BattleFish` object copy (a single fish object is enough).

## 5. Open questions / not verified
1. Which event point/trigger starts function 11 (e01) / 12 (e04, s09) / `fn_f674` (e02) and whether it exists only when the Georama lake part is placed (the pond rect is computed from `_GET_EDIT_PARTS_POS(12 or 11)` which is the LAKE part's position; if the lake is not built the position is probably invalid). The `mapinfo.cfg` parse showed e01 part 6 (`e01h05`, the house where the rod is received) carries events [10, 11]: event 11 is the board; e04/e02 use `TALK_EVENT 11/12` on an NPC.
2. Pad-bit naming: script `_GET_PADDOWN & 32` and `EdMoveChara` `EdPadDown(0x40)` are both "OK"; I assume 0x40 = Cross, 0x20 = Circle as in the help text glyphs (`{fd08}` Cast, `{fd06}` Quit).
3. Exact cast launch: not modelled in code (emerges from the rod animation + rope). Real max reach/landing zone need a live trace (PINE read of `point[18]` after a cast) if the port wants the original feel.
4. The size is printed as "cm" in the window; the stored integer is `len*10` (e.g. Bobo 100-200). The unit conversion (maybe the number is shown with a decimal point) was not decoded.
5. `EdFishingAngleHelpMEs` is an empty stub in this build, so messages 202/203 (`Now!!`) are never shown; `fishmes.bin` 206-209 `Fishing Point N pt.` (digit-count variants of the FP counter in the menu).
6. `FishingFishStatus` returns the index of the LAST fish for state 7 (loop writes the index each iteration), only the 8 case returns the right index; the port should pass the index of the hooked fish for the fight.
7. Time-of-day period numbers 0..3 -> which clock names (afternoon/dusk/night/morning) I did not map; only the clock ranges are given.
8. Whether `rand_check` etc. use the game's global `rand()` (libc, 31-bit, the 2.1474836e9 divisor) is READ; reproducing the exact sequence is not needed.

## Native front end (dc_townfish.cpp, ModeFish of DcTownWalk)
- `_GOTO_FISHING` (nav code 0xb, EvNav::Fishing) enters ModeFish; the hero keeps its rod model, events 133 (quit menu) and 134 (change bait, modal kind 9) are raised from state 1 with Circle / Square, and `_GOTO_CHANGE_ESA` -> `_SET_FISHING_ESA` -> `_GOTO_FISHING` re-enters with `bait_changed` and calls `fish_set_bait`.
- States: 0 init, 1 idle, 2/3 cast, 4 line out, 6 interested, 7 hooked (press to pull), 5/9 reel in, 10 fight, 11 catch message. The hook hangs `kHookDrop = 9` below the float so that the bait (hook - 3) is at the depth of the fish (water - 12); with 8 the fish approach (bite 6) but `dist <= 1` never holds and nothing bites.
- Verified with `--fish-auto 1` in e01 (Norune pond): cast, bite, hook, catch "Nonky (11.1cm) caught!! Fishing Points +12 Total 12"; the record list and fishing points are stored through `register_catch`.
- Exchange (modal kind 8): `exitemlst` at 0x2929d0 is 35 entries of **int16 pairs {item, cost} with a 4-byte stride** (not int32 pairs); list, buy, not-enough-points verified. Test flag `--fp N`.
- Record list (modal kind 10) verified with `--fish-records kind:size,...` (sorted, names from the system text). Bait return on leaving is coded (`fish_leave`). Not checked: the other ponds (e02, e04, s09; they need a placed lake).
