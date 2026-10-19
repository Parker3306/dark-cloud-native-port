# Dungeon items, view mode, mimics, trap circles, get-item sequences

Research for the native port (read-only; nothing under native/, iso/, data/ was touched). Tags: **READ** (decompile `work/decomp/all_dun.c`), **DISASM** (read from the MIPS disassembly, Ghidra headless on a scratch copy of the project), **VERIFIED-DATA** (read from the ELF / DUN.BIN / data files), **INFERRED**.
Pad masks: 0x80 square (Use), 0x40 cross, 0x20 circle (US: OK = 0x40, NO = 0x20), 0x10 triangle, 0x800 start, 0x1/0x2/0x4/0x8 = L2/R2/L1/R1. `UserStatus` offsets as in notes/menus.md. Player mode `DAT_01dc4490` (= `PlayerCombat::mode`): 0 free, 1 attacking, 2 throwing, 3 drinking, 4/5 hurt, 6 guard. `DAT_01dc448c` = requested motion ("wish"), `DAT_01dc44e4` = can_act, `DAT_01dc44f0` = held. `selectMapNo` = dungeon index 0..6 (so `selectMapNo + 1` = 1..7).
Motion numbering: the `//N` comments of `base.cfg` are the motion numbers (KEY_START line is not a motion): throw-hold = **0x1a (26)**, throw = **0x1b (27)**, drink = 0x1c (28) (already used by `drink_step`).

## 1. Item throwing

### 1.1 Which items throw (READ `CActiveItemPack::CheckStatusType` 0x1d5430)
Return 1 (throw) when `ITEM_LIST[id].flags16(+8) & 2`: ids **0x98 Sacred Water, 0x9f Bomb, 0xa0 Stone, 0xa1 Flame Gem, 0xa2 Chill Gem, 0xa3 Lightning Gem, 0xa4 Wind Gem, 0xa5 Sacred Gem, 0xa6 Throbbing Cherry, 0xa7 Gooey Peach, 0xa8 Bomb Nuts, 0xa9 Poisonous Apple** (VERIFIED-DATA items.csv: all have flags16 = 3). 4 = instant use, 2 = drink (existing). **Type 3 = Dran's Feather 0xeb** (see 1.9). Everything else 0. Item kind "throw" needs only `checkItemUsed` (=1 for these) and `count > 0`.

### 1.2 Start (MoveChara 0x01db08a0, READ, lines ~177640-177700)
Conditions: `Down(0x80)` && `DAT_01dc4518 == 0` (not dead/frozen) && mode == 0 && `gameTask != 0xf0` && can_act != 0 && `mode != 2` && CheckStatusType == 1 && count (short `UserStatus+0x4366+2*sel`, sel = itemNowSel 1..3) > 0.
* **No lock-on** (`lockOnTargetFlag == 0`): mode = 2; `DAT_01eb6304 = 1` (aiming), `DAT_01eb6308 = -1`; reticle `BombInfo` (0x1eb62f0, vec4) = `P + d + 30*d` per axis x and z (d = `getCharacterVector` = (sin yaw, 0, cos yaw); exact float order `(d + P) + d*30`), y = P.y (`DAT_01eb62f4`), i.e. 31 units in front; wish = 0x1a; can_act = 0; held (`DAT_01dc44f0`) = 1; the item model is put in the hand (`SetHandModel`, model state 1 = child of character frame named `item`, rot X +pi/2).
* **Lock-on**: `DAT_01eb6308 = 1` at once (no aim), mode 2, wish 0x1b, can_act = 0, held = 1, hand model.
* While `Down(0x80)` is held and no lock-on, other code (type 3 only) uses `On(0x80)`; throwing needs only the press.

### 1.3 `BattleActionThlow` 0x01dbbee0 (READ + DISASM), runs every tick in MoveChara after the `SearchDataSlotPos` event check, before `BattleActionDrink`. Only acts when mode == 2.
State `DAT_01eb6308`: -1 aiming (flag `DAT_01eb6304`), 1 waiting for the release frame, 2 follow-through.
1. **Aiming** (`DAT_01eb6304 != 0`): if `On(0x80)` is NOT held: aim = 0, state = 1, wish = 0x1b. Else with `a` = camera angle (`CCameraFollow::GetAngle`), `lx, ly` = `GetLXf/GetLYf` (stick/128, up = negative ly, same values the walker already has):
   `cand.x = (double)B.x + (double)lx*cos(a) + (double)(ly*sinf(a))`, `cand.z = ((double)B.z + (double)ly*cos(a)) - (double)(lx*sinf(a))` (cos is the double libm, sinf float; result cast to float), `cand.y = B.y`, w = 1. `d = DistVector(cand, P)` (3D). **Accept (B = cand) only if 30.0 <= d <= 120.0** (DISASM: `c.lt d,30` rejects, `c.le d,120` accepts). The player ALWAYS turns toward the candidate: `yaw = unitRotation(atan2f(cand.x - P.x, cand.z - P.z), yaw)` (accepted or not), wish = 0x1a (hold pose). No other input; no cancel (taking a hit cancels, 1.8). The stick does not walk (can_act = 0 zeroes velo). One unit of reticle per tick at full stick.
