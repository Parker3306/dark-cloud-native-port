# Villagers of the Georama towns: selection, placement, walking, talking

Scope: e01..e05 (outdoors, `GameMode` 1 walking) and the house interiors. Companion of `notes/georama.md` (formats), `notes/georama_edit.md` 2.7 (presence rule, `EditCharaData`) and `notes/events.md` (event VM, talk mode).
Evidence tags: **[R]** read from the decompile / disassembly (`work/decomp/all_dun.c`, addresses are main-ELF), **[VD]** checked against the disc data (tool: `python -I tools/dc/villagers.py [e0N] [--persons] [--interiors]`), **[I]** inferred, **[?]** open.
Tick = one `MoveChara`/`EditLoop` pass (60 Hz in the native port); lengths in game units (edit-grid cell = 100).

## 0. Headline findings

1. **There are no waypoint / route tables.** `moves` (0/1) and `speed` (float, units per tick) are the only movement data. A villager that "moves" does a **random walk on the edit grid**, one 100-unit cell at a time, preferring roads. Nothing is stored per time of day.
2. **Only half of the outdoor villagers walk.** Each time the outdoor set is (re)built, `floor(n/2)` of the n present villagers are pinned to their standing point (`moves==0` ones first, then random others); the rest of those with `moves==1` roam. Roamers start at a random free cell near their own house (not at the `person` point).
3. **Who is outdoors is random in e01..e04** (`PEOPLE_LIST` reshuffles the four time-of-day rows at every map entry and overrides `TIME_TABLE`); only e05 (no `PEOPLE_LIST`) uses the fixed `TIME_TABLE`. The native port currently reads `TIME_TABLE` literally: wrong for e01..e04.
4. Villagers **fade in / out**: a villager is only drawn (and animated) while it is one of the two nearest in front of the camera AND within 150 units of the hero; alpha ramps 8/tick (0..128) [R]. Not verified in-game ([?], see 7.1).
5. Interior villagers are **static**: placed by the interior `event.stb` (`_LOAD_IN_VILLAGER`, fixed x,y,z,yaw), idle motion only. The interior `mapinfo` `PEOPLE` directive is a no-op [R `CommandPEOPLE` 0x19fec0].
6. `_APPEAR_VILLAGER_ON/OFF/IN/OUT` have **no visual effect** (no fade, no door walk); they only set a per-villager scheduling state read by `EdSelectVillager` / `EdCheckVillagerIn`. `_APPEAR_VILLAGER_MOVE` only overrides the `moves` flag and is never called by the disc scripts. There are no "appear points".
7. The cat is just villager 14 of Norune: `c04cat` outdoors (roams like the others), `c04pcat` in houses (static).

## 1. Data

### 1.1 `VILLAGER_INFO` (0x90 bytes) **[R]** (`CommandPEOPLE2` 0x177330, `LoadEditMapData` 0x174390, `EdSelectVillager` 0x1857e0)
Two arrays: `EDIT_MAP_INFO+0x18400` = the 16 villagers of the map in `PEOPLE2` order ("master" array), and `EdVillagerInfo` (0x1d329d0, 10 entries) = the villagers currently present (outdoors list; also the array `_LOAD_IN_VILLAGER` fills per slot).

| off | field | source |
|---|---|---|
| 0x00 | char model[0x40] (`pNNa`, `c04cat`...; empty = unused slot in the 10-array) | PEOPLE2 arg0, **overridden by `EditCharaData`** for rows 0..15 |
| 0x40 | active flag (1 = present in the current outdoor set) | set by `EdInitVilagerPosition` |
| 0x44 | villager number (index in the master array, 0..15) | PEOPLE2 order |
| 0x48 | parts_no (house part index, -1 = always) | `EditCharaData.part` |
| 0x4C | element index k (house element whose installed flag makes the villager appear) | `EditCharaData.element` |
| 0x50 | PEOPLE2 arg1 (always 0 on the disc; unused) | |
| 0x54 | **moves** (0 stand, 1 roam) | PEOPLE2 arg2, `EditCharaData.moves`; **rewritten** by `EdInitVilagerPosition` and by `_APPEAR_VILLAGER_MOVE` |
| 0x58 | **speed** units/tick (floats 0.08 .. 0.5, mostly 0.3) | PEOPLE2 arg3, `EditCharaData.speed` |
| 0x5C | hide_until_event | `EditCharaData` |
| 0x60/0x64 | TALK_EVENT a,b (default -1, 0) | `TALK_EVENT` |
| 0x68 | TALK_ROT (1 = do not turn toward the hero when talked to **inside an interior**) | `TALK_ROT` |
| 0x6C | TALK_DIR (1 = can be talked to from any side) | `TALK_DIR` |
| 0x70 / 0x80 | current standing position vec4 / rotation vec4 (0, yaw, 0) | `EdInitVilagerPosition` |

