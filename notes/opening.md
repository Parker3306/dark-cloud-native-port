# The opening cutscene (new game, `MapJump(400)`) - specification for the native port

Source of truth: the **title overlay `TITLE.BIN`** (`extracted/TITLE.BIN`, link address 0x01DABD00, same address as DUN.BIN). It was decompiled in a *copy* of the Ghidra project
(`work/gh_title`, overlay mapped by `work/ghscripts/AddOverlay2.java` = `AddOverlay.java` without `analyzeAll`, symbols from `notes/symbols.csv` section `title`, 1648 names).
Helpers: `work/dec.ps1 -Out f.c -Names Func,...` (decompile, names or `0x01xxxxxx`), `work/dis.ps1 start end out.asm` (force disassembly), `work/annot.py` / `work/strannot.py`
(annotate calls / string pointers), raw dumps `work/title_dec*.c`, `work/title_A.c`, `work/title_BCD.c`. Data tools: `tools/dc/opening_dump.py`, `opening_sne.py`, `opening_stage.py`.
Generated listings: `notes/opening_timeline.txt` (every command with time, English subtitle), `notes/opening_objects.txt` (motion tables of the 23 stage objects), `notes/opening_stage_tables.txt` (placement tables).

Confidence tags: **[V]** read directly from the decompiled code or verified against the data, **[D]** verified against the data only, **[G]** guess / inferred.

## 1. Overview

* Main loop mode 5: `OpeningInit()` once (0x01DAF1C0), then `OpeningLoop()` (0x01DAF970) once per main-loop iteration (one `MGEndFrame` per iteration). When it returns true the main loop does `MapJump(0,-1)` (Norune map 0, event 128 ...). [V]
* It is **not an FMV**: a script (`opdat/opening.scr`, 1416 commands) drives real-time 3D models, a camera path per cut (`opdat/scene/*.sne`), subtitles in speech balloons, sound effects and music.
* `OpeningLoop` per frame [V]: `ReadBG()` (background CD stream), `PauseProcess()`, then if not paused `CScript::Step()` + `WaitKeyProcess()` + `SceneChange()`; then always `MotionProcess()`, `SoundProcess()`, `DrawProcess()`. If the script object's end flag (`CScript+0x10`, set by `END`) is set the loop returns true; if `End` (skip) is set it runs 0x80 more frames then returns true (`SoundStop`, background black, `FadeCansel`). Otherwise `CSound::Step`.
* The opening has **4 parts / 10 scene indices** (selected by `SCENE n`, dispatch tables at 0x01DDD740..0x01DDD800): 

| scene idx | part | what | config | data |
|---|---|---|---|---|
| 0 | A | cursed dance ritual in the Dark-Genie cave (jar, smoke, Majin appears, eats the priest) | `opdat/opinfo.cfg` | `dungeon/*` , `chara/01*.chr` |
| 1 | B | Norune village at night (Paige walks) | `opdat/norn.cfg` | `norn/norn.pak`, `norn/*.mds` |
| 2 | B' | Toan's house (Rene, Toan, Paige knocks) | `opdat/toan.cfg` | `toan/toan.pim`, `toan/t0101.mds`, `chara/03*.chr` |
| 3,4 | C | Norune festival night; village dancers | `opdat/norn2.cfg` | `norn2/norn2.pim`, `norn2/norn2b.pak`, `norn2/ndance.pak` |
| 5 | C | Majin + Flag appear over the village (moon shadow, bust-ups) | `opdat/norn2.cfg` | `norn2/majin.pak`, `norn2/majinb.pak` |
| 6,7 | C | the destruction of Norune, burning village | `opdat/norn2.cfg` | `norn2/norn2.pim`, `norn3/norn3.pak` |
| 8,9 | D | Spirit King (gives Toan the Atlamillia), 2D explanation screens, flowers/butterflies | `opdat/seirei.cfg` | `norn4/seirei.pak`, `norn4/seirei2.pak`, `norn4/*.img` |