2. **State 1** (release): wish = 0x1b every tick. When `releaseFrame[char] <= motionTime <= motion[0x1b].end` (motionTime = `DAT_01ea2010`, the clock after this tick's step) the item leaves:
   * `releaseFrame` (float x6 at **0x01DC2860**, VERIFIED-DATA): Toan 548, Xiao 452, Goro 267, Ruby 347, Ungaga 188, Osmond 321. Motion 0x1b ranges (base.cfg): Toan 546-566 @0.4, Xiao 450-460 @0.4, Goro 264-275 @0.3, Ruby 341-360 @0.35, Ungaga 186-206 @0.22, Osmond 320-328 @0.2. Hold pose 0x1a is the single first frame (546/450/264/341/186/320, speed 0).
   * sound `SndSePlay(0x96)`; **consume**: `ids[sel-1]` at `UserStatus+0x4362+2i`, counts at `+0x4368+2i`: if count == 1 { id = -1, count = 0 } else count--. (No bag / per-slot value change.)
   * hand model deleted; start position `S` = world position of frame `item` of the character (offset (0,0,0,1)).
   * **No lock-on**: `v = ParabolicInitialVector(g = 0.12, T = 20.0, S, B)` then `v.xyz *= 0.8` (`sceVu0ScaleVectorXYZ(DAT_002a1bcc = 0.8)`). ParabolicInitialVector (DISASM 0x1d4080): `vx = (B.x - S.x)/T`, `vy = ((B.y - S.y)*2 - T*(g*T)) / (2*T)` (PS2 `mula/msub`: `acc = 2*dy; acc -= T*(g*T)`), `vz = (B.z - S.z)/T`, w = 1, then **`vy = -vy`** (so vy = gT/2 - dy/T; this is the game's own sign quirk, port it as is).
   * **Lock-on**: `v = normalize(Lock.pos - S)` with `v.y` computed from `(Lock.y - S.y) + 5.0` before normalising (xyz only), then `v *= 4.5` (xyz). `Lock.pos` = `LockState::pos` (DAT_01dc4500).
   * `SetThrowModel(model of the active slot, S, v)` (state 2 model; the model's own counter `+0x27d0 = 0`); state = 2.
3. **State 2**: wish = 0x1b; when `end-2 <= t <= end` of motion 0x1b: mode = 0, can_act = 1, `DAT_01dc44a8 = 0`, `driveNoInterpolate = 1`, held = 0, wish = 0.
Hit by anything while mode == 2 (BtCheckDamageProc, 1.8) aborts the throw WITHOUT consuming.

### 1.4 Flight: `CMainItemModel::Step` 0x1d4e20 + `ItemThrowStep` 0x1d4260 (READ + DISASM)
`CMainItemModel::Step` runs in the effects step (before HitMark/CRandomItem, i.e. `step_effects` start; NOT gated by `CEffectHold`) for each of 16 model slots whose state == 2. Per tick, per thrown item (position `p`, velocity `v`, counter `n`):
1. `ItemThrowStep(p, v)`: 
   * `q = p + v` (xyz), then `v.y -= 0.12` (always, even when something is hit).
   * for monster units 0..15, spheres 0..15: sphere active flag `unit*0x510 + 0x55450 + 4*k` != 0 (NOT the unit's `active` field) and `DistVector(sphere.pos (+0x55250 + 16k), p) <= radius (+0x55390 + 4k) + 1.5` (**tests the OLD position p**) -> return 2, position unchanged. These are the monsters' `body_cols` the shot code already uses (checkCollision there also requires unit `active != 0`; the item test does not).
   * else terrain: polygons near p via `setCollisionData(20.0, 1.5)` (same gather the shots use, `floor_polys_near` + the box filter used in `shot_collide`), `h = CheckHit(polys, n, p, q, out hit, nearest = 1, mask = 4)`.
     * no hit: `p = q`, return 0.
     * hit (polygon `i`): `N = normalize(poly[i].n)` (poly +0x30). `d = DistPlanePoint(N, hit, p)` (signed distance of the old point; result of `ReflectionPlane` is `2d`); `v' = (hit - p) - N*(-2d)` i.e. **`v = (hit - p) + 2d*N`** (computed from the travelled segment, NOT from v); then `p = hit + 1.3*N`; `v *= 0.4` (x,y,z); return 1 (the item bounces and keeps flying).
2. `n += 1` (counter `+0x27d0`); if ItemThrowStep returned 2, `n` is first set to 0x2d. **When `n >= 0x2d` (45) the item detonates** at `p` (after the position was stored) and the model is deleted. So a thrown item always explodes 45 ticks after leaving the hand, or on the first monster sphere it touches.
3. Detonation by item id (model's cached id, `+0x18`), `dun = selectMapNo`, `D = (dun+1)*30`:
   | id | effect |
   |---|---|
   | 0xa1..0xa5 gems | `SndSePlay(id - 0x3c)` (0x65..0x69) + `SndSePlay(0x6c)`; effect slot `MasekiEffect[id-0xa1]` (CSHOT_EFFECT, stride 0xa160 at 0x1E5B380): `Set(pos, target=(0,1,0,0), owner_kind -1, sub_id -1, 0, no frame, force_phase -1)`, `SetDmg(D)`, `SetLifeTime(10)`, `SetWait(5)` |
   | 0x98 Sacred Water | sounds 0x69 + 0x6c; effect `MasekiEffect[4]` (0x1E83900, the holy one): same Set, `SetDmg(D)`, **`SetEnemyAttr(1)`** (only monsters of slayer kind 1 = undead take damage, `ColEntry::only_monster_kind`), `SetWait(2)`, no SetLifeTime |
   | 0x9f Bomb, 0xa8 Bomb Nuts (default) | `SetBombEffect(1.0, p, 3, D)` |
   | 0xa0 Stone | `ColPool::set(radius 8.0, 1.0, p, damage 8, life 5, target 2, kind 2, flags 0, extra 0)` |
   | 0xa6 Throbbing Cherry | same Set with damage **2**, flags **0x100** (freeze) |
   | 0xa9 Poisonous Apple | damage **2**, flags **0x200** (poison) |
   | 0xa7 Gooey Peach | damage **8**, flags **0x800** (goo/slow) |
   (DISASM 0x1d4e20..0x1d5330; args of `CCollisionData::Set` are radius f12, flag f13, this, pos, damage, life, target, kind, flags, extra. Owner stays -1/-1, so monsters use the `owner == -1` branch of `monster_combat` incl. `dmg_scale` and the 0x100/0x200/0x800 status code that already exists. Target 2 = monsters only; the sphere is freed after a hit because target != 3.)
   * **Gem models** `maseki_ex` (VERIFIED-DATA rows at DUN.BIN 0x1dc2230 + k*0x70, loaded by `LoadData` via `Entry2/ReEntry` from `dun/effect/maseki_ex.chr`): mode 0, speed 0, radius[0] 10.0, element attr 1/2/4/8/0x10 (fire, ice, thunder, wind, holy), kind 3, target 2, motion phase0 = 0/1/3/2/4 for fire/ice/thunder/wind/holy; `ShotDef::from_memory` + `ShotSystem::entry` can load them (5 slots like the player main effect). They step in `step_effects` after the bomb effects (order in the 0x192330 block: Step CMainItemModel, HitMark..., RandomItem, StealItem, CItemBombEffect x3, **MasekiEffect x5**, ShockWave, ShotEffect x5, MachineGun, FireBar, ...).
   * **SetBombEffect** 0x1d5940 (READ): picks a free `CItemBombEffect` (3 slots, visual only; no free slot -> nothing at all, not even the sphere), `SndSePlay(0x6c)`, `ColPool::set(radius 20*scale, 0.0, pos, damage, life int(45*scale), target 3, kind 3, 0, 0)` and on success `hit_counter = hit_reset = 10`. `shot_env().on_bomb` in `monster_combat.cpp` already is exactly this (scale 1.0, target 3 hurts the thrower too; dir stays (1,0,0)). Only for scale > 1 a shock wave is added (not used by thrown items).
4. Models: `dun/item/main_data/<name>.mds` + `.img`, name = `ComItemInfo[id*4 + 0x87c]` string pointer (bakudan, ishi, masekifi/ic/td/wd/ho, isiranbo, nebapeac, bomnuts, dokuring, seisui ...). Thrown model rot X = +pi/2, never spins.

### 1.5 Existing dccore pieces to reuse
`ColPool::set`, `ShotEffect::set` + setters (`damage`, `cooldown` = SetLifeTime, `wait`, `only_monster_kind` via `ColEntry`), `shot_env().on_bomb`, `shot_collide` gather code, `check_hit`, `unit_rotation`, `drink_step` pattern (`end-2 <= t <= end`).
New state needed: `aiming`, `throw_state`, `reticle[4]`, up to 16 `ThrownItem{pos,vel,counter,item}`, the 5 Maseki ShotEffect slots.

### 1.6 Drawing the aim
While `DAT_01eb6304 != 0` the model `dun/etc/bomblock.mds` (`bombCursorFrame`) is drawn at `BombInfo` (MainDraw, after the lock-on cursor). Nothing else (no line). Hand model and thrown model are drawn by `CMainItemModel::Draw`.

### 1.7 Sounds
0x96 release; 0x6c explosion; gem 0x65..0x69; Sacred Water 0x69; instant items 0x13 + 0x1b8 (not here).

### 1.8 Cancel rules
`BtCheckDamageProc` (any pool hit on the player, also the player's own bomb) sets aim = 0, `DAT_01eb6308 = -1`, can_act = 1, mode 0, held 0 and `AllReleasItem` (hand model gone); item NOT consumed. Death (gameTask 200) does the same.

### 1.9 Dran's Feather (type 3, 0xeb), for completeness (READ MoveChara ~177610)
While `On(0x80)` is held, no lock-on, `DAT_01dc4490 == 0`, `DAT_01dc44e4 != 0`, slot count > 0: `BtBySpeedFlag = 1`, **run speed 3.0 instead of 1.45** (`DAT_002a1c18`), pulsing light, and the slot's int `UserStatus + 0x8b14 + 4*(sel-1)` is decremented every tick; at <= 0 the slot is emptied (`DelActiveItem`), message 182 "Dran's Feather broke". The HUD pulse tests id 0x9c (a game quirk, never matches).

## 2. First person view mode (gameTask 10, `viewMode`)

### 2.1 Enter / exit (READ MoveChara)
* **Enter**: `Down(2)` (R2) in gameTask 0 while `DAT_01dc44a0 == 0` (not in the post-hurt stun) and held (`DAT_01dc44f0`) == 0. Works for every character (Toan/Goro/Ungaga only look; Xiao, Ruby, Osmond can also shoot). Effects: `oldCameraAngle/Height` saved, camera follow off, `BtActStatus = 0` (the third-person character is not drawn), `viewMode = 1`, `InitEyeCamera` (`viewAngleH` = player yaw, `viewAngleV` = 0), Xiao: `EquipReAttach`, `DAT_01dc44a8 = 0`, wish 0, mode 0, step 0, **lock-on dropped** (`LockOffTargte`), `ResetMovePower`, Ruby effect off, `gameTask = 10`.
* **Exit**: `Down(2)` R2, `Down(4)` L1 or `Down(0x20)` circle, or forced: damage taken (`BtCheckDamageProc != 0`), death (-> gameTask 200), event/script triggers (400/0x226), falling below y = -30. Exit: follow camera on, `SetHeight(oldCameraHeight)`, camera speed 1.0 snap then 8.0, player yaw := `viewAngleH`, `BtActStatus = 1`, `viewMode = 0`, `cameraAuto` restored, Xiao `EquipReAttach`, Ruby effect off; if not hurt: wish 0, mode 0, `DAT_01dc44a8 = 0`.
* Triangle (0x10) opens the pause/battle menu (gameTask 0x1e) when life >= 1 and (frozen or not shooting) - returns to gameTask 10 afterwards (0x22/0x9b check `viewMode`). Start = pause logo (gameTask 0x9b). Changing character from the menu leaves view mode.

### 2.2 Per tick in gameTask 10
1. `EyeCamera` (0x1dc17e0, DISASM/READ): `lx, ly` = stick; `viewAngleH -= lx*0.02` (wrap to (-pi, pi], pi = 3.14159, 2pi = 6.28319); `ly' = -ly`; `viewAngleV += ly'*0.02` only while `viewAngleV < 0.65` (looking up) or `viewAngleV > -1.0` (down) respectively. Camera position = `P + (0, height[char] - 1.5, 0)` with height = {16,14,16,16,18,15} (VERIFIED-DATA 0x1dc1b40); reference = position + `RotY(H)*RotX(V)*(0,0,10)` (identical to `setShotVector(10, H, V)`). `SetPos/SetRef` directly (no smoothing).
2. Frozen (`StatusErrCheck(4)`) -> `DAT_01dc4518 = 1` (cannot shoot). `HealingWater()`; `UserStatus+0x8b08 = 1` (thirst drains as if walking).
3. If `NowDngMap[0x2f7b] != 1 && selectMapNo == 0` (only dungeon 0): gravity/ground step with zero horizontal velocity (vy -= 0.1, clamp -10, MoveCheck); y <= -30 forces exit.
4. Messages when pressing OK with `DAT_01dc4514 != 0` or (Osmond and `DAT_01dc4512 == 10`) - shooting is refused.
5. Characters 1, 3, 5 (and not frozen, not refused): arm/hand model rotation `(viewAngleV, viewAngleH, 0)`, `CharaMainHandViewFlag = 1`. OK press -> `BattleActionOn_Jinn / _Ruby / _Ozumond[_H|_F]` (same entry conditions as normal view: Xiao/Ruby need attack gauge exactly 100 and mode 0; Osmond MG/FB need `DAT_01dc4524 == 0`, H needs gauge 100). While mode == 1 the matching `BattleActionPlay_*` runs with **view flag = 1** (DISASM: `li a1,1`; Osmond's `_F` and `_H` get a0 = 1) and with the **arm model's** motion clock instead of the body clock.
6. Nothing else moves the player (no walking, no turning except the camera). Monsters keep acting; hits end the mode.

### 2.3 What shooting does differently in view mode (READ)
Everything else is the normal-view code already ported; the differences:
* **Xiao** (slingshot): clock = arm model `c04bhand` (`hand_up.cfg`: motion 11 = frames 10-20 @0.7 pull, 12 = hold 20, 13 = frames 20-30 @0.7 release). States 0xb/0xc/0xd as in normal view with the same logic, state 0xb -> 0xc/0xd at `end-2..end` of arm motion 0xb (end 20); the shot fires at **arm time 21.0 < t < 21.5** (normal view: body time 251-252), at the world position of frame `eff30` of the weapon (offset (0,0,0,1)), velocity `setShotVector(3.5, H, V)` (normal: 5.0 along the yaw or toward the lock target); then `SwordDmgCheck1(1.0)`, bullet life 0x78, damage = weapon attack (x2 with Stamina). End when arm time in `[table[0xd].end - 2, end]` (end 30).
* **Ruby**: arm `c05arm` (`hand_up.cfg` motions 11 magic1 120-140 @0.3, 12 150-170, 13 charge 180-185, 14 loop 185-195, 15 195-200, 16 charge magic 205-225); logic `BattleActionPlay_Ruby(hand, 1)` has the same frame windows as normal view (126.0-126.3 start charge, 128.0-128.3 first shot, 216.x second shots, constants 0x2a1cd4.. VERIFIED), `BattleActionShotRuby` uses frame `weapon` of the ARM model as muzzle and `setShotVector(3.5, H, V)` for the target point (`muzzle + dir`), force_phase 1. Arm is updated but NOT drawn (only char 1's arm is drawn, MainDraw `Draw(0x1ea2ed0)` guarded by char == 1).
* **Osmond**: no arm. Muzzle = **player position + (0, 10, 0)** (not the weapon frame); direction `setShotVector(3.5, H, V)` (machine gun / hand cannon `OpC_InitProcess5(1)` / `_H(1)`), flame thrower `Init` uses speed 1.0, `Set` 3.5. Lock-on is off in view mode so no target override.
* Draw: HUD unchanged, no crosshair, hand weapon drawn through `NowWeapon` virtual draw for Xiao only.
Implementation order for view mode: camera + enter/exit (easy), Xiao slingshot, Osmond, Ruby.

## 3. Mimics, trap circles, chest traps

### 3.1 Mimics (READ `SetupViewMonstor` 0x1e02b0 + DISASM, `SetMimicEvent` 0x1c8bc0, MoveChara event type 8)
* Monster table entry with `monster_code` (ELF row +0x54, slayer category 8 = Mimic) == 8: at placement `monster.active = 0` (hidden; port already does this) AND, by the row's `kind` (ELF +0x78, `MonsterEntry::kind()`): **kind 4 -> `SetMimicEvent(x,y,z, map, slot, 0)` (big chest), kind 3 -> `SetMimicEvent(..., 1)` (small chest)**; any other kind: hidden forever (none exist). (x,y,z) = the monster's placement position.
* `SetMimicEvent`: first free chest slot (24 slots, same table as real chests): `used = 1`, pos, `+0x20 = monster slot` (where a chest holds its item id), `+0x24 = 1` (visible), `+0x28 = kind` (1 small, 0 big), `+0x2c = 0`; trap id `+0x30` untouched. A free event slot (48 x 0x50): `type = 8`, `+4 = 0`, `+0x20 = chest index`, area centre `(x, y, z + 8.0)` for kind 1 or `(x, y, z + 10.0)` for kind 0, radius `8.0` (same numbers as `SetTreasureBox`; does not write the fptosi'd tile fields nor bump the chest counter).
* **Opening** (MoveChara, OK pressed in the area, no stun `DAT_01dc44a0 == 0`, not frozen, `DAT_01dc45c8 == 0`): `monster[chest.slot].active = 1` (the monster script's revive function 50 runs, the mimic jumps out), `chest.used = 0` (chest model disappears), event slot `type = -1`. No sound, no camera, no `gameTask` change. 
* Port work: `FloorEvents` event type 8 + a chest entry; `EventWorld::step` handling above; `MonsterWorld` must call the revive path when `active` becomes 1 (check `monster_ai` handles `active == 1`). Message names: mimic names are in the dunmsd bank (3034-3039, 3079-3083, 3309-3310).

### 3.2 Trap circles (`Run_TrapCircle` 0x1dbfa70, DISASM incl. jump table 0x1DC33A0)
Correction to `Notice::Trap` in event_world.h: **there is no monster summoning and kind 0 is not poison.** Trigger: every tick in gameTask 0, `DistTrapCircle`: circle state 1 and `DistVector(circle, player) <= 5.0` -> state 2, `Run_TrapCircle`, then `SetSystemMes(kind + 300, 150 frames, pos 8)` (bank `meswin/sysarticle_<lang>.mes`, VERIFIED-DATA ids 300-309). The circle kind (0..9) was rolled in `SetupTrapCircle` (`rand()*10/2^31`, clamp 9). **If the active weapon id equals `defWeapon[char]` or `defWeapon[char]+1` (257,299,314,331,347,363 at 0x1DC1B00) the kind is forced to 0.** Effects:
| kind | sound | effect | message 300+kind |
|---|---|---|---|
| 0 | 0xe1 | `BtSetStatusErr(8)` = the Stamina state (attack x2 via `StatusErrCheck(8)`, timer 0x708) | "Pumped with energy." |
| 1 | 0xe1 | gold += `(int)(gold*1.2) + 10`, capped 0xffff (`UserStatus+0x4346`, u16; const 0x2a1af8) | "Funds increased a little." |
| 2 | 0xe1 | weapon code 0: `exp(+0x14) = GetWeaponMaxExp` (Abs full); nothing if the weapon is one of the six broken defaults | "Abs of the equipped weapon became full." |
| 3 | 0xe1 | code 2: max WHp (+0xc) += `rand()%3 + 3`, capped 99; nothing for `IsDefaultWeapon` (def or def+1) | "Max WHp ... increased." |
| 4 | 0xe1 | code 4: WHp (+0x10 float) = max WHp; if the weapon is the broken default it becomes the real default (`EquipWeaponFrame`, record rebuilt like Repair Powder, Osmond: `DAT_01dc4520 = 0`) | "WHp ... recovered." |
| 5 | 0xe2 | `CMonstorUnit::AllBin2`: **every monster's `bin2` (+0x1e3e0 + i*0x190) = 300** (monster "pumped": takes half damage, see monster_combat) | "Monster is pumped with energy." |
| 6 | 0xe2 | gold = max(0, `(int)(gold - gold*0.2)`) (const 0x2a1c50) | "Funds decreased a little." |
| 7 | 0xe2 | code 1 (see below) | "Status of the equipped weapon changed." |
| 8 | 0xe2 | code 3: max WHp -= `rand()%3 + 3`; if (float)max < WHp then WHp = max; max >= 1; nothing for default weapons | "Max WHp ... decreased." |
| 9 | 0xe2 | code 5: WHp /= 4.0, min 1.0 | "WHp ... decreased." |
Weapon codes go through `WeaponDataChangeByRGate(weapon_record, code)` 0x20fce0 (record = `UserStatus + 0x450c + ch*0xaa8 + slot*0xf8`), afterwards `SetWeaponAttachStatus(NowWeaponHave)`. **Code 1** (not for `IsDefaultWeapon`): `s = rand() & 3`, `a = rand()%3 + 2`; s=0: Attack(+4) -= a (min 1); 1: Endurance(+6) -= a (min 0); 2: Speed(+8) -= a (min 0); 3: Magic(+0xa) -= a (min 0); then four `LocalWeaponDataChange(ptr, count, base, mod)` (each: `idx = rand() % count`, `amt = base + rand() % mod`, `arr[idx] -= amt`, floor 0, byte array): (rec+0x17, 5, 2, 3) elements, (+0x1c, 3, 2, 2), (+0x1f, 3, 2, 2), (+0x22, 4, 2, 2) slayers - 10 rand() draws in all. A circle fades after use (existing code).

### 3.3 Chest "quality" is the trap id (READ `SetTreasureBox`, MoveChara chest branch)
`buildEventData` passes `rand()*6/2^31` (0..5) as `param_5` for BIG chests; it is stored at chest `+0x30` and `_GET_ITEM_TRAPID` returns it. In MoveChara a big chest (kind 0) with `+0x30 != 0` does not open: it runs the dungeon script function **0x10 if the id is 5, else 0xf** (`gameTask = 400`, `DAT_01d567ec` = chest index) - see notes/dungeon_events.md; `_RESET_ITEM_TRAP` clears it. Small chests (kind 1) never check it. So `Chest::quality` of the port must be treated as the trap id, not a weapon grade.

**Port state (2026-10-19):** a big chest with a trap id does not open: `EventWorld::step` raises `Notice::TrapChest` (a = chest index, b = id; id 5 frees the event slot), `DcWalker::start_trap_script` runs dungeon function 15 (16 for 5) with the chest as `ibox_` (mode 1, event position = the chest's event area). Primitives: ext 1081 `_GET_ITEM_TRAPID`, 1082 `_RESET_ITEM_TRAP` (chest id 0), 1083 `_BOM_SET` (`MonsterWorld::trap_bomb`: a bomb sphere at the player, damage 20 per dungeon number), 1084 `_SET_STATUS_ERR` (status + `trap_hurt` sphere), 1086 `_CHECK_MARDAN` (SaveData +0x12f94 = Mardan Garayan caught: with one the trap guess is skipped), 1098 / 1099 (lid angle, chest used up), 1100 `_GET_PIERO_ITEM` (`EventTables::piero_item`, tables `PieroItemListPtr` at 0x279CF0 loaded from the ELF). The scripts do the rest (Y/N, guess the trap, key item 181, Piero scene with item pick). The trap id is part of the co-op floor state (three ints per chest). Verified: `work/trap_smoke.py` (wrong guess at an Explosion trap hurts, right guess at a Curse trap releases it, the Happy Clown gives a weapon); harness flags `--set-trap chest,id`, `--bag 1`, d-pad bits of the pad presets (`ddown`, `dup`).

## 4. Get-item presentations (all hold the world: `CMonUnitHold = CMonUnitHyde = CEffectHold = CEffectHyde = 1`, HUD off via `SetMIniMapStatus`, `UserStatus+0x8b0c = 1`, `DAT_01dc451c = 1`, `DngMessMan = 0`, player shadow `DAT_01dc44d4 = 0`)

### 4.1 Chest (gameTask 0x82 small: `BtGetTreasureboxSmall_Init/Loop` 0x1d1fc0/0x1d2460, 0x78 big: 0x1d13f0/0x1d1810) - READ
* Init: item = chest `+0x20`. Small: `CheckItemGet(item)`: 0 ok; 1 -> message 0x48 (bag full), 2 -> 0x51 (attach bag full); big: `CheckWeaponRot(item) >= 10` -> message `0x49 + weapon user`; blocked: sound 0xcf (small) / 0xce (big), chest `+0x2c = -20.0`, state 10 = wait for `Down(0x60)` then release. Big only: id 0x12f becomes 0x130 when `PlayerAllItemCheck() != 0`. `TreasureboxBig_itemType`: weapon user 1 (Xiao) -> 1, 5 (Osmond) -> 2, 3 (Ruby) -> 3, else 0.
* Otherwise: the event slot is freed at once; small: `createAttachVolume(item, dungeon)` (rand) = volume; item with no model name -> id 0x66; loads (background) `dun/item/main_data/<name>.mds/.img`, the character's pose pack `dun/mainchara/<c01d|c04b|c06b|c05a|c10b|c18a>_ex00.chr` (pointer table 0x27E7D0, `base2.cfg`: motions 42 small-get 10-80 @0.5 + 43 hold at 80, 44 big-get, 45 hold, 46 atla-get 30-130 @0.4, 47 hold) and sound bank 2.
* Loop state 0: wait for loads, bind item texture/model, character model := pose pack. State 1: chest `+0x2c = -30.0` (chest model sunk) and `+0x24 = 0` (hidden), open-animation models positioned at the chest (`itemget_s/d01i02m` small, `itemget_b/d01i01m` big, CMotionModel start/end/speed 10/80/0.5 per MOTION_INFO 0x1dc2510..; fx models `igetfx`/`igetfxb` in the map pack), player teleported onto the chest, yaw 0, wish **0x2a** (small) / **0x2c** (big), models' time = 10.0, item model scale 0.01 and positioned (small: chest pos + (0,13,0) then back; big: pos - 5 in z? plus per user-type rotation/offset: type 0 rot (pi/2,0,pi/2), type1 rot (-pi/2,-pi/2,0) +3.5, type2 rot (-pi/2, pi, 0) +2.5, type3 rot (0, pi/2, 0) +3.5 scale 2.0), camera = `SubCamera` (copy of the main camera state, follow off); state 2.
* State 2 (every tick the open-animation models advance 0.5): camera is set from the objects `cam` / `int` of the fx model (`setCameraPassData`). Small: fx time 19..20.4 -> sound 0xcf + SPSe(2); time 50..55: item scale `0.2*(t-50)` (small) / `(scale/5)*(t-50)` (big); at **t >= 79**: wish 0x2b / 0x2d (hold), model flag 2, `ItemGetMes(item, volume|-1, 0x28, 1)` (message 10/11 attach/weapon, 20/21 ids > 0x50, 30/31 ids > 0x90; item name = id + 100, weapons `info+6` + 100), state 3. Big uses sound at fx time 14..15 (0xce + SPSe(2)).
* State 3: camera as above; on `Down(0x60)` (cross or circle): item granted - small: 0xe9 -> `BtEquipMap = 1`, 0xea -> `BtEquipMasuisyou = 1`, else `GetItem(item, volume)`; big: `GetItem(weapon)`; player put at his previous position + (0,10,0)... (position vector copy, y += 10), yaw `-3.14159`, message cleared, shadow on, holds released, chest `+0x2c = 0` (hidden state cleared), camera back to `MainCamera`. Total presentation ~ 140 ticks (fx 10 -> 80 at 0.5) before the message.

### 4.2 Atla (gameTask 0x8c/0x8d = `BtAtraGetShort_Init/Loop` 0x1d2af0/0x1d2c70) - READ
Only Toan (`char == 0`); other characters get `NotGetAtraMes` (message 0x46 + name, 90 frames). Init: `atraShortGetType = 1`, loads `dun/mainchara/c01d_ex00.chr` (motion 46/47), the effect model `dun/effect/saget.chr` (`info.cfg`), sound bank 1; `BtAtraGetNo = iventActive`, `iventActive = -1`, state 0.
* State 0 (after loads): player model := pose pack, effect model placed at the atla ball (ball table 0x20 stride at map+0xbc80), `BtAtraGetID` = `UserStatus + 0x2078 + dungeon*0x4b0 + ball.idx*12`; ball `+0x14 = 0` (hidden), event slot freed; **`getAtraToSaveData(id, ball.idx, SaveData, dungeon, floor)`**: id < 0x28 -> `CSaveData::AtraPartsGet(dungeon, id)` (parts: if `GetEditAtraPartsData->[1] < 1` only sets the flag once, else `+8 += 5` capped by `[0]`, `+4 = 1`), else `AtraChipGet(dungeon, id - 0x28)` (first free of 0x80 shorts in `GetElemData` gets `id - 0x28`); then `GetAtraData`: in the floor list (`UserStatus + 0x278 + dungeon*0x500 + floor*0x20`, 8 ints) the entry == ball.idx (else the first == -2) becomes -3 and the dungeon table count (`+0x2080 + idx*12`) is decremented, id := -1 at 0. Player placed on the ball, yaw 0, wish **0x2e**, camera = SubCamera driven by the effect model's own camera (`SetMotionCamera`), `atraGetStatus = 1`.
* State 1: `atraGetStatusRate += 2` up to 256 (ambient light blend, colours (0.977, 0.66, 0.5)*rate in the lighting); at effect time 53..54 SPSe(1); at **effect time >= 129.5**: `AtraGetMes(dungeon, id, 0x28)` (message 0x28 with name id from `GetAtraMsgNo`: `1000 + dungeon*200 + name`, +0x28 for ids >= 0x28, +0x28 more for >= 0x50), `atraGetStatus = 2`, message board alpha from 0, wish 0x2f. State 2: board alpha += 4 to 128. State 3: wait `Down(0x60)`, then fade out (8 frames). State 4: when fade == 128 fade in. State 5: shadow on, message cleared, HUD back, camera `MainCamera` (speed 6.0, follow on), player yaw `-3.14159`, holds released. The player stays on the ball position.

### 4.3 Other
Gate key (`BtGetGateKey_Init/Loop` 0x1d3560/0x1d36a0, gameTask 0x1fe), attach/steal loot (`BtGetAttach_Init/Loop` 0x1d3a70/0x1d3b00, gameTask 0x208) are the same message-and-hold pattern (not read in detail). Escape (`BtEscape`, gameTask 0xaa) not covered.

## 5. Implementation order (suggested)
1. Throw core: start/aim/release/consume + flight + detonation for Stone/Bomb/Bomb Nuts (reuse `on_bomb`, `ColPool::set`); aim reticle in Godot (bomblock).
2. Status throwables (Cherry/Apple/Peach: just flags 0x100/0x200/0x800 spheres) and gems / Sacred Water (Maseki ShotEffect slots; `ColEntry::only_monster_kind`).
3. Trap circle effects 0-9 (section 3.2; replace the wrong `Notice::Trap` doc) and Dran's Feather.
4. Mimics (section 3.1) - small.
5. Chest trap id (3.3) is the event agent's job; wire `Chest::quality`.
6. View mode: camera + enter/exit, then Xiao/Osmond/Ruby (needs arm models `c04bhand`, `c05arm` for timing).
7. Presentations (4.1/4.2): front-end only; keep gameplay flow (grant item at the end, holds) even if the camera is simplified.

## 6. Tables to read from the ELF / overlay
* `ITEM_LIST` 0x27D0A0 (0x14 each, id - 0x51), `ComItemInfo` 0x27DE50 (name pointers `+0x87c + 4*id`): throw flag, model names.
* Release frames 0x01DC2860 (6 floats); defWeapon table 0x01DC1B00 (also 0x293960); eye heights 0x01DC1B40 (6 floats); Maseki rows DUN.BIN 0x1dc2230 + k*0x70 (k = 0..4); item-open pose chr names 0x27E7D0; strings `maseki_ex.chr` 0x1dc3370.
* Floats: g 0.12 @0x2a1d10, 0.8 @0x2a1bcc, 0.4 @0x2a1b5c, 1.3 @0x2a1da8, eye speed 0.02/0.65 @0x2a1d3c/0x2a1d48, gold 1.2 @0x2a1af8, 0.2 @0x2a1c50, run 1.45 @0x2a1c18.
* Messages: `meswin/sysarticle_1.mes` ids 300-309 (trap), 10/11/20/21/30/31/40 (item got), `dun/message/ww_mes/dunmsd00_1.mes` (floor messages).
* Motions: `base.cfg` of each `dun/mainchara/<c01d|c04b|c06b|c05a|c10b|c18a>.chr` (motions 0x1a/0x1b), `*_ex00.chr/base2.cfg` (42-47), `hand_up.cfg` of c04b / c05a.

## 7. Open questions
* Exact camera animation of the chest/atla sequences (objects `cam`/`int` in the fx models) not extracted; timings above are exact, camera path would need an MDS/motion dump of `d01i02m`, `d01i01m`, `saget.chr`.
* Big-chest item-model offsets in 4.1 are from the decompile and not checked visually.
* Whether `monster.active == 1` revive path exists in `monster_ai` (mimic) - not verified in the port.
* Scripts' effect of gold constant 1.2 (gain is 2.2x gold): from disassembly only, never observed live.