`EditCharaData` (ELF 0x267070, 20 rows x 0x14 bytes per town, see `georama_edit_tbl.py --chara`) is authoritative for rows 0..15; PEOPLE2 `moves`/`speed` equal it on the disc **[VD]**. Speeds: mostly 0.30; e01 p05a 0.17, p03a 0.5, p10a 0.2; e02 0.1 on four villagers; e04 0.1 and 0.08 (p58a). `moves == 0` rows: e01 v0, v11, v15; most of e03; e02 v4; e04 v8 (full table: `villagers.py`). The cat (e01 v14) is `moves 1` in EditCharaData although its PEOPLE2 row says 0: use the EditCharaData value.

### 1.2 Standing point = `person` function record (type 1, `georama.md` 3.4)
`+0x20` villager number, `+0x40` position and `+0x54` yaw relative to the placed part. `GetPeoplePos` (0x1a2d10): `world = frame transform of the part (pos, rotY) * rec.pos`; `yaw = AngleLimit(part frame yaw + rec.yaw)`; w = 1. One or two records per house part **[VD]** (e.g. e01 house 1 holds villagers 2 and 3). Returns 0 (villager hidden) when the part is not placed. e05 records are all (0,0,0). Dump: `villagers.py --persons`.

### 1.3 Schedule
* `TIME_TABLE_NO 0` (week 0 only) + 4x `TIME_TABLE slot v0..v5` (slot 0 day, 1 dusk, 2 night, 3 morning; 6 villager numbers) -> `EDIT_MAP_INFO+0x18d00 + slot*0x40` (16 ints, -1 padded). [VD] dump gives them per town.
* `PEOPLE_LIST` (15 ints) -> `+0x1a200` (initialised to -1). `EdCreateVillagerTable` (0x185320, called from `EditInit` before `EdInitVillagerTable`): n = number of entries >= 0; if n > 0, for each of the 4 slots do 100 random swaps of the n entries (the same array keeps being shuffled) and write the first 10 into the slot row, **replacing TIME_TABLE**. Present in e01 (11 villagers 1,2,3,5..12), e02, e03, e04; absent in e05 **[VD]**. So outdoors in e01..e04 the 6 present villagers are a random subset of the people list, different for each of the four day phases, new at each map entry. `rand()` state is saved/restored around `EdInitVilagerPosition` only.
* Day phase `EdGetTime(NowTime)` (0x187e60): day [11.5,2.5), dusk [2.5,5.5), night [5.5,8.5), morning [8.5,11.5) (wrapping at 12). e05 has `TIME_STOP`.
* `EdInitVillagerTable(t)` copies row `phase` into `appear_table[10]`.

### 1.4 Appear state (`appear` 16 x 12 bytes at ELF symbol `appear`; reset to {0,-1,-1} by `EdInitVillagerControl` at every `EditInit`)
`{ i32 state; i32 priority; i32 move_arg }` per villager number. `EdVillagerAppearOn/Off/In/Out(v, p)` (0x1856b0/185640/1855d0/185560) set state 4/3/1/2 if `priority <= p` (scripts always pass p=1); `Move(v, m, p)` sets `move_arg`. Script ext ids: 59 ON, 60 OFF, 61 OUT, 62 IN, 70 MOVE, 57 `_LOAD_VILLAGER` (re-run select + load + position).

