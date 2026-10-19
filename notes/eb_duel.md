# The EB "Duel" mini-game (event battle): how the original works and how to port it

Research notes, READ-ONLY with respect to the sources. Evidence levels: **READ** = decompile / disassembly of the executable (Ghidra, `work/decomp/all_dun.c`), **VERIFIED-DATA** = computed from the disc data
(`event.stb`, `*.cfg`, `.img`) with the repo tools, **INFERRED** = reasoning that was not confirmed against the running game (nothing here was run in PCSX2).

Source of the code: `ebattle.cpp` of the main executable (`__sinit_ebattle.cpp @0x002a0190`), functions `0x00168100..0x001696c0` (not in an overlay, so it exists in every game mode):

| address | function | address | function |
|---|---|---|---|
| 0x00168100 | `EBInitialize()` (`ebattle_flag = 0`) | 0x00168690 | `EBLoop()` |
| 0x00168110 | `EBInit(float)` | 0x00168b80 | `EBDraw()` (called from the draw routines of event mode) |
| 0x001681f0 | `EBFinishSound(int,int)` | 0x001690e0 | `DrawButton`, `DrawButtonSub` @0x169340 |
| 0x00168200 | `EBInitIntro()` | 0x00169420 | `init_draw_ok`, `set_draw_ok` @0x169440, `draw_ok_loop` @0x169460, `draw_ok` @0x169490, `button_scale` @0x169730 |
| 0x001682b0 | `EBSetMotion(CCharacter*, int*)` | 0x00196600.. | the ext wrappers (below) |
| 0x00168420 | `EBDebug(int)` | | |
| 0x00168430 | `EBSetKey(float, int, int)` | | |
| 0x00168560 | `EBExit()`, `EBIntroLoop` @0x1685c0 | | |

Ext wrappers (EVENT table ids; `n` = number of arguments after the id):

| id | name | address | arguments | behaviour |
|---|---|---|---|---|
| 950 | `_EB_INIT` | 0x196600 | `(speed_f)` | `EBInit(speed)` |
| 951 | `_EB_SET_KEY` | 0x196700 | `(time_f, pad_mask [, level])` | `EBSetKey` (level only read when n > 2; **no script passes it**, always 0) |
| 952 | `_EB_LOOP` | 0x1967a0 | `(&out)` | `out = EBLoop()` |
| 953 | `_EB_SET_MOTION` | 0x196630 | `(chara, motion, motion, ...)` | `EBSetMotion(GetChara(chara), list)`, n < 2 -> returns 0 |
| 954 | `_EB_DEBUG` | 0x196880 | `(v)` | `debug_mode = v` (never used by a script) |
| 955 | `_EB_INTRO_START` | 0x1967e0 | `()` | `EBInitIntro()` |
| 956 | `_EB_INTRO_LOOP` | 0x196810 | `(&out)` | `out = EBIntroLoop()` |
| 957 | `_EB_INTRO_END` | 0x196850 | `()` | `EBExit()` |
| 958 | `_EB_FINISH_SOUND` | 0x1968b0 | `(fade_bgm, play_fanfare)` | `EBFinishSound` (only s24 calls it: `(0, 0)`) |

Reproduce: `python -I tools/dc/stbtext.py dun/script/d01/event.stb 110` (function 110 and the internal functions it calls; names are the EVENT/DUNGEON table names), `python -I tools/dc/stbdis.py ...` for raw
instructions (its default naming prefers the *monster* table, so ids 10..253 are mislabelled; use `stbtext.py` or `event_usage.py` names). Decompile of the EB code:
`work/dec.ps1 <out> EBLoop__Fv EBSetKey__Ffii ...` (Ghidra headless `DecompTo.java`, project `work/gh_title`); all of it is already in `work/decomp/all_dun.c` (EBInit line 64219, EBSetMotion 64281, EBSetKey 64334, EBLoop 64429, EBDraw 64573).

## 0. Corrections to the other notes (VERIFIED-DATA)

* The duel is **not only in d01/d02**: besides `dun/script/d01/event.stb` and `d02/event.stb` the calls are in four story maps (`gedit/s01`, `s24`, `s97`, `s99`; matches `event_ext.csv` "smap:4"). s97 and s99 are the test/gallery copies of the d02 and d01 duels (the dungeon scripts load `gedit/s97/chara/...` and `gedit/s99/chara/...`).
* **d01 has two duels**, and function 110 is only the *wrapper* of the first one (it calls the internal function at record 0x40c94). The other one is in function 300 (floor 7 story scene) -> internal record 0x16330. d02 has two as well (record 0x10800 = function 105, called by function 111; record 0x2759c, called by function 300).
* Every message id the *floor-3* duel uses above 1004 (1005, 1006, 1007, 1008) and d02's 304, 1501, 1502, 1504 is **missing from the embedded message bank** of the stb (d01 bank ends at 1004, d02's has 28 ids). What exists in d01 for the same purpose: 130 "Let's continue where I left off...", 131 "This is the end..", 132 " Try again from immediately before.\n Back to Norune Village.", 133 "Excellent!!\nA present from the Fairies of Terra.\n{fd13} acquired {fbfe}." (these are used by the **floor-7** duel). In the game, `MakeMesWin` with an unknown id finds no text (`GetTextLineDataTop` returns NULL -> `MakeMesWinTbl` returns 0); what the floor-3 duel shows in retail is therefore an empty window (INFERRED). A port should map 1007 -> 132 and 1008 -> 133 (INFERRED fallback; same meaning). Texts of 1005/1006 do not exist anywhere (grep over every `.mes`/stb bank).
* Pad masks of the keys are the **raw `CGamePad::GetPadDown` bits, not exchanged** (section 5). Scripts' own `_GET_PADDOWN` swaps 0x20/0x40, the duel does not.