Total script length (this port's simulation, WAIT_KEY = 2 steps): **38,180 steps = 10 min 36 s at 60 steps/s** (125 camera cuts). Real time depends on the player's key presses (24 `WAIT_KEY`) and on frame drops on the PS2.

## 2. Timing model [V unless noted]

* **Time unit = one `CScript::Step` call = one main-loop iteration (assumed 60 per second; `mgWaitVSync` is 0 so there is no extra vsync wait; the PS2 may run slower in heavy scenes, the script does not compensate) [G for the 60].**
* **There is no frame counter in seconds: everything is measured in "camera frames".** The camera clip advances by `speed` camera-frames per step (speed = 4th argument of the last `CAMERA`), and `WAIT n` is n camera-frames, i.e. it lasts **n / speed steps**. So the same `WAIT` is longer in a slow (small speed) cut.
* Exact algorithm (`Step` 0x01DABEE0), with `c = CScript+0x28` (float), `sp = CScript+0x4E4` (the camera speed):
  ```
  Step():
     if end flag: return
     if waitkey flag (+0x24): c = sp + 1.0              # holds the script
     if c < 1.0:  execute commands in file order until one of END / WAIT_KEY / WAIT has been executed (exit this Step)
     else:        c -= sp
  WAIT n  : c = n - 1.0                      (so the next command runs on the step j = floor((n-2)/sp) + 2 after the WAIT step; j = 1 for n < 2)
  WAIT_KEY: waitkey flag = 1  (cleared by WaitKeyProcess, see 7)
  ```
  Several consecutive `WAIT`s simply add up. Everything else (`OBJ_*`, `CAMERA`, `SE`...) executes with zero time, many per step.
* All other "frames" arguments (`OBJ_EYE/MOUTH/TALK/MESSAGE` durations, eye/mouth timers, message life) are also **counted down by `sp` per step** (they are in camera-frame units too). Object motions are NOT in camera units: a motion advances by its cfg `KEY` speed in motion frames per step (see 5; `OBJ_STEP` only sets the cross-fade rate).
* The **camera clip time** of the current cut: `t = start` at the step the `CAMERA` command executes (copied from the script: `Op_MotionInfo = {start, end, speed}`), then every step: `t = min(t, end-1)`; evaluate the clip at `t`; `t += speed`. The clip therefore holds on frame `end-1` once it ran out (no looping in practice). While a `WAIT_KEY` is active the camera is frozen (`t` is reset to its value at the start of the wait). [V: MotionProcess 0x01DB0090, SetMotionEX]
* Pause (START, mask 0x800 on the pad): stops the script and sound, music volume ducked; pressing mask 0x20 (circle) resumes, mask 0x40 (cross) = **skip the opening** (fade out 1.0 then end). [V: PauseProcess 0x01DAFC50]

## 3. Script file format and the 26 commands

Text, Shift-JIS comments, CRLF. `CScript::Step` skips white space and `//...` comments, then **matches the command name by prefix against a table in this order** (table at 0x01DD5380, 0x2C bytes per entry {name ptr, length, argc, 8 x arg type}): `OBJ_MOTION2, OBJ_MOTION, OBJ_MOVE, OBJ_TALK, OBJ_EYE, OBJ_MOUTH, OBJ_DISP, OBJ_STEP, CAMERA, SE_STOP, SE, SOUND_FADE, FADE_IN, FADE_OUT, MESSAGE, NEXT_MES, MES_CLR, LOAD_OBJ, SPRITE, BOM, BEEM, SCENE_LOAD, SCENE, END, WAIT_KEY, WAIT` (the order resolves the prefix clashes). Arguments are numbers read with `atof`, separated by commas (`OBJ_DISP 3, ON` -> `ON` = 1.0, `OFF` = 0.0; ints are floats converted with `fptosi`). Anything else -> `exit(-1)`. All arguments are stored as floats at `CScript+0xE0C8+4*i`, then `CheckScript` (switch at 0x01DDBEC0) copies them into the CScript object (0x01DE1B00, 58,112 bytes): **commands do not act by themselves, they set fields that `MotionProcess`/`SoundProcess`/`DrawProcess`/`SceneChange` read every frame** (so a repeated command is idempotent, and the last one before the frame wins).

Object records: object `i` (0..22) owns 0x34 bytes at `CScript + 0x30 + 0x34*i`:
`+0x00 byte disp | +0x04 int motion | +0x08 int motion2 (-1 none) | +0x0C int move key (unused!) | +0x10 byte move-enable | +0x14 int eye anim | +0x18 float eye time | +0x1C int mouth anim | +0x20 float mouth time | +0x24 byte talking | +0x28 int load slot | +0x2C int loaded state | +0x30 float step (motion speed multiplier)`.

| command | arguments (types) | effect [V] |
|---|---|---|
| `OBJ_DISP o, ON\|OFF` | object 0..22 | `disp` flag. Only displayed objects are animated/drawn. Initial state: all OFF except 6 and 7 (dance leader, dancers). |
| `OBJ_MOTION o, m` | motion index m (= `KEY` line number of the object's cfg, section 5) | `motion = m; motion2 = -1`. The motion loops inside its `KEY` range. On a change the character cross-fades from the current pose to the first frame of the new range at the object's blend rate (`OBJ_STEP`, default 0.05). |
| `OBJ_MOTION2 o, a, b` | | play motion a; when its time passes `end-1` of the KEY range it switches to b (`motion = b; motion2 = -1`). Used for "turn then idle" (e.g. 8,1,2). |
| `OBJ_STEP o, s` | float | stores `s` in the character's `MOTION_STATE[2]` (`Chara+0x2F8`) = **blend rate used when its motion changes**: the pose cross-fade advances by `s` per step (1.0 = snap/instant, 0.05 = 20 steps = default value set at load, `uGpffff8100`). It does not change the playback speed of a running motion (that is `KEY.speed`). Typical use: `OBJ_STEP o,1.0` just before an `OBJ_MOTION` that must start at once, `OBJ_STEP o,0.05` to go back to soft transitions. (Majin 0.01 = very slow blend.) |
| `OBJ_MOVE o, key` | second argument is stored but **never read**; only sets `move-enable` | From then on (scene A, objects 0..7) the object is placed every step at the pose of the **node named after it in the current camera clip** (section 4). In scenes B/C/D the placement of objects 8..22 happens every step for every *displayed* object whether or not OBJ_MOVE was issued. |
| `OBJ_EYE o, idx, frames` | eye cell index, duration | selects row `idx` of the eye atlas for `frames` (camera-frame units) then back to 0 (then automatic random blinks: `rand()%200==0` per step starts a 3-step blink cycle 1,2,..). `0` = default open eyes. Indices are character specific, see the comments in the script (3 = look left, 4 = look right, 6 = glare, ...). Random blink: eye index ramps 0,1,2 (max = 2, `blink max` word of the face table) one step at a time and back; it is suspended while an `OBJ_EYE` is active. [G on the exact atlas rows] |
| `OBJ_MOUTH o, idx, frames` | mouth cell, duration | same for the mouth atlas. |
| `OBJ_TALK o, msg, frames, x, y, tx, ty` | o, message id of the subtitle bank, life in camera-frames, balloon position (screen px, 640x448), tail target px | opens a speech balloon with message `msg`; sets object o `talking` (random mouth 0..3 flapping every 6th tick of its mouth timer for `frames`). `tx = 0` -> no tail (`tail width` 0), else the tail points to (tx, ty). See section 7. |
| `MESSAGE msg, frames, x, y, tx, ty` | | same balloon without a speaker object (narration / off-screen voice). |
| `NEXT_MES n` | frames | `ClsMes+0x16B8 = n`: the auto-page timeout for the next multi-page message (a page break `\p` inside a message auto-advances after n ticks). Used before the messages that contain `\p`. |
| `MES_CLR` | | `message id = 0`: close the balloon now. |
| `WAIT n` / `WAIT_KEY 0` | camera-frames / flag | see section 2 / section 7. |
| `CAMERA key, start, end, speed` | | **starts the next camera clip** (section 4). `key` = cut label (sound/effect triggers and near-plane/shake rules test it), `start..end` = frame window of the clip, `speed` = camera-frames per step (also the wait/timer unit). |
| `SCENE_LOAD n` | 1..9 | requests the **background CD load** of the data of scene n (`SceneChange`, no frame hitch): n=1 `norn/norn.pak`, 2 `toan/toan.pim`, 3 `norn2/norn2.pim`, 4 `norn2/ndance.pak`, 5 `norn2/majin.pak`, 6 `norn2/norn2.pim`, 7 `norn3/norn3.pak`, 8 `norn4/seirei.pak`, 9 `norn4/seirei2.pak` (to a streaming buffer; the request clears itself). |
| `SCENE n` | 1..9 | switches scene: `current scene = n` (selects the Motion/Sound/Draw process of table 0x1DDD7A0/D7D0/D800) and runs the synchronous `Op?_InitProcess` that builds the scene from the streamed data: 1 `SoundStop`+`OpB_InitProcess`, 2 `OpB_InitProcess2`, 3 `SoundStop`+`OpC_InitProcess`, 4 `OpC_InitProcess2`, 5 `SoundStop`+`OpC_InitProcess3`, 6 `OpC_InitProcess4`, 7 `OpC_InitProcess5`, 8 `SoundStop`+`OpD_InitProcess`, 9 `SoundStop`+`OpD_InitProcess2`. Scene 0 (A) is built by `OpeningInit`. Always preceded by a fade out; followed by `FADE_IN`. |
| `LOAD_OBJ slot, o` | | (background) load of the extra model of object o into data buffer `slot`: table of `chr` files `01c07a,01c08a,01c11a,01c09a,01c08b,01c08c` (+cfg names) indexed by o. Scene A: `LOAD_OBJ 6,4` = `chara/01c08b.chr` (Majin bust-up, object 4) and `LOAD_OBJ 4,5` = `chara/01c08c.chr` (Majin hand, object 5). Third use in scene C: Majin bust-up (`majinb.pak`). Just preload these two files before the object is shown. |
| `SPRITE n` | int, `ON`=1 `OFF`=0 | effect id `CScript+0x14`, **meaning depends on the current scene** (section 9). |
| `BOM x, y, z, w` | 4 floats | spawn an explosion ("bomb") at (x,y,z) of size w; ring of 3 pending (`+0x520`). Scene C only (section 9). |
| `BEEM x1,y1,z1, x2,y2,z2` | 6 floats | spawn a beam/light streak from p1 to p2 (ring of 3); `BEEM -1,0,0,0,0,0` = "no geometry" (just the flash). |
| `SE ch, prog, key, pan` | see section 8 | one-shot sound effect. `pan` is not used by `SoundProcess`. |
| `SE_STOP ch, prog, key` | | stops that effect (`SE_Stop`). |
| `SOUND_FADE ch, target, rate` | ch must be -1 (all) | fades every music/SE port to `target` at `rate` per step (section 8). |
| `FADE_IN c, speed` / `FADE_OUT c, speed` | c: 0 black, other = white | full-screen fade. `CDispFade`: alpha 0..128, +/- `speed` per step (FADE_OUT: alpha += speed up to 128; FADE_IN: alpha -= speed down to 0). Initial state at start-up: alpha 128 (black), `FADE_IN 0,2.0` at the first cut = 64 steps. `speed >= 128` = instant. (`MotionProcess` applies it.) |
| `END` | | end flag; `OpeningLoop` returns true. |

## 4. Camera and actor tracks: the `.sne` files

* `opdat/scene/*.sne` (129 files; the opening plays **125 clips = 122 distinct files** from the table below; `0101cp 0106cp 0422cp 0424cp 0507cp 0508cp 0509cp` are unused cut leftovers). Format (verified): an ordinary pack (`tools/dc/pack.py`) with `<n>.cfg`, `<n>.mds`, `<n>.mot`. Parsed by `tools/dc/opening_sne.py`:
  * `.mds`: object 0 `dummyRoot`; then one object per **actor placeholder node** named after the actor model: `c07a` Seeda, `c08a` Majin, `c11a` Priest, `c09a` Flag, `c08c` hand, `p19a` dance leader, `p17a1`..`p17a35` the 35 dancers, `c01d` Toan, `p09a` Paige, `p10a` Rene, `p01a p02a p03a p05b p07a p08a p12a p15a(1..6) p16a(1..) ...` village people, `c03c` Spirit King, `tyou/tyou2` butterflies, `hikari` light, `point`, `end`, `moon`, ... plus `cam`/`int` nodes. The node's rotation part is the actor's **facing**, constant; its translation row is the start position.
  * `.mot`: the usual track chain (0x20-byte header + 0x20-byte keys, key time = absolute frame number): type **2** = translation of node `object` (x,y,z); type **30** (0x1E) camera position, **31** (0x1F) camera target (look-at), **33** (0x21) vertical field of view in degrees (value in `.x`), (32 = roll in degrees, supported by the code, unused). Linear interpolation between the bracketing keys: `f=(t-t0)/(t1-t0)`; before the first key nothing is applied (the previous value stays).
* **The Nth `CAMERA` command of the script plays the Nth `.sne` of the table at 0x01DD5820 in TITLE.BIN** (125 pairs {path ptr, cfg-name ptr}; `opening_dump.load_sne_table()`): `0101acp, 0101bcp, 0104cp, 0104bcp, 0105cp, 0107cp, 0108cp, 0109cp, 0110cp, 0111cp, 0112cp, 0113cp, 0114acp...0114kcp, ..., 0529cp` (some files appear twice: 0126cp x3, 0120cp x2). The game streams them in that order into a 3-slot ring (`LoadSceneBG` 0x01DAF7E0, first three preloaded by `LoadScene`); **the `key` argument is NOT an index** (it only repeats/skips: 0,0,2,3,4,6,...,126). Verified: for 122 of the 125 cuts `[start,end]` of the script equals or lies inside the key-time range of that file's camera track (`opening_dump.py check`); the other 3 differ by 1-2 frames (`start` before the first key: the camera simply stays).
* Camera evaluation each step (`MotionProc` types 0x1E/0x1F/0x21): `pos = lerp(track30, t)`, `target = lerp(track31, t)`, `fov = lerp(track33, t)`; projection distance `d = 240 / tan(fov/2)` (`MGSetProjection((1/tan(fov*pi/360))*480*0.5)`) i.e. fov is the vertical FOV for a 640x448 frame with half height 240; **default projection 800** (`MGSetRenderInfo(800, near 6, far 65535)` at init; `PROJECTION n` in the cfg overrides: toan.cfg and seirei.cfg use 500). No roll is used. World units: same as the models (cfg positions are x10, see 6).
* **Near plane by cut key** [V `OpA_MotionProcess` 0x01DB6BF0]: key 48,42,41,40 -> 18; key 39 and 0 -> 16; key 37, 15, 3, 2 -> 10; otherwise 6.
* **Camera shake** [V]: while key in {14,16,17} (and 15 with amplitude 3, key 18 only while the clip time < 56, key 44 once the smoke flag is set -> amplitude 5): each step the camera *target* (ref point, `GetRef/SetRef`) gets `+ (rand() % N) / 10` added on each axis (never negative): N = 10 for keys 14, 16, 17; N = 3 for key 15; N = 15 for key 18 while the clip time < 56; N = 5 for key 44 once the smoke flag is set.
* **Actor placement** (`OBJ_MOVE`, scene A) [V]: for object i in 0..7 with `disp && move-enable`: node = SearchFrame(current clip, `NODE_A[i]`), `NODE_A = {c07a, c08a, c11a, c09a, c08a, c08c, p19a, p17a}`; `M = node world matrix`; position = `M` row 3; yaw = `atan2(M[2][0], M[2][2])` (Z-axis), `SetRotation(0, yaw, 0)` (leader: yaw + constant; object 5: also an x-tilt from row 2.y). The 35 dancers (`MoveDancers`) use the `p17a<k>` nodes of the clip the same way (so dancers circle as the clip says); when the current clip has no such node the object keeps its last pose. Scene B (objects 8..11, every step if displayed): nodes `{c01d, p09a, p10a, c01d}`. Scene C (objects 0..22, displayed ones, every step): table at 0x01DDA4B0: `1:c08a 4:c08a 5:point 9:p09a 11:c01d 12:p03a 13:p08a 14:p05b 15:p01a 16:p12a 17:p07a 18:p02a 19:p15a 20:p16a`; the Flag (object 3, `04c09a`) is attached to node `f0` (its skeleton root takes the node matrix). Scene D: nodes `c01d` (Toan), `c03c` (Spirit King), `tyou`/`tyou2` (butterflies), `hikari`, `atrp` etc. [G for D details]
  Because the translation tracks are inside the clip of the current cut, **an object's path during a cut is exactly the node track of that cut's `.sne`**; nothing else moves them (except walking/animation inside the object's own motion).
* A `.sne` also contains per-cut debris like unused nodes (`obj96`, `cam_1`...). Ignore nodes that no object table names.

## 5. Stage objects 0..22 (OBJ_DISP indices) and their data

Model/skeleton files are `*.chr` packs (`cfg, [mds], bbp, mot, wgt`; see `notes/native-plan.md`), motion numbers = line numbers of the `KEY start,end,speed` list in the cfg (`// n comment` gives the meaning; run `tools/dc/opening_dump.py objects` or read `notes/opening_objects.txt`). Textures come from the `.pim`/`.pak` of the scene (`.img`, `an` = face-animation atlas).

| obj | name | scene A (cursed dance) | B / B' | C (festival / destruction) | D (Spirit King) |
|---|---|---|---|---|---|
| 0 | Seeda | `dungeon.pak`: `01c07a.{cfg,mds,bbp,mot,wgt}`, tex `c07a01(.an)` | | | |
| 1 | Majin | `01c08a.*`, tex `c08a01` | | `majin.pak`: `04c08a.*`, tex `c08a01` | |
| 2 | Priest (Shisai) | `01c11a.*`, tex `c11a01(an)` | | | |
| 3 | Flag (the boss) | `01c09a.*`, tex `c09a01(an)` | | `majin.pak`: `04c09a.*` | |
| 4 | Majin bust-up | `chara/01c08b.chr` (via LOAD_OBJ) | | `majinb.pak`: `04c08b.*` | |
| 5 | Majin hand / butterfly 2 | `chara/01c08c.chr` (via LOAD_OBJ) | | | `seirei2.pak`: `buterfly.*` (D: butterfly 2) |
| 6 | dance leader (呪いダンスリーダー) | `dungeon.pak/01p19a.mds` + motion packs `chara/01p19a{1a,1b,2a,2a,2b,3,3b,4a,4b,5}.chr` (table `noroi` at 0x01DD6140) | | | |
| 7 | 35 dancers | `dungeon.pak/01p17a.mds` + motion packs `chara/01p17a{...}.chr` (table `dancer` 0x01DD61E0); positions table `DancerPos` (35 x (x,y,z) x10) used before the first clip | | | |
| 8 | Toan B | | `chara/03c01d.chr` (+`toan/03komono.chr` props) | | `seirei.pak`: `05c01d.*` |
| 9 | Paige | | `chara/03p09a.chr` | `norn2b.pak`: (p09a skin, `04p09a`) | |
| 10 | Rene | | `chara/03p10a.chr` | | |
| 11 | Toan | | `chara/03c01d2.chr` | `norn2b.pak`: `04c01d.*` | `seirei.pak`: `05c01e.*` |
| 12 | Gina | | | `04p03a` | |
| 13 | Karl | | | `04p08a` | |
| 14 | Ornet | | | `ndance.pak`: `04p05b` | |
| 15 | Macho | | | `ndance.pak`: `04p01a` | |
| 16 | Claude | | | `ndance.pak`: `04p12a` | |
| 17 | Little Macho | | | `norn2b.pak`: `04p07a` | |
| 18 | Odd | | | `norn2b.pak`: `04p02a` | |
| 19 | village dancer m | | | `ndance.pak`: `04p15a` | |
| 20 | village dancer f | | | `ndance.pak`: `04p16a` | |
| 21 | Spirit King | | | | `seirei.pak`: `05c03c.*` |
| 22 | butterfly | | | | `seirei2.pak`: `buterfly.*` |

* Objects are `CCharacter`s (0x11B0 bytes each, array base 0x01DEFEE0: `Chara[i] = base + i*0x11B0`, so the object index is also the slot index). Character data buffers: `CDataAlloc2` at 0x01DEFE00.. (`0x1defe00` chara A, `1defe40` hi-res, `1defe60` motion, `1defeb0` map models...).
* Motion semantics [V `OpA_MotionProcess` + `CCharacter::Step` 0x00138530 + `SetMotionEX`]: per object per step: if `motion2 != -1` and the motion time has passed `KEY[motion].end - 1`: `motion = motion2, motion2 = -1`. The requested motion number goes to `Chara+0xC68`; `CCharacter::Step` restarts the time at `KEY.start` when the number changed (motions 1 and 2 instead keep the walk phase `NextMotionTime`), then cross-fades old pose -> new start pose with fraction += `OBJ_STEP` per step, after which playback continues at `time += KEY.speed` per step, wrapping `time -= (end-start)` when `floor(time) >= end`. Footstep sounds attached to motion frames are triggered by the generic `CCharacter` footstep tables (`SndPlayFootSound`).
* Dance (objects 6, 7) [V `LoadMotionData` 0x01DB90E0]: while object 6 is displayed, the leader and dancers play **10 dance phases back to back**, each from a different motion pack: `noroi[i] = {start, end, speed 0.5}` = (100,750) (201,533) (103,798) (136,777) (1,352) (50,628) (195,843) (103,675) (139,542) (298,747) for the leader and `dancer[i]` = (12,536) (209,541) (111,806) (126,767) (1,352) (1,628) (145,793) (92,670) (139,542) (327,782) for the dancers; when the playing time passes `end-1` the next pack is loaded in the background and the next phase starts. This is independent of the script (script only cuts the camera). The music starts when the dance timer passes 5.0.
* Faces [V/G]: each character has a face texture and an animation atlas (table `face` 0x01DD67F0: {base, atlas, ...}: `c07a01/c07a01an`, `c08a01/c08a01an`, `c11a01/c11a01an`, `c09a01/c09a01an`, ..., `p19a_03/p19a_03an`, `p17a01/p17a01an`; scene B `p09a01/p09a01an`, `c01d01`/`03c01d`...). `FaceChange(obj)` copies the selected eye row and mouth row of the atlas into the base texture each step (`MoveImage`): eye cell height 40 px (Seeda/Majin/Flag/Majin: 40, mouth 35), destination y bases 256/320/448. Eye/mouth index 0 = default.

## 6. Part A in detail (`opdat/opinfo.cfg` + stage models)

Directives are parsed by `OPAnalyz` (0x01DB1400) with the same syntax as the dungeon/town cfgs (`NAME arg, arg, ...`; string args quoted; argument table at 0x01DD5C40..0x01DD60F0). **All positions are multiplied by 10 and all rotations are degrees** when turned into world data; `OBJ_ROT` deltas are degrees per step.

| directive | meaning in `opinfo.cfg` |
|---|---|
| `DebugFlag ON`, `RUN_SPEED 1` | ignored for the opening |
| `LEVEL_FAR a,b,c,d` | LOD switch distances (10,300,500,2000) |
| `AMBIENT r,g,b` = 22,22,12 | `MGSetAmbient` |
| `LIGHT_C dx,dy,dz, r,g,b, n` | directional light n (1..3): direction normalised (towards the light), colour r,g,b (0..255). #1 (-0.562,-0.4,-1.278) (147,113,153), #2 (-1.214,-0.987,1.112) (22,22,51), #3 (-0.214,0.5,1) (50,30,70) |
| `FOG near,far, r,g,b, a?, b?` = 280,2000, 15,5,25, 0,255 | `MGSetFogParm(near, far, r,g,b, x, y)`; first 5 as in the dungeon cfg; the last two go to `uGpffff8a30..` (alpha/intensity) [G] |
| `BG_COL r,g,b` = 0,0,0 | clear colour |
| `SKY "dungeon/e01s01_0.mds", 0` | sky dome model (`opdat/` prefix is added); second arg 1 = give it the "no-lighting" attribute |
| `CRD "dungeon/i01h12_a.mds"` / `ADD_CRD file,x,y,z,rx,ry,rz` | collision model(s); unused visually |
| `GND file,x,y,z,rx,ry,rz` | ground model (`i01h12.mds` the ritual chamber, `i01h12_w.mds` the water/blood pool); added to `OP_GroundMap` |
| `BLD file,x,y,z,rx,ry,rz,lod` | building/object model (`i01h12e1.mds`) -> `OP_BuildingMap`; last arg 0 = no LOD set |
| `OBJ_ROT "frame", ox,oy,oz, dx,dy,dz` | `OBJ_ANIME_SEQ` kind 0 (rotation), constant velocity: frame `ekitai1/2/3` (blood liquid, -0.05/0.1/-0.15 deg/step about Y) of `i01h12.mds`; `i01h12`, `i01h12m`, `i01h12h` of `i01h12e1.mds` (+0.5, -1, +1 deg/step: the magic circle rings) |
| `FIRE x,y,z, scale, flag` | fire sprite emitter (x,y,z are x10); 38 torches around the room. `flag` 1 = flickering with light, 0 = lamp (norn cfgs) |

Textures of part A: `dungeon/dungeon.pim` (a pack of 25 `.img`: `c07a01 c07a01an c08a01 c09a01 c09a01an c11a01 c11a01an fuki256 gaiji p17a01 p19a01 p19a01an syst04 cloud cloudan cloud2 e01s01 ex fire i01t01 i01t02 ashikage pause pause_e start2`). Models in `dungeon/dungeon.pak`: `01c07a 01c08a 01c09a 01c11a` (cfg, bbp, mds, mot, wgt each), `01p17a.mds`, `01p19a.mds`, `01cloud.mds` (smoke cloud), `ashikage.mds` (foot-print marker). Sound packs in `dungeon.pak`: `o01a.txt` (load list: `o01a_a.hd o01a_c.hd o01a_i.hd sysa_m.hd o01a_a.sq`), banks `o01a_a/c/i.hd/.bd`, `sysa_m`, sequence `o01a_a.sq`; second music `dungeon/o1bbgm.snd` (pack with `o01b.txt` + `o01b_a.hd/.bd/.sq`, background-loaded when the camera key is 44 and its clip time > 250 and swapped in at key 14).

## 7. Subtitles / speech balloons

* Language -> bank (`LoadMessage` 0x01DAF4C0): language 0 `opdat/fconv.bin` (Japan), 1/2/6 `opdat/usa/fconv.bin` (**English; byte-identical to `opdat/optext_1.mes`**), 3 `optext_3.mes` (French), 4 `optext_4.mes` (German), 5 `optext_5.mes` (Italian). Format = the standard `.mes` bank of `notes/events.md` section 4.1 (`tools/dc/mesdump.py opdat/optext_1.mes`; `\p` = page break, `<name0>` = player name). The English bank has **30 messages (ids 1-3, 5-15, 17-32)**. The script also uses ids **35-41 and 100-120** (cut G-K of the dance, Majin-bust/Flag 'Kukuku', the Spirit King's speeches 101-120): those ids do **not exist** in any shipped bank - in the retail game those lines print nothing (this port should treat a missing id as "no balloon"). Messages 1..32 are exactly the priest/Flag/Majin/Paige/Rene dialogue; see `notes/opening_timeline.txt` for each in context.
* Window object: one `ClsMes` (0x01E0CDF0, `Preset(2)` = balloon style 2 [V], texture `fuki256.img` [G], glyph cell 11x20, normal print speed 1 glyph/step (`+0xA4=+0xA8=1.0`), 640x448 screen). `DrawMess` (0x01DB0AE0) each step:
  `balloon position (x,y)` from the command (top-left origin of the window, screen px), tail anchored to speaker point `(tx,ty)` (tail width 0x40, or none when `tx == 0`; the tail base is clamped to the window: if the speaker is above the window centre the tail attaches at the top edge (+0x14) else at the bottom (+height-0x14)); balloon is drawn "absolute" (`AbsFukidashiIn`, fixed screen coordinates, not a world-space balloon).
  Life: `frames` is decremented by `sp` each step; at <= sp the balloon closes. When a `WAIT_KEY` is pending the life is held at `2*sp` and the message waits for the player.
  A new message id (changed `CScript+0x4EC`) re-creates the window (`MakeMesWin`).
* `WAIT_KEY 0` + `WaitKeyProcess` [V]: while the flag is set the script and camera are frozen; on pad mask 0x20 (circle) or 0x40 (cross): if the message is waiting at a page break (`State()==5`) -> next page; elif the whole message is shown (`State()==3`) -> clear the flag (script continues next steps); else (still revealing) -> draw speed 0 = show the whole page at once. The page-arrow indicator blinks 16 on/16 off steps. (`MES_CLR` is explicit in the script.)
* `NEXT_MES n` sets the auto page timer limit (steps) for multi-page messages 3, 21, 23 (n = 80, 200, 160; 300/350 are used before ids 37/38 which do not exist).
* Typical balloon coordinates (examples): `OBJ_TALK 3,1,40, 300,308, 430,157` (Flag, msg 1, 40 camera frames, window at x=300 y=308, tail to 430,157). Window sizes are decided by `ClsMes` from the text (min 202x138 per `events.md` 4.1).

## 8. Sound

* **BGM/SE engine**: the normal `CSound` (see `notes/sound.md`): `SndInitialize(4, 30, 4, 5)` at `OpeningInit`; each part loads its own pack via `LoadSoundFileFromPack("o0Xa.txt")` from the part's `.pak` and plays `SQ_Play`. A `SoundStop` precedes each `SCENE` (1,3,5,8,9). Reverb set per part (`SetReverb(0,4,0x50)/(1,4,0x3c)` in A, `(0,4,0x1e)/(1,4,5)` in B, `(0,4,0x1e)/(1,2,5)` in D).
* Sound packs per part: A `o01a` (+ `o01b` from `o1bbgm.snd`), B `o02a` (`norn.pak`: `o02a_a/e01a_c/e01a_e/o02a_i/sysa_m`, sequences `o02a_a.sq`, `e01d_e.sq`), C festival `o03a` (`norn2b.pak`), C Majin `o03b` (`majin.pak`), D `o04a` (`seirei.pak`), `o04b` (`seirei2.pak`), `o04c` (loaded at key 123 via `seirei2.pak`).
* **`SE ch, prog, key, pan`** [V `SoundProcess` 0x01DB0450]: `ch` 0 -> sound port 0xE, `ch` 1 -> port 0xC (the Q = "special SE" bank); the call is `SE_Play(port, prog, key, pan 0x40, ... volume = se_info volume of (prog,key) x 1.0)`; `prog`/`key` are the Dark Cloud SE (program, key) numbers resolved through `GetSeNo`/`se_info` (`sound.md` section 7: prog 40/38/21 are the "e"/event banks of the loaded pack; `SE 1,124,k` = special SE number k-19: `124,60` = `sp41`, `124,62` = `sp43`, matching `sp41_q.hd` in seirei.pak and `sp43_q.hd` in seirei2.pak). The 4th argument is parsed but ignored. Used (prog,key): (40,21..40) in part A/B, (38,38) door, (38,39), (40,43/44) Atla equip, (21,20).
* `SE_STOP ch, prog, key` -> `SE_Stop(port, prog, key)`.
* `SOUND_FADE -1, target, rate` [V `SoundProcess`]: `CSound::Fade(rate, port, target)` for ports 0..2 and 0xC..0xF (rate halved for port 0 = BGM). `Fade` stores {target, rate, current volume read from the module} per port and moves linearly each step [G: volume units/step, 256 = full; used values -4, -2, -1 = about 1 s, 2 s, 4 s from full]. `FadeCansel()` resets them.
* **Automatic sounds in the Process functions** (not in the script) [V]: footsteps - `OpPlayVolPanSE(100.0, 500.0, pos, port 0xE, prog 0x15 (21), key 0x1C/0x1D (28/29 = left/right foot))` fired (with a 10-step debounce, volume and pan from the distance/direction between the character and the camera, `SndGetVolPan`) when the **Flag's motion** is 1, 4 or 10 and its motion time passes frame 39/49, 99/109 or 229/239 (resp.), and when the **Priest's motion** 14 passes frames 296/299/301 (`0x128/0x12b/0x12d`) (scene A); scenes B/C use keys `0x14/0x15` and `0x20/0x21` of prog 21 on the same principle (Toan/Paige/Rene steps); ambience on port 0xF prog 0x10 key 0x14 (a fire loop positioned at the central brazier (world 0,?,?), volume tracking the camera distance) and key 0x1C (rumble) while the smoke flag is up; explosions/Majin beams in C play `SndInfo` entries. Music triggers by cut key: A starts BGM when the dance timer > 5.0, loads the 2nd BGM at key 44 / starts it at key 14; C starts BGM at keys >= 58 (`0x3A`) and `SQ_Play(1)` at key 70; D: key 101 (`0x65`) message-gated `OpBgmPlay`, fade at 110, again at 115, fade at 118, `o04c` at 123.
* Music/SE are therefore the standard game banks; a port that already plays `bgm`/`snd` packs only needs the load-list names above and the (prog,key) numbers.

## 9. Effects

* `SPRITE n` [V where read; draw details G]: scene-dependent effect id:
  * A: **1** = full-screen additive flash sprite (texture slot 7; alpha `128 - v/4` with `v` starting at 1024 and decaying) - used for the "!" belly-growl cut (`SPRITE ON ... OFF`); **2** = jar smoke: starts the `01cloud.mds` smoke model (grows from scale 0 to 1, `cloud`/`cloudan` scrolling textures, camera shake at key 44); 3 has no draw code in A (used at the Majin landing together with `SE 40,21`; probably the shockwave/dust - [G]).
  * B': 1 = door light, 2 = steam from the soup (`soup_nakami`), 3 = steam/hat gag (cuts 52-56).
  * C: 1 = light flash (with `BEEM -1,...` = "光エフェクト" light effect) and explosion flashes with `BOM`.
  * D: 1 = bright flash (texture slot 10), 2/3/4 = the three **2D screens** of the destroyed village (images `norn4/i00002.img`, `i00006.img`, `i00022.img`, shown one after another at 9:48-9:52), 5/6 = the **2D explanation screens** (`norn4/0519.img`, `0519p.img`: the Atla/"Atlamillia" instruction picture drawn in pieces by `Setsumei`, 0x01DC6E70).
* `BOM x,y,z,w` / `BEEM`: only the village-destruction cuts (7:12-7:21). BEEM from (600,400,-100) to (200,10,100) etc. = Majin's laser blasts hitting houses; BOM at the impact point with radius w; textures `beem.img`, `bakuhatu.img` (explosion), `sunakemuri.img` (dust), `smoke.img` ("gray smoke"). [G on rendering]
* Fire/torch flames: `FIRE` entries; water: `WATER_SET/RGB/PARAM` (not used by the four cfgs); `OBJ_ROT` ring animations; scrolling textures (`setTexScroll`: blood `e01b20/e01b21`, `e01b22/23`; cloud scroll).
* Fades: section 3. Shadows: simple projected shadow models (`*_s.mds`, `shadow` frame, `shadow_buff` render target).

## 10. Stage tables for parts B, C, D (static data in TITLE.BIN)

Each entry (0x20 bytes) {mds path, shadow mds path (or 0), x, y, z, rx, ry, rz}: position x10, rotation degrees; a null path reuses the previous model (instance). Tables: B1 0x01DD6B40 x68 and B2 0x01DD73C0 x23 (Norune night), C1 0x01DD7F30 x75, C2 0x01DD8890 x87 (festival), C3 0x01DD9370 x76, C4 0x01DD9CF0 x62 (destroyed), D1 0x01DDAF00 x10. `opening_stage.py` prints them; `notes/opening_stage_tables.txt` is the dump. Entry 0 of B1/B2 etc. is the ground. Model files are the loose `opdat/norn*/*.mds` (these are the Norune Georama models: `e01h*`, `e01c*` (fences/roads), `e01t01_*`, `e01r*`, `t0001..t0006`, `t02xx`, `t03xx`; `*_s.mds` = shadow volumes), textures `e01b01..03 e01t01 e01s03 e01s06 t0001..t0003 t0203 t0205 ...` from `norn.pak` / `norn2.pim`. `norn.cfg` / `norn2.cfg` / `toan.cfg` / `seirei.cfg` hold ambient/lights/fog/torch `FIRE`s the same way as `opinfo.cfg` (torch lists, `// 松明`, `あかり` lamps). Interiors: `toan.cfg` has `PROJECTION 500`, `AnimeSpeed 10,10`, `FIRE 0,1.1,0, 0.1, 0` (the hearth), `FOG 10000,11000,...` (effectively no fog).

## 11. Annotated timeline (condensed; every command with time is in `notes/opening_timeline.txt`)

Times are mm:ss at 60 steps/s with `WAIT_KEY` = 2 steps; subtract nothing for key waits (add the player's wait). "cut #N" = Nth `CAMERA`/`.sne`.

| time | scene | cut # (file) | content |
|---|---|---|---|
| 0:00 | A | 0-1 (0101a/b) | fade in 2.0 (64 steps) on the sitting dancers (leader + 35), slow pan 6.5 + 9.8 s |
| 0:16 | A | 2-4 (0104c,0104b,0105) | leader stands to the beat, close-up (eyes/mouth 4/2), dancers rise |
| 0:29 | A | 5-8 | **Flag and Priest appear** (obj 2,3 ON), walk (motion 1), dialogue msgs 1,2,3(\p, NEXT_MES 80),5,6,7,8,9,10 as they inspect the dancers ("blood of witches...") |
| 0:55 | A | 9 (0111) | Seeda (obj 0) appears, turns (motion2 1,2) and later runs away |
| 1:02 | A | 10-12 | Flag/Priest talk: msgs 11,12 ("when will it happen?" - "very soon") |
| 1:07-3:02 | A | 12-23 (0114a-k,f1,f2) | **dance part**: dancer objects only (obj 2,3 OFF except cuts G-K), 11 camera angles each 1-2 s of camera time; msgs 36-39 (not in the bank); fire/rings animate; `SPRITE 2` smoke starts at 3:00 |
| 3:03 | A | 24-26 | jar smoke (shake keys 14-17): msgs 13,14 ("So now is the time..." "Yes sir"), Majin forms in the smoke (obj 1 ON, motion 0 "fly"), lands (`SE 40,21`, `SPRITE 3`) |
| 3:20 | A | 27-47 (0119-0136) | Majin vs Flag/Priest/Seeda: msgs 15 ("This is the Genie?"), 17,18, Seeda 18/19, Majin yawns (`SE 40,23`), belly growl (`SE 40,28`, `SPRITE ON/OFF`), "Wait a minute" (23), eats the priest (`SE 40,24..27`, msgs 24-27 "Gulp... Mmmm, delicious"); `SCENE_LOAD 1` streams Norune at 4:28 |
| 4:50 | - | 48-49 | fade out 1.0 + `SOUND_FADE -1,0,-4`, then `SCENE 1` |
| 4:52 | B | 50 (0301) | Norune at night, Paige (obj 9) walks to Toan's house (one 500-frame camera pass); `SCENE_LOAD 2` |
| 5:12 | B' | 51-57 (0302-0308) | Toan's house: Rene (10) and Toan (8), knocking (`SE 40,36` x3), door (`SE 38,38`), Paige enters, msgs 28-32 (WAIT_KEY on 28/29/...), soup, hat; fade out 1.0, `SOUND_FADE -2` |
| 5:34 | C | 58-62 (0401-0405) | Norune festival at night (scene 3/4): Toan (11) runs, Gina/Karl (12,13), village dancers (19,20), `SCENE_LOAD 4/5` |
| 5:50 | C | 63-66 (0406-0409) | festival dance, 4 camera passes of 10 s: Ornet (14), Macho+Claude (15,16), Little Macho+Odd (17,18) |
| 6:22 | C | 67-69 (0410-0412) | Toan reacts (eyes/mouth 5), blinks scripted by OBJ_EYE; `SOUND_FADE -1,0,-1` (4 s music fade) |
| 6:32 | C | 70-81 (0413-0427) | **Majin and Flag over the village** (scene 5): moon shadow still picture, bust-ups (`LOAD_OBJ 6,4`), msgs 40,41; light effects `SPRITE 1 + BEEM -1` at 6:50-7:10 |
| 7:12 | C | 82-98 (0428-0443) | **destruction of Norune** (scenes 6,7): `BEEM`/`BOM` explosions, flash fades (`FADE_OUT 1,4`, `FADE_IN 1,128`), fire, Paige bust-up (cut 91), Toan; last village shot (0443, key 100) holds 112 s: Majin speaks (msg 100 not in bank), music fades at 8:47, fade out at 8:49 |
| 9:30 | D | 99-111 (0501-0515b) | **Spirit King** (scene 8, `seirei.cfg`): Toan B (8) listens, Spirit King (21), msgs 101-116 (not in the bank), Atla equipment (`SE 40,43/44`), 2D screens of the ruined village (SPRITE 2,3,4 at 9:48-9:52) |
| 10:03 | D | 112-124 (0516-0529) | scene 9: `SE 1,124,60` (sp41), 2D explanation screens (SPRITE 5,6 at 10:09), flowers and butterflies (obj 22, 5), Toan with the Atla, `SE 1,124,62` (sp43), `SE 0,21,20`, fade out 1.0 at 10:33.8, `END` at 10:36 |

## 12. What is solid / guessed / blocking

**Solid** [V/D]: the command set and the exact meaning/units of the arguments as they are stored; the `Step`/`WAIT` timing law; the one-to-one `CAMERA` N-th <-> `.sne` N-th mapping and the `.sne`/`.mot` track layout (camera pos/target/fov, node translations, facing); the stage-object -> model/pack tables; the cfg directive scaling; the stage placement tables; the message bank selection and the missing-id behaviour; SE port/program/key addressing; fade formulas; object/Chara slot mapping; all file names loaded per scene.

**Guessed** [G]: 60 Hz step rate; exact atlas rows of `OBJ_EYE/OBJ_MOUTH` indices (use the script comments); the visual form of `SPRITE`/`BOM`/`BEEM` (flash sprite, smoke model, explosion + beam sprites, 2D screens); the unit of `SOUND_FADE rate`; the facing constant for the dance leader; part D node usage; exact `FOG` last two arguments; the near-plane/shake tables are exact but their purpose (avoid clipping, rumble) is inferred.

**Blocks a *faithful* port**: (1) the balloon/`ClsMes` pixel layout and glyph rendering (see `events.md` 4.x; position/tail data above are exact, drawing is not ported); (2) the per-scene special rendering code of the four Draw processes (water/blood shaders, cloud scroll, sun lens flare `LensFreaProcess`, `EffectAtraPrizum`/`RollLight`/`EffectSeireiKing`, `Setsumei`, wind/cloth `ClothStep`, fire sprites, shadows `ShogunShadow`/`ReaderShadow`...) is decompiled in `work/title_BCD.c`/`work/title_A.c` but not documented; (3) cfg `FOG`/`LIGHT_C` conventions are those of `dccore/floor`; (4) the part-A hard-coded footstep/ambient sound triggers (section 8) and part-C/D music keys need a per-cut audio table; (5) the Japanese-only/unshipped message ids (35-41, 100-120) have no text in the US build.