## 2. Selecting the outdoor set: `EdSelectVillager(NowTime, EdVillagerInfo, EditMapInfo)` (0x1857e0) **[R]**
```
appear_table = row[EdGetTime(t)]                       // EdInitVillagerTable
sel[10] = -1; n = 0
for v in 0..15 while n < 6: if appear[v].state in (ON=4, OUT=2): sel[n++] = v      // forced
for e in appear_table[0..9] while e >= 0 and n < 6:
    if appear[e].state not in (OFF=3, IN=1) and e not in sel: sel[n++] = e
for i in 0..9:
    if sel[i] < 0: EdVillagerInfo[i].model[0] = 0
    else: EdVillagerInfo[i] = master[sel[i]]; if appear[sel[i]].state != 0 and appear[].move_arg >= 0: info.moves = move_arg
    restore_info[i] = EdVillagerInfo[i]
```
So at most 6 villagers outdoors (a 7th forced one is dropped). `RestoreVillagerInfo` (0x185780) copies `restore_info` back (undo of the `moves` rewrite below).
Models are (re)loaded by `EdInitVilager` (0x185f40; texture block `0x36 + slot`, alpha 0, step 8). On a time-of-day change (`GameMode 3`, `chg_time_cnt` 140 ticks): villagers fade out, at count 140 `EdSelectVillager` + `EdInitVilager`, at the end new models and `EdInitVilagerPosition`.

**Inside** (`EdCheckVillagerIn(v, info)` 0x185af0, ext 58 `_CHECK_IN_VILLAGER`): state IN(1) -> go to the part check; state 0 -> false if `v` is in `sel[]` (outdoors), else part check; OUT/OFF/ON -> false; part check = `parts_no < 0` or house element `k` installed. => **a villager who is not outdoors at this time of day is at home** (if his element is installed); an interior script spawns him only if this is true. Every house script calls `_APPEAR_VILLAGER_IN` in its function 200 ("entering for the first time") for the villagers that belong there.
`EdCheckVillager(v, info, ground)` (0x185c50, ext 267): `parts_no < 0` -> 1; else the part is placed and the element installed.