## 1. What the duel is

A cut-scene fight in which the player never controls the character: Toan (the event model of Toan, e.g. `e28ebc01d.cfg`) and an opponent play a **pre-authored motion queue** (the `_ASQ_*` action sequences) while a rhythm bar runs over the 3D scene. The player has to press the shown
button(s) when each marker reaches a fixed window; there is no HP and no damage. Success = every key pressed in its window; "EXCELLENT" if every key was in the narrow inner window, "CLEAR" otherwise; one error = "MISS" and the script's retry logic. Only Toan can start it (the trigger functions return at once when `_GET_NOW_USER_ID != 0`).

| duel | where / who | packs, cfgs | BGM | rating results |
|---|---|---|---|---|
| **A** d01 floor 3 (`inter2`, fn 102 spawns 3 x e03a skeletons, `_CLEAN_MONSTOR_SCRIPT_NO(110,1)`, fn 110 -> 0x40c94) | Toan vs `e28ebe01a` | `dun/d01/event/e28eb.chr` (`e28ebc01d.cfg`, `e28ebe01a.cfg`), before it `e28c01d/e28e01a/e28eff/e28c03c/pic.chr` | `rec_21d4(20)` = BGM 20 | see section 6 |
| **B** d01 floor 7 (`inter`, marker 300, fn 300 -> 0x8fe8 / 0x139bc intro, 0x16330 duel) | Toan vs c07a (Seeda in notes/opening.md) | `gedit/s99/chara/e17eb.chr` (`e17ebc01d.cfg`, `e17ebc07a.cfg`), fail scene `e22.chr` | - | |
| **C** d02 fn 111/105 (record 0x10800) | Toan vs `e07eb1/e07eb2` | `dun/d02/event/d02ebc01d.chr` (`c01deb02.cfg`), `d02eb02.chr` | BGM 20 | |
| **D** d02 fn 300 (record 0x2759c; needs the right weapon (`_SGET_EQUIP_WEAPON(0) == 12`) and item 240, which it consumes) | Toan vs `e117e54a/e54b` (+ `kira` sparkle model) | `gedit/s97/chara/e117_0.chr` (`e117c01d.cfg`) | sound set 401 | |
| **E** story map s01, function 11 -> 0xf614 -> 0xbde4 (twice, args 101 and 104) -> 0x6904 | Toan vs c06a | `gedit/s01/chara/e101eb.chr` (`e101ebc01d.cfg`, `e101ebc06a.cfg`) | 20 | |
| **F** story map s24, function 128 (looping retry) -> 0x10dd4 | Toan on the flying machine scene | `gedit/s24/chara/e305jump.chr` (hero cfg with motions 100..103), `e305ex.chr`, `kira3.chr` | `_LOAD_BGM(31)` | |

Common to all: the script first loads `img/ebat.pak` (one file `ebat.img`, VERIFIED-DATA) and `_LOAD_SPRITE_TEXTURE("ebat.img")`; `EBInit`/`EBInitIntro` do nothing unless the texture manager has both textures `ebat` and `ebat2` (READ), so the whole duel silently turns into a no-op (EBLoop returns 1) if they are missing.
Atlas `ebat` (128x128, 8 bit IM2): row 0 the four face buttons 32x32 at u = 0 circle (mask 0x20), 0x20 triangle (0x10), 0x40 square (0x80), 0x60 cross (0x40); row v=0x20: up (0,0x20), down (0x20,0x20), right (0x40,0x20), left (0x60,0x20); row v=0x40: up+right (0,0x40), up+left (0x20,0x40); (0,0x60) the yellow "?" hint button, (0x20,0x60) the star sparkle. Atlas `ebat2` (512x256): "EXCELLENT" (0,0,256x70) next to the orange "!" CAUTION sprite (256,0,56x56), "Miss" (0,0x50,146x60), "Clear" (0,0x92,184x64), "OK!" (0,0xd0,26x16), "COOL!" (0,0xe0,40x16). (Rendered and checked by eye; rects are the ones in `EBDraw`/`draw_ok`.) Face-button code order in `DrawButton` (READ): 0x20, 0x10, 0x80, 0x40, then 0x1000 (+0x8000 -> up-left cell, +0x2000 -> up-right cell), 0x8000, 0x2000, 0x4000; any other mask draws nothing.

## 2. State and the behaviour of each function (READ)

Globals (ELF): `ebattle_flag 0x2a268c`, `ebattle_intro_flag 0x2a2690`, `eb_count 0x2a2694`, `eb_intro_cnt 0x2a2698`, `eb_finish_cnt 0x2a269c`, `eb_end_count 0x2a26a0`, `eb_result 0x2a26a4`, `eb_key_count 0x2a26a8`, `eb_chara 0x2a26b0`, `fade_bgm 0x2a26b4`, `play_fanfare 0x2a26b8`, `debug_mode 0x2a26c4`, `eb_key_num 0x2a26d0`, `speed 0x2a1f0c`, `eb_cool_flag 0x2a1f04`, `ok_draw_cnt 0x2a26d4`, `ok_type 0x2a26d8`, `ok_effect_button 0x2a26dc`, `eb_motion 0x266b90` (32 x 20 B), `eb_key 0x1d18f70` (64 x 28 B).
`eb_motion[i]`: +0 motion number (list ends with -1), +4 start (float), +8 end (float), +0xc speed (float), +0x10 ticks of the motion. `eb_key[i]`: +0 tick, +4 pad mask, +8 level, +0xc pressed bits, +0x10 done, +0x14 passed, +0x18 hint.

* `_EB_INIT(s)` = `EBInit`: needs both textures; resets `eb_count = 0, eb_key_num = 0, eb_end_count = 0, eb_key_count = 0, eb_finish_cnt = 0`, `speed = s * 4.0` (**pixels per tick**), `fade_bgm = 1, play_fanfare = 1`, `debug_mode = 0`, clears the "OK!" popup. It does **not** reset `eb_cool_flag` or the motion table.
* `_EB_SET_MOTION(chara, m0, m1, ...)` = `EBSetMotion`: `chara` is the script slot (-1 = hero, `GetChara`); copies the motion numbers, then for each one `GetMotionInfo(chara, motion)` (the 16-byte row of the character's own cfg `KEY start, end, speed`; start/end are *ints* converted with `cvt.s.w`, speed is a float, VERIFIED in the disassembly) and stores `ticks = trunc((end - start) / speed)`; `eb_end_count = sum(ticks)`. If a motion is missing from the cfg the function returns **without** starting (then `EBLoop` reports 1 at once). Success: `ebattle_flag = 1`, `eb_cool_flag = 1`, `CGamePad::MenuModeOn(pad, 0x50)`.
  `MenuModeOn(0x50)` stores 0x50 in the pad's "menu mode" slot (+0x454); `CGamePad::UpDate` (READ) then **ORs the left stick into the d-pad bits** whenever that value is > 0: stick x > 80 -> 0x2000, < -80 -> 0x8000, y > 80 -> 0x4000, < -80 -> 0x1000 (calibrated -128..127; up+down or left+right cancel). That is why the tutorial says "directional buttons or the left analog stick". `EBExit` calls `MenuModeOff`.
* `_EB_SET_KEY(t, mask [, level])` = `EBSetKey` (ignored when `eb_key_num >= 64`): converts the motion time `t` into a **tick index** by walking the motion list in order: for the first motion with `start <= t < end` `tick = trunc(sum_of_earlier_ticks + (t - start) / speed)`, otherwise the whole motion's ticks are added; a `t` in no motion's range ends up as the total. Stores `mask`, `level = min(level, 5)`, pressed = done = passed = hint = 0.
  So **the keys are written in character-motion time, and everything after that is counted in ticks of the `EBLoop` counter** (the game ticks 60 times per second; one `_EB_LOOP` call per script step = per tick).
* `_EB_INTRO_START` = `EBInitIntro`: textures required; `eb_intro_cnt = 0, ebattle_intro_flag = 1`. `_EB_INTRO_LOOP` = `EBIntroLoop`: returns 1 when the flag is 0, else counts `eb_intro_cnt` up and returns **1 on the 101st call** (0 before). Purely presentation: the bar wipes in from the right (`draw_rect = 640 - cnt*640/100` added to the three fill boxes' x), a CAUTION sprite blinks at the screen centre every 4 ticks, SE 8 plays every 8 ticks (both in `EBDraw`). In the scripts it hides the loading of the next pack (`rec_648` loops `until(intro done && load done)`).
* `_EB_INTRO_END` = `EBExit`: `ebattle_flag = 0`, `eb_intro_cnt = 0`, `MenuModeOff`, `ebattle_intro_flag = 0`, `eb_finish_cnt = 0`, clears the popup, **waits until the background special-SE load (`SndSPSeSyncBG`) is finished**, `InitReadBG`. The scripts call it right after the intro and *before* `_EB_INIT` (it is also the function `EBLoop` runs when its result is final).

## 3. `EBLoop` - the whole judging algorithm (READ, `0x168690`)

```
speed S = 4 * EB_INIT arg            tick counter c = eb_count (starts at 0, increments once per call while running)
call EBLoop():
  if !ebattle_flag: return 1                                   // inactive / no textures / motion missing
  if eb_finish_cnt >= 1:                                       // result phase
      if eb_finish_cnt == 1: EBExit(); return eb_result
      eb_finish_cnt--; return 0
  if c == 0 and play_fanfare: start the background load of special SE 0x2f (special/sp47.snd)
  // 1. classify every key
  cand = none; good = 0
  for i in keys (in script order):
      d = (c - key[i].tick) * S                                // pixels past the key's line (float)
      key[i].hint = 0
      mine = i
      if d > 0:   if d >= 48: key[i].passed = 1, mine = cand   // window is over
                  if d < 24:  good = 1                         // inner ("COOL") window of ANY key
      else:       if d <= -16: mine = cand                     // window not open yet
                  if level>0 and d <= 16 - (6-level)*64: key[i].hint = 1   // drawn as the yellow "?" (never used on disc)
      cand = mine                                              // => cand = the LAST key with -16 < d < 48
  // 2. judge
  err = false
  if cand == none:  if GetPadDown() != 0: err = true           // ANY newly pressed bit outside a window is an error
  else:             cand.pressed |= GetPadDown()
                    if cand.pressed == cand.mask:  if !cand.done: { play SE 9 (good==0) or SE 10 (good==1);
                                                                      popup(good, i); eb_cool_flag &= good; eb_key_count++ }
                                                   cand.done = 1
                    if cand.mask != (cand.mask | cand.pressed): err = true          // a bit that is not part of the mask
  for keys i from 0 while key[i].passed:  if key[i].mask != key[i].pressed: err = true // a key whose window ended unfinished
  if err and !debug_mode and eb_key_count < eb_key_num and !EdDebugParamDrawOff:     // after the last hit nothing can fail any more
        SndBgmFadeOut(0x28); eb_finish_cnt = 0x50; eb_result = -1; c++; return 0
  if c == eb_end_count - 100 and fade_bgm: SndBgmFadeOut(100)
  if c < eb_end_count:  draw_ok_loop() (popup timer--); c++; return 0
  // end of the motion list reached without error
  if play_fanfare: wait for the load, SndSPSePlay(0x2f)          // special SE 47, ~6 s fanfare
  eb_finish_cnt = 0xa0; eb_result = (eb_cool_flag != 0) + 1      // 2 = EXCELLENT, 1 = CLEAR
  return 0
```
Consequences (READ unless noted):
* **Return values of `_EB_LOOP`**: `0` running (also during the result display), **`2` EXCELLENT (every hit inside the inner window)**, **`1` CLEAR**, **`-1` MISS**. The result is returned **160 ticks after the end of the motion list** (success; `eb_end_count + 160`) or **80 ticks after the failing tick** (the call that detects the error returns 0, the 80th call after it returns -1). During those ticks the script keeps running its own timeline (sound cues, camera) and the overlay shows the banner.
* Tolerance in ticks, with `o = c - key_tick`: accepted if `-16 < o*S < 48`, inner window if `0 < o*S < 24` (the inner window is *after* the key's tick only; a press before the tick is accepted but never "COOL"):

  | EB_INIT arg | S (px/tick) | accepted offsets | inner (EXCELLENT) offsets | used by |
  |---|---|---|---|---|
  | 0.7 | 2.8 | -5 .. +17 (-83..+283 ms) | +1 .. +8 (17..133 ms) | A, B |
  | 0.8 | 3.2 | -4 .. +14 | +1 .. +7 | F |
  | 0.9 | 3.6 | -4 .. +13 | +1 .. +6 | C |
  | 1.0 | 4.0 | -3 .. +11 | +1 .. +5 | D, E |

  (general: `lo = floor(-16/S)+1`, `hi = ceil(48/S)-1`, `inner_hi = ceil(24/S)-1`.)
* **Pressing anything when no window is open is a miss**, so is pressing a wrong/extra button, pressing too early (before `o*S > -16`), letting a window pass, or any other newly pressed bit (L1/R1/L2/R2/Start/Select count too: `GetPadDown` is the whole 16-bit word). A second press of the *same* button after the hit is harmless while the window is still the candidate; once it is gone it is a miss. A mask with two bits (e.g. 0x9000 up+left) is satisfied when both were pressed at any ticks inside the window (bits accumulate); the rating is taken on the tick that completes it. After the last key is hit, errors are ignored until the end.
* The window of the **next** key takes over as `cand` as soon as it opens, even if the previous key's window is not over (overlap only possible when keys are closer than `(64)/S` ticks; none of the shipped tables overlap).
* No randomness anywhere in the duel (the only random is the script's reward roll, section 6). No health, no combo, no per-key scores: the score is just `all hits inner` vs `not all`.
* `EBLoop` plays SE 9 / 10 (system SE bank: plain hit / COOL hit), `SndBgmFadeOut(0x28)` on a miss, `SndBgmFadeOut(100)` 100 ticks before the end (when `fade_bgm`), special SE 47 on success (when `play_fanfare`). `_EB_FINISH_SOUND(0, 0)` (s24) turns the last two off. The intro uses SE 8.

## 4. Drawing (`EBDraw`, READ) - screen space is 640x448

`MGFillBox` rects are in 1/16 px of the 640x224 *field* (notes/menus.md), so y and h are doubled on screen:
* lane: full-width translucent bar, colour (0,0x28,0xa0), alpha 0x40, y = 320..352 (rect `{0,0xa00,0x2800,0x100}`);
* window frame: white, alpha 0x20, x = 168..232 (64 wide), same y; inner guide: white alpha 0x20, x = 192..208.
  A button sprite has its left edge at `x = 200 - d` (centre `216 - d`), so its centre lies inside the frame exactly while `-16 < d < 48`  - the frame **is** the accepted window (VERIFIED against the thresholds).
* each not-yet-hit key (index >= `eb_key_count`) is drawn by `DrawButton(mask, x = 200 - (c - tick)*S, y = 320, scale = button_scale(i), hint)`: markers enter from the right and move left at S px per tick; culled for x outside -32..640. Icons: ATLAS above; `hint` flag -> the yellow "?" cell.
* hit feedback: `set_draw_ok(good, i)` -> for 30 ticks the hit marker scales 1.0 -> 2.0 (0.2 per tick for 5 ticks) -> 1.0 (5 ticks), the "OK!" (not good) or "COOL!" (good) sprite blinks (visible when `(timer/3)` is odd) at x = 200 - w/2, y = 318 - h, and 8 star sprites fly out radially from the last hit's marker position (alpha fades 0x80 -> 0).
* intro: lane + frames shifted right by `draw_rect`, CAUTION blinks; result: the banner centred at (320,224): `eb_result == 2` EXCELLENT, `1` Clear (both steady), `< 0` Miss (blinks, visible when `(finish_cnt >> 2)` is odd).

## 5. Pad semantics (READ)

`GetPadDown` = bits newly set since the previous frame (`state & ~previous`) of the libpad 16-bit word: 0x10 triangle, 0x20 circle, 0x40 cross, 0x80 square, 0x1000 up, 0x2000 right, 0x4000 down, 0x8000 left, low nibble L2 0x1 R2 0x2 L1 0x4 R1 0x8, 0x100 select, 0x800 start (matches `notes/menus.md` and `CGamePad::UpDate`). **`EBLoop` uses these raw bits.** `_GET_PADDOWN` of scripts passes the word through `exch_ok_cancel` (swaps 0x20/0x40), so a script "OK" (0x20 after the swap) is the raw **cross**, while the duel key mask 0x20 is the raw **circle** and 0x40 the raw cross (the first key of duel B is 0x40 = cross). In the port `EventRuntime::pad_down` already is the engine word and `case 1` applies `swap_ok_cancel`; the duel must read `pad_down` directly.

## 6. What the scripts do with the result (VERIFIED-DATA, `stbtext`)

Common loop: `v = 0; while (v == 0) { _EB_LOOP(&v); [cue SEs and camera at counter values]; yield; }`. The hero's and the enemy's motions are queued with `_ASQ_MOTION_NEXT/PLAY` and started with `_ASQ_MOTION_TRG` *right before the first `_EB_LOOP`*; they run on their own and are **not** changed by hits or misses (a miss only fades the music and the overlay says Miss). So the timeline is the tick counter; the visuals only have to be started at the same moment. Every duel then does:

* **Result 2 (EXCELLENT)**: `_GET_RANDOM(&r); item = r % 12 + 95; _SITEM_GET(item); _SET_STATUS_CHANGE(1,3); _MES_MAKE(1, <id>, 0, item + 100)` (the text "Excellent!! A present from the Fairies of Terra. <name> acquired <item>."), 240 ticks, close. Items 95..106 are the twelve birthstone attach items (Garnet, Amethyst, Aquamarine, Diamond, Emerald, Pearl, Ruby, Peridot, Sapphire, Opal, Topaz, Turquoise; type 0 rows of ComItemInfo, VERIFIED-DATA). Message ids: A 1008 (missing, use 133), B 133 (the only one that exists), C/D 1502 (missing), E `_MES_MAKE(0, 1001, ...)` (window 0 = talk bank `s01talk_1.mes`) and F `_MES_MAKE(1, 1001, ...)` (`s24_1.mes`) are **also absent** from their banks (VERIFIED-DATA: no `1001` in `s01talk_1/s01_1/s24_1.mes`), so in retail the EXCELLENT bonus of C, D, E, F has no text (the item is still given); the port can reuse text 133 of d01 ("Excellent!! A present from the Fairies of Terra. <name> acquired <item>.", needs the system-bank item name `item + 100`).
* **Result 1 (CLEAR)**: nothing extra (B waits 120 ticks).
* **Result -1 (MISS)**: A: `_STOP_BGM; _FADE_OUT(30)`, defeat scene (record 0x3ec8c: hero knocked down, motions 106..108, dim filter), returns 0 to function 110, which opens the 2-choice window (text 1007 -> use 132): choice 0 "Try again" re-runs 0x40c94 with `v0 = 1` (skips the story intro and the 60-tick wait) , choice 1 `_MAP_JUMP(1)` (Norune). B: `rec_14dd8` (defeat scene with texts 131 and 132); choice 0 returns 0 -> function 300 loops (flag 37 is set, so it plays the shorter intro 0x139bc with text 130 and repeats 0x16330), choice 1 `_MAP_JUMP(1, -1)`. C: window 1007, choice 1 `_MAP_JUMP(2)` (Matataki), else repeat. D: returns -1 -> `rec_23c4c(0)` (message, event ends, item kept). E (s01): no retry window; a miss plays the defeat scene `0xd174` and the scene goes on with a second fight (record 0xbde4 with arg 104, text 10401 "You never learn..."), which has the same win/lose branches. F: the whole function 128 loops forever until the duel is won.
* **State the story reads afterwards** (everything is written by the script itself through ext functions the runtime already has): A: `_SSET_GAME_FLAG(102, 1)` (set by function 110 on any non-zero result), IBox at (-8,0,0) item **195 (Dran's Crest, the gate key)** always, and at (8,0,0) **178 Powerup Powder if EXCELLENT, else 112 Undead Buster** (record 0x40c94 end; hero placed at (0,0,80) yaw 3.14). B: `_SSET_GAME_FLAG(25,1)`, `_SET_ATRA(157,0,130,5)`, `_SET_EVENT_SW(300,0)` (function 300). C: `_SSET_GAME_FLAG(111,1)`. D: flag 48, `_SITEM_LOST(240)`. E: result > 0 -> `_SSET_GAME_FLAG(3,1)`, `_FINISH()`, epilogue scene 0xda34 (set by the first or the second fight); the later `_MAP_JUMP(2, 301)` is at the end of s01's function-301 scene. F: flag 38, `_MAP_JUMP(23, 300)`. So the **only value the engine must hand back is `out` of `_EB_LOOP`** (-1/1/2); the stub's constant 1 makes every duel a CLEAR, i.e. it loses the birthstone and, for A, the Powerup Powder (Undead Buster box instead).

### Key tables (VERIFIED-DATA; ticks computed as `EBSetKey` does it, float32, truncating)

`script time t -> tick = trunc(sum_earlier_ticks + (t - start)/speed)`; cfg motion rows: A: motion 250 = 10..132 @0.3 (406 ticks); B: 300 = 10..23 @0.23 (56), 301 = 24..55 (134), 302 = 56..143 (378), 303 = 144..217 (317); C: 300 10..30 @0.2 (100), 301 31..43 (60), 302 46..51 (25), 303 52..83 (155), 304 84..128 (220); D: 100..109 = 150,100,100,120,70,60,70,80,100,66 ticks; E: 100..103 = 244, 35, 40, 165 ticks; F: 100 = 22..65 @0.27 (159), 101 = 66..93 (99), 102 = 94..113 (70).

| duel | init (S) | motions | end tick (+160) | keys: `(t -> tick, mask)` |
|---|---|---|---|---|
| A (d01 fl.3, fn 110) | 0.7 (2.8) | -1: 250 | 406 | (33 -> 76, 0x8000 left) (51 -> 136, 0x20 circle) (96 -> 286, 0x8000) (110 -> 333, 0x20) (117 -> 356, 0x20) |
| B (d01 fl.7 / s99) | 0.7 (2.8) | -1: 300 301 302 303 | 885 | (33 -> 95, 0x40 cross) (42 -> 134, 0x8000) (144 -> 568, 0x20) (163 -> 650, 0x20) |
| C (d02 fn 105/111) | 0.9 (3.6) | -1: 300..304 | 560 | (54 -> 195, 0x8000) (79.5 -> 322, 0x40) (87.5 -> 357, 0x10 triangle) (101 -> 425, 0x10) (115 -> 495, 0x20) |
| D (d02 fn 300 / s97) | 1.0 (4.0) | -1: 100..109 | 916 | (37 -> 135, 0x1000 up) (48 -> 360, 0x10) (59 -> 415, 0x20) (68 -> 460, 0x2000 right) (77 -> 500, 0x10) (94 -> 610, 0x40) (97 -> 640, 0x2000) (275 -> 866, 0x20) (283 -> 893, 0x20) |
| E (s01) | 1.0 (4.0) | -1: 100..103 | 484 | (42 -> 128, 0x4000 down) (49 -> 156, 0x1000) (60 -> 200, 0x10) (69 -> 236, 0x9000 up+left) (91 -> 339, 0x20) (95 -> 359, 0x20) |
| F (s24) | 0.8 (3.2) | -1: 100 101 102 (hero cfg of `e305jump.chr`) | 328 | (52 -> 111, 0x20) (61 -> 144, 0x8000) (70 -> 173, 0x10) (78 -> 203, 0x40) (94 -> 258, 0x4000) (105 -> 298, 0x10) |

Consistency check that the tick conversion is right: in A the script's own sound cues fire at counter values 333 and 357 (`_PLAY_SE(400)`, `_PLAY_SE(401)`), i.e. at the ticks of the keys 110 and 117 (333, 356); its final camera cuts are at 406/407 = `eb_end_count`; B's cues at 95 (= key 33), 135 (~key 134)...
Script `_EB_SET_KEY(t, mask)` second argument is the mask as an int literal (32768 = 0x8000, 32 = 0x20 ...); no script passes the level argument.

Snippet to regenerate a table (needs `tools/dc/pack.py`; cfg `KEY a, b, c` rows, number = `KEY_START` + row index):
```python
import numpy as np
def key_tick(t, motions, rows):            # rows[m] = (start, end, speed), floats
    acc = 0
    for m in motions:
        s, e, v = (np.float32(x) for x in rows[m])
        if s <= np.float32(t) < e:
            return int(np.trunc(np.float32(acc) + (np.float32(t) - s) / v))
        acc += int(np.trunc((e - s) / v))
    return acc
```

## 7. Implementation plan in the existing port

State of the port (READ): `event_runtime.cpp:1233-1235` accepts 950/951/953/954/955/957/958 and answers 952/956 with `out = 1`; `DungeonSession::tick(pad_on, pad_down, alive)` forwards the engine pad word (`EventRuntime::pad_down`, no OK swap); `DcWalker::step_ticks` (gdext `dc_walker.cpp:~200`) computes `down = buttons & ~session_prev_buttons_` and ticks the session every game tick while an event runs; the dungeon stage (`DungeonEventHost`: `actor_motion_info`, `ev_motion_info`, `get_motion_info` with `start/end/speed`) already plays the ASQ motions of the actors.

1. **dccore: new pure class `EbDuel`** (`include/dccore/eb_duel.h`, `src/eb_duel.cpp`, no I/O, deterministic, unit-testable): `init(float arg)`, `set_motions(std::vector<Motion{no,start,end,speed}>)` (ticks = trunc((end-start)/speed) in float), `set_key(float t, uint32_t mask, int level)` (max 64), `int step(uint32_t pad_down_raw)` = section 3 verbatim (return 0/1/2/-1, internal `count`, `finish_cnt`, key array with `pressed/done/passed/hint`), intro counter `intro_start()/intro_step()` (returns 1 on the 101st call), `exit()`, `finish_sound(fade, fanfare)`, `debug`. Expose a read-only `draw_state()` (phase intro/duel/result, `count`, `speed`, keys with `tick/mask/done/hint`, `key_count`, popup `{type, button, timer}`, `result`, `finish_cnt`, `intro_cnt`) and an event list for the sounds (`Se{8,9,10}`, `BgmFade{40}`, `BgmFade{100}`, `Fanfare(special 47)`), drained by the runtime each tick.
2. **`EventRuntime`** (replace the stub `case`s at event_runtime.cpp:1233): member `EbDuel eb;`
   * 950 `eb.init(F(0))`; 953: for every motion call a **new host method** `bool EventHost::actor_motion_range(int slot, int motion, float& start, float& end, float& speed)` (default `false`; the existing `actor_motion_info` returns `blend = 2` and `true` for unknown motions, so it cannot be used: the original aborts silently when a motion is missing, `EBLoop` then returns 1) -> `eb.set_motions(...)`; 951 `eb.set_key(F(0), I(1), n > 2 ? I(2) : 0)`; 952 `a.set_int(0, eb.step(pad_down))` using the **unswapped** `pad_down`; 955/956/957/958 as in section 2; 954 debug.
   * Left stick -> d-pad bits: the original does this inside the pad layer while `MenuModeOn(0x50)` is set, so do it where the pad word is built (below), driven by `eb.active()`.
   * Route `EbDuel` sound events through the existing `EventHost::sound(op, a, b, v)` with new op strings (`"_EB_SE"`, `"_EB_FANFARE"`, `"_EB_BGM_FADE"`).
3. **`DungeonSession`**: needs only `bool eb_active() const { return rt_->eb.active(); }` for the walker, and, for d01, the message fallback (ids 1007 -> 132, 1008 -> 133 in `event_bank` when absent). Everything the story needs afterwards (`_SSET_GAME_FLAG`, `_SITEM_GET`, `_SET_IBOX`, `_MAP_JUMP`, `_RUN_SCRIPT_NO`) exists already; nothing besides the `_EB_LOOP` result has to be returned.
4. **gdext `DcWalker`** (`dc_walker.cpp`): (a) `DungeonEventHost::actor_motion_range` -> `ev_motion_info` but with the `speed` of `DcCharacter::get_motion_info(motion)` for the actor loaded in that slot **by the event** (slot -1 is re-loaded with `e28ebc01d.cfg` etc., so look in `ev_actor_nodes_[-1]` first, not in the player's base cfg: motions 250/300.. exist only there; return false if absent); (b) `read_devices()`: add triangle (0x10) and d-pad up/down (0x1000/0x4000) to the existing OK 0x40 / NO 0x20 / USE 0x80 / d-pad left,right 0x8000/0x2000 bits, and while `session_->eb_active()` OR the left stick into 0x1000/0x2000/0x4000/0x8000 with the threshold 80/128 of the calibrated axis (`|v| > 0.625`), up+down and left+right cancelling; NOTE the port's camera/lock bits 1, 4, 8 (Q/E/R keys) must not be forwarded as presses (in the original L1/R1/L2 *are* errors) - choose and document; (c) `DungeonEventHost::sound(...)` is currently the empty default: forward `_PLAY_SE`/`_EB_*` as notices (kind 31 `a = se id` already plays `play_se(a, -1, 0)`; add kinds for special SE 47 (`play_special(47)`) and BGM fade); (d) `get_eb_ui()` returning the draw state (section 4 quantities) next to `get_event_ui()`.
5. **Godot** (`native/godot/dungeon_run.gd`): in `_make_event_ui` add a `Control` child of `ev_layer` (below `ev_fade`, above the 3D view) drawing the overlay in 640x448 virtual coordinates scaled to the window: lane rect (y 320..352, colour (0,40,160, 0.25)), frame x 168..232 and inner x 192..208 (white, alpha 0.125), markers from `ebat.img:ebat` (`DcModels` image loader of `img/ebat.pak`), popups/stars, intro wipe + CAUTION, result banner from `ebat2`; `_update_event_ui` pulls `get_eb_ui()`. Message windows (retry choice, reward text) already go through the generic event windows; the 2-choice window of `rec_4bdc/rec_426c` is the existing choice code of town scripts (cursor via d-pad/stick, `_GET_PADDOWN & 0x20` after the swap = cross). A first playable version needs **no 3D**: the timeline is the tick counter, the ASQ actors can be added when the stage draws them (they are already ticked).
6. **Tests/validation**: unit tests for `EbDuel` (inner-window press -> 2; press at offset -3 -> 1; wrong bit / press with no window / window left open -> -1 returned exactly 80 calls after the failing call; success returned exactly at `end + 160`; two-bit mask split over two ticks; errors ignored after the last hit; `S = 2.8` accepted offsets -5..17, inner 1..8); a headless replay through `dungeon_trace`/`stbrun` hosts with a "perfect player" feeding `pad_down` at `tick + 4` of section 6; golden outcomes: A perfect -> out 2 + boxes 178 and 195 + flag 102, A with one early press -> out 1 + boxes 112 and 195. Optional live check with the PINE rig: break at `EBLoop` (0x168690) and compare `eb_key` (0x1d18f70) ticks and `eb_end_count` (0x2a26a0) with section 6 (not done).

## 8. Open / not verified

* Whether the first `EBLoop` call sees the actors one frame ahead of or behind their motion clocks (the `_ASQ_MOTION_TRG` calls come just before the loop; the script cues are tuned to the counter, so a 0/1 tick offset is not visible).
* The ramp/blend of the actors' motions is not needed for the judging; keys are only defined through the cfg `KEY` rows, which are read at run time (the port must use the same cfg rows of the event-loaded pack, not the base character cfg).
* Exact star-sparkle directions (`dir_588`, 8 vectors) and the sprite alpha ramps were not decoded (decorative).
* `MenuModeOn(0x50)`: the effect described is the stick-to-d-pad conversion in `UpDate` (READ); other users of the value were not found.
* Retail behaviour of the floor-3 duel windows with missing ids 1005/1006/1007/1008 (INFERRED empty windows); the existing "floor-3 duel result is skipped" behaviour of the port corresponds to `out = 1` there.
* What the 4 story-map duels are in the story order (s01, s24 are reached by `_MAP_JUMP` from other story scenes; not traced here).

## 9. Summary

* The duel is `ebattle.cpp` in the main executable (EB* at 0x168100..0x1696c0, ext wrappers 0x196600..0x1968b0); used by d01 (floor 3 via function 110 -> record 0x40c94, floor 7 via function 300 -> 0x16330), d02 (functions 111/105 and 300) and the story maps s01, s24 (+ test copies s97, s99): 6 distinct duels.
* The player does nothing but press buttons: markers scroll right-to-left at `4 * EB_INIT arg` px/tick (0.7..1.0 -> 2.8..4.0) on a lane at y 320; hero/enemy motions are pre-queued ASQ sequences that never react to hits.
* `_EB_SET_MOTION(chara, motions...)` fixes the timeline (sum of `(end-start)/speed` of the cfg KEY rows); `_EB_SET_KEY(t, mask)` converts motion time to a tick; max 64 keys; mask = raw `GetPadDown` bits (0x10 tri, 0x20 circle, 0x40 cross, 0x80 square, d-pad 0x1000/0x2000/0x4000/0x8000) - not OK/cancel swapped; the left stick works as d-pad (threshold 80).
* Judging per tick: window `-16 < d < 48` px past the key line (d = (count - tick) * S), inner "COOL" window `0 < d < 24`; the last key whose window is open is the candidate; ANY extra/wrong/early/late press or a window that closes unfinished = MISS (after the last key everything is ignored).
* `_EB_LOOP(&out)`: 0 running, 2 EXCELLENT (all hits inner), 1 CLEAR, -1 MISS; result is returned 160 ticks after the motion list ends (success, fanfare = special SE 47) or 80 ticks after the error (BGM fade 40).
* No randomness in the engine; the reward roll is the script's: EXCELLENT -> `_SITEM_GET(95 + rand % 12)` (a birthstone) + message; d01 floor 3 also swaps the box item Undead Buster (112, CLEAR) for Powerup Powder (178, EXCELLENT), always adds Dran's Crest (195) and sets flag 102 on any win.
* MISS: scripts play a defeat scene and offer "Try again / Back to the village" (retry re-enters the duel function; text ids 1005..1008 of d01 floor 3 and d02's 1501..1504 are absent from the banks, d01 has 130..133 for the floor-7 duel).
* Key tables of all six duels are in section 6 (ticks computed from the cfg `KEY` rows, e.g. d01 floor 3: ticks 76/136/286/333/356 of 406, masks left, circle, left, circle, circle).
* Plan: `EbDuel` pure class in dccore + `EventRuntime` cases 950..958 (replace the stub at event_runtime.cpp:1233) + host method `actor_motion_range` + pad word with triangle/up/down and stick-as-d-pad in `DcWalker::read_devices` + `get_eb_ui()` + a 2D overlay in `dungeon_run.gd` using `ebat.img`; the script already writes all flags/items itself, so only the `_EB_LOOP` result must be returned.
* Not verified live: counter/motion alignment, decorative sparkle details, the retail look of the floor-3 duel with its missing message ids.

## 10. Port state (2026-10-19)
* Implemented: `dccore/eb_duel.*` (the judging rules of section 3, unit test `tests/test_eb_duel.cpp`: window edges, inner window, extra buttons, 80 / 160 tick results, two-bit masks, key ticks), `EventRuntime` cases 950..958, `EventHost::actor_motion_range`, `DungeonSession::eb_state / eb_active`, the pad (triangle on T / joystick Y, the stick as a d-pad and no camera / lock bits while the duel runs, `DcWalker::step_ticks`), overlay `godot/eb_overlay.gd` (lane, frame, markers from `ebat.img`, popups, banners), sounds (notice kind 34).
* Needed for it: **motion-only configs** (a `.cfg` without MODEL holding `MOTION n, "x.mot" ... KEY_START 250 ... MOTION_END`): `Character::extra_sets / extra_motions`, `add_motion_sets`, `DcCharacter::set_character`; the dungeon and the town host add them to the actor already in the slot (before, such a config replaced the actor with the first model of the pack).
* Verified: d01 floor 3 (`play.tscn --dungeon 0 --floor 3 --run-function 110 --eb-auto 4`): keys at ticks 76 / 136 / 286 / 333 / 356 (exactly the table of section 6), a perfect player gets EXCELLENT (result 2) and the story goes on; pressing anything else (even the test script's attack presses) is a MISS and the script offers the retry.
* Town (story maps s01 / s24): `DcTownWalk::step_event` builds the duel's pad word (cross 0x40, circle 0x20, triangle T / Y 0x10, square Z / X 0x80, d-pad, the left stick as d-pad while the duel runs), `get_eb_ui` feeds the same `EbOverlay` (`TownRun._make_ui`); `town_play.tscn --town s01 --event 11 --eb-auto 4 --script cross:2,none:40,...` plays duel E perfectly (6/6 hits, result 2, the dialogue goes on). s24's duel F was not played to its end in the harness.
* Not done: the sparkle details, the fanfare's exact playback.