## 3. Placing / roaming set-up: `EdInitVilagerPosition(npcs, info[10], ground, heroPos|0)` (0x186510) **[R]**
Called from `EditLoop` when edit mode is left, at map entry (after `EdInitVilager`), from the time-of-day change, and from `_LOAD_VILLAGER`. Town maps (`MapNo < 0xb`):
```
n = count of info[] with a name;  stat_quota = n / 2;  move_quota = n - n/2
seed = rand();  all info[i].active = 0
1. for i in 0..9 (slot order): if !active and info.moves == 0: active = 1; if ++cnt >= stat_quota: break
2. while cnt < stat_quota: i = random inactive named villager; active = 1; moves = 0        // GetRandomVillager
3. repeat move_quota times: i = random inactive named villager with moves != 0 (GetRandomMoveVillager; stop if none)
       active = 1; moves = 1
       pick a start cell: avoid list = hero position (if given) + the pos (+0x70) of all 10 infos, each radius 200;
       center = house part object position (GetPartsObject(parts_no)+0x10) with radius 200 (radius -1 = unconstrained if the house is not placed);
       GetRandomPlanePos(...) ; if it fails: radius += 100 and retry until radius > 500, then once unconstrained; if that fails the villager is deactivated again
4. every still inactive named villager: active = 1; moves = 0
5. for each active villager:
     visible = parts_no < 0 || (house placed && obtained > 0 && house.elem[k].installed)  and not (house complete && !completion event done && hide_until_event)
     if moves == 0: (pos,yaw) = GetPeoplePos(villager number)  (fails -> visible = 0);  info.pos = pos, info.rot = (0, yaw, 0)
     npc.ClearSeq(); npc.+0x147c = moves; npc.+0x1444 = villager number; SetPosition/SetRotation; npc.+0x1470 = visible; motion 0
srand(seed)
```
`GetRandomPlanePos` (0x1a26d0): over all edit areas, cells with grid index in `[2, w-2) x [2, h-2)` such that (no part in the cell OR the cell's part kind is 1 = road `GetPartsExtra`==1), the cell is flat (`GetAlt_i == 0`), not farther than `radius` from the center (when radius > 0) and not within the avoid radius of any avoid point; picks one uniformly (`rand() % count`), position = cell origin + unit/2 in x and z, y = cell altitude (3D `DistVector`). Max 2048 candidates.
Story scenes (`MapNo >= 0xb`) skip all this: every villager with a `person` record is placed at it and stands.

## 4. Per-tick behaviour: `EdMoveVillager(info[10])` (0x186ef0, called from `EditLoop` when `GameMode == 1`) **[R]**
Order each tick, for each of the 10 slots (npc = `EdVillager[i]`, stride 0x14a0):
1. `npc.+0x1474` (enable) = 0. If `info.active`:
   * `parts_no < 0` or `npc.visible(+0x1470)`: if `parts_no >= 0` and not a roamer, also require house `obtained > 0` and element installed, else skip (hidden); enable = 1.
   * **standing** (`moves == 0`): `SetPosition(info.pos)`, `SetRotation(info.rot)` **every tick** (so a standing villager always snaps back to the facing of its standing point).
   * **roaming**: if the 3D distance hero-villager `< 20` -> `npc.+0x11b0 = 0` (route playback paused) and request motion 0 (idle) (the queue is kept); else `+0x11b0 = 1`. Then `EdSetVillagerNextPos(npc, info, ground)`.
2. `GetNearVill(camera, hero, npcs, idx[], dist[])` (0x186cf0): for every npc with enable != 0: enable = 0; if it is in front of the camera (`(npcPos - camPos) . camDir > 0`) its 3D distance to the hero is recorded; sort ascending. **Only the two nearest, and only if `dist < 150`, get enable = 1.**
3. For all 10: `Step` (-> `CCharacter::Step` animation only if enable or `+0x1478`; alpha `+0xcec` += 8 when enable else -= 8, clamp 0..128; then `PlaySeq`), `ShadowStep`, `ClothStep`.
4. Roamers (`+0x147c != 0`) with `+0x146c` active: y = `GetAlt(ground, x, z)` (edit-grid cell altitude); if drawable (alpha > 0): collect polygons of the edit-area cell (`PickUpEditAreaPoly`) plus, if a part stands at (x,z), the polygons of that part's collision frame (`_a`, `GetCollisionFrame`) inside a 40x40 box (x +-20, z +-20, y from +1000 to -1000), then `GetFootPoly(range 1000, pos.y + 30)` -> y = foot polygon height and the foot-sound id. So **roamers follow the edit-grid ground and the collision models of roads/bridges**; they do not use the static town `GROUND` collision, have no wall collision, and no push-out against the hero.

`GameMode 2` (talk): `TalkMode` (0x17e770) calls only `Step`/`ClothStep` for all (no EdMoveVillager), roamers stay frozen because of the `< 20` rule (talk range is 1.2*r+5 ~ 13 [events.md]).
`EdMoveVillagerSubMap` (0x1874b0, `MapNo >= 10`): npcs with a villager id are only placed at `info.pos/rot`; no walking.

### 4.1 Route state machine: `EdSetVillagerNextPos` (0x187710) + `CNPCharacter::PlaySeq` (0x156140)
Each NPC has a ring queue of 8 commands of 0x50 bytes (`+0x11c0`, read idx `+0x11b4`, write idx `+0x11b8`; `+0x11b0` = playback enabled). Command: `+0 type` (1 = move, 2 = wait), `+4 wait ticks`, `+0x10 target vec4`, `+0x20 speed`.
`EdSetVillagerNextPos` only acts when the queue is empty (`CheckSeq == 0`):
```
dirs = [(0,0,100), (100,0,0), (0,0,-100), (-100,0,0)]          // ELF 0x2699f0; index 0 = +z, 1 = +x, 2 = -z, 3 = -x
b  = heading bucket of npc yaw (rot.y): 1 if [0.7854, 2.3562); 2 if >= 2.3562 or < -2.3562; 3 if [-2.3562, -0.7854); else 0   // yaw 0 = +z, atan2(dx, dz)
w  = [40, 20, 5, 20]   for k = 0 forward, 1 = dirs[b+1], 2 = back, 3 = dirs[b+3];   stay weight = 15
others = other npcs that are active, visible and ROAMERS, within 210 (3D) -> vectors d_j = other - self
for k in 0..3:  d = dirs[(b+k) & 3];  t = pos + d;  base = w[k];  w[k] = 0
    if any d_j . d > 0 (int-truncated): continue                              // somebody roams in that half-plane: blocked
    box = parts box of the edit area at t, grown by 200 on each side (x/z only); t must lie inside, else continue
    part_t = GetParts(t); part_here = GetParts(pos)
    if part_t == none and GetAreaCode(t) >= 0:  w[k] = max(1, base / 5)        // open ground: allowed, rare (8,4,1,4)
    elif part_t.kind == 1 (road)  or  (part_here != none and part_here.kind != 3 and part_t.kind == 3 (bridge)):  w[k] = base
    // anything else (house, tree, river, lake, other ground piece) = 0
total = 15 + sum(w);  p[k] = w[k] * 100 / total (int);  cumulative;  r = rand() % 100
pick the first k with cum[k] > r  -> SetSeq(speed = info.speed, target = pos + dirs[(b+k)&3]) ;  else SetWait(60)
```
`PlaySeq` (every tick, via `Step`, needs `+0x11b0 != 0`):
* wait command: `ticks--` (< 0 -> next), motion request = 0 (idle).
* move command: `v = normalize((target - pos) with y = 0) * speed`; if `distance(target, pos) <= speed` -> snap to the target, next command, motion 0; else `pos += v`. Facing: `yaw = AngleInterpolate(yaw, atan2(v.x, v.z), 0.1, mode 0)` = constant turn rate **0.1 rad/tick**, snapping to the target angle when closer than 0.1 (mode argument is in `a0` = 0 by `por a0,zero,zero` at 0x1562a0); movement is **not** gated on the heading. Motion request while moving: number 1.
* Motion request registers: `+0xc68` = motion number, `+0xc64` = flags 0, `+0xc60` = speed -1 (use the KEY step of the motion).
Consequences: a roamer walks exactly one 100-unit cell per command (ends on a cell-aligned offset from its start cell centre), then decides again after one tick of idle; per decision 15% (before blocking/ground adjustment) it stands for 60 ticks (1 s). Typical: speed 0.3 -> 333 ticks (5.5 s) per cell; `p05a` 0.17, `p03a` 0.5, `c04cat` 0.3, e04 `p58a` 0.08.

### 4.2 Motions (base `info.cfg` KEY table of the `.chr`) **[VD for p01a, p05a, c04cat, c04pcat]**
Humans (`pNNa`): KEY 0 idle (10..20, step 0.1), 1 walk (30..50, 0.3..0.35), 2 walk (same), 3 talk gesture (60..100/85, 0.2/0.15); `FOOT 31,41` (foot-step frames); `BODY_SIZE 20|17,7,60`. Requests used: idle 0, walking 1, talking 3 (`EdTalkMode` sets motion 3 while the message is displayed and motion 0 while the choice list is shown). `c04pcat` (indoor cat): 0 stand, 1/2 walk (60..80, 0.3), 3 sit. `c04cat` (outdoor cat, villager 14): **0 stand, 1 sit (30..40), 2 walk (60..80), 3 alert, 4 run** -> the generic "walk = motion 1" request plays the *sit* animation while the cat slides, [I] a quirk of the original (or the cat is rarely a roamer). No other motion numbers are requested for roamers.

## 5. Talking / the hero near a villager
* Selection (`EdSearchNearNPC` 0x198cc0, already in events.md): villager visible flag `+0x1470`, |dy| <= hero height, XZ distance <= 1.2 * npc.`+0xb0` + 5, and (hero within +-1.5708 of the villager's facing OR `info.TALK_DIR` OR the villager is a roamer `+0x147c`).
* `EdTalkModeInit` (0x198f90): `talk_camera = rand() % 3` for roamers (0 for standing ones).
* `EdTalkMode` (0x199090) each tick: villager enable = 1 (stays visible); hero turns to face the villager and the villager turns to face the hero, both with `AngleInterpolate(.., 0.1)`; **unless** `EdInteriorFlag != 0 && info.TALK_ROT != 0` (villager keeps its facing indoors). Camera: midpoint hero/villager + 15 up, offset `vv_2757[talk_camera]` rotated by the hero->villager angle. Villager motion 0 while a selection list is open, 3 while text is shown; back to 0 at the end.
* After the talk a standing villager snaps back to its original yaw immediately (rule 4.1); a roamer stays frozen while the hero is within 20.
* `TALK_EVENT a,b` (e02 v1 `12,1`, e03 v5 `12,1`, e04 v1 `11,1`): info `+0x60/+0x64`; auto talk event, see events.md.

## 6. Georama changes
* Entering edit mode: nothing is moved; leaving it (`EditLoop`) runs `EdInitVilagerPosition` (3) -> standing villagers jump to the `person` point of their (moved/rotated/removed) house; roamers get a new random start cell near the house; `EdInitVillagerOnOff` (0x186360) refreshes only the visible flag (house placed + element installed + completion/hide rule).
* A removed house hides its resident (standing: `GetPeoplePos` fails; roaming: `visible` = 0) but a villager with `parts_no < 0` (e01 v13 `p14a`) is unaffected. `hide_until_event`: hidden once the house is complete until its completion event ran (`georama_edit.md` 2.7).
* Models are not reloaded on a house move; only the position/visibility.

## 7. Open questions
1. [?] The 150-unit enable range: the code is unambiguous (`lui 0x4316` at 0x1871d0), but it is not verified in the running game that villagers beyond 1.5 cells really vanish. If the real game shows them from afar, check `Draw__12CNPCharacter` callers in `EditLoop` (alpha 0 -> `CheckDraw` false).
2. [?] `CEditArea::MakePartsBox` (0x16ef00) box used to limit the walk area (bounding box of the cells holding parts; min-x looks like it is stuck at cell 0 by a bug): reimplement by taking the real cell bounds, or read `+0x2030..+0x204c` live.
3. [?] Hero-vs-villager body collision in plain walking is not located (events use `_NPC_COL`); roamers never push the hero here.
4. [I] The "indoor_only" PEOPLE2 argument (`+0x50`) is always 0 and read by nothing seen.
5. [?] Hardcoded special cells in `GetPartsExtra` (0x16de90, returns 2 for a few (area, cell) pairs when `area+4 == 1`) look like story-scene helpers; ignored.

## 8. Implementation spec (native)
```
struct Villager { int num; string model; int parts_no, elem, hide_until_event; bool moves; float speed;
                  int talk_ev[2], talk_rot, talk_dir; vec3 pos; float yaw; bool active, visible, roamer;
                  float alpha; int motion; Cmd q[8]; int qr, qw; bool play; };            // 0x90 + NPC state
tables to read: mapinfo.cfg PEOPLE2/TALK_*/TIME_TABLE/PEOPLE_LIST (mapinfo.py already);  EditCharaData  ELF 0x267070 + town*400 + row*0x14
                { u32 name_ptr @0; i16 part @4; i16 element @6; i16 moves @8; i16 ? @10; f32 speed @12; i16 hide_until_event @16; i16 ? @18 }  (rows 0..15 override PEOPLE2);
                PTS function records type 1 (standing point, 3.4);  dirs[4] @ 0x2699f0;  yaw thresholds 0.785398 / 2.356194 (0x2a1b30..38);
                turn rate 0.1 (0x2a1870); wait 60; weights 40/20/5/20/15; sight 210; hero-pause 20; enable range 150; talk range factor 1.2 (0x2a1a48), facing tol 1.5708.
per map entry:   appear[16] = {0,-1,-1};  if PEOPLE_LIST has entries: for each of 4 phases: shuffle(list) ; row[phase] = list(+ -1 padding)
per phase change / map entry / leaving edit mode / _LOAD_VILLAGER:  EdSelectVillager (2) -> EdInitVilagerPosition (3)
per tick (walking): EdMoveVillager (4): stand: pin; roam: pause if hero < 20, EdSetVillagerNextPos when queue empty, PlaySeq in Step, ground y via edit-area cells + part collision frame;
                    fade alpha toward 128 for the <= 2 nearest-in-front-of-camera villagers within 150, else toward 0; draw only if alpha > 0; animate only if enabled.
motions: idle 0, walk 1, talk 3.   interiors: static, positions from the house event.stb (_LOAD_IN_VILLAGER x,y,z,yaw), idle 0, do not walk.
```
The native host interface `villager_present(v)` (`event_runtime.cpp`) should answer `EdCheckVillagerIn` (state IN/0 + outdoors list + element) and `_CHECK_VILLAGER` (267) = `EdCheckVillager`; `dc_townwalk.cpp make_villagers` should create all 16 slots and let the selection decide who is shown.

## 9. Interior placements (from `event.stb`, `villagers.py --interiors`)
`_LOAD_IN_VILLAGER(slot, villager, "model", x, y, z, yaw)` run in the house's internal init function (the one that starts with `_CLEAR_VILLAGER_BUFF`), guarded by `_CHECK_IN_VILLAGER(villager)`; positions are interior-local world coordinates. Example Norune: i01h02 villager 3 `p01a` (-20, 0, 28) yaw 1.85, villager 2 `p07a` (-19.8, 0, -7.12) yaw 1.16; i01h011 the cat `c04pcat` (-28, 0, -34) yaw 1.55 and `p47a` (34.3, 0, -12.46); i01h01 villager 12 `p10a` (-8.99, 0, -8). All towns: run the tool. Story rooms (`s*`) use the same calls plus `_LOAD_OUT_VILLAGER` (s03).
