# Georama (town) map data: formats and loading

Scope: everything under `data/gedit/<map>/` (the five Georama towns `e01`..`e05`, e01 = Norune village per its mapinfo.cfg header, and the 60+ story scenes `sNN`), as read by the main executable (`EditInit` 0x178380, `EditLoop` 0x1797e0, `LoadGroundData` 0x1815e0, `LoadEditMapData` 0x174390, class `CEditGround` 0x1a0470.., `CEditArea` 0x16d860.., `CMapParts` 0x19a7b0..).
All function names/addresses below are from `work/decomp/all_dun.c` / `notes/symbols.csv`.
Everything marked **(verified)** was checked by parsing the real files; **(code)** = read from the decompile only; **(guess)** = plausible but not proven.

Tools written for this (all in `tools/dc/`):

| tool | what |
|---|---|
| `pts.py` | `open_scn(path)` -> `Scn` (entries, `.pts` list of `Pts` with `.models`, `.info`, `.funcs`), `check_pts`, `frame_key`; `python -I tools/dc/pts.py [--all]` checks every file, `--dump file.scn [part]` prints a part |
| `mapinfo.py` | exact re-implementation of the `mapinfo.cfg` tokenizer (all quirks), a semantic layer (`load(dir)` -> `MapInfo`), the 61 directive table and the 15 directive table of the interior configs; `--check` parses all files |
| `georama_check.py` | cross-checks: name resolution of mapinfo/PTS references, legality of the `gdata0.edt` layouts, `fconv.bin` vs `*talk_1.mes` |

Run `python -I tools/dc/pts.py --all`, `python -I tools/dc/mapinfo.py --check`, `python -I tools/dc/georama_check.py` to reproduce every number quoted here.

## 0. Map directory

```
gedit/e01/                  (e01..e05 = Georama towns, sNN = story scenes, interior/ = shared interior config)
  mapinfo.cfg               text script, see section 4         (every map; the game looks for mapinfo.cfb first, a compiled form that is not on the disc)
  scene.scn                 'SCN' container of 'PTS' blocks (section 2/3)   (all maps but s36; s17 has a second one in s17/1/)
  mds.pak                   pack: sky/sun models, the edit-grid model e01g01.mds, small helper models
  img.pak                   pack: IMG/IM2 texture containers named in mapinfo + texanime.cfg (texture animation script)
  mds/                      a few loose .mds (e01a03_0.mds..)
  chara/                    NPC (pNNa.chr), main character models etc.
  in/<house>/               interior of every house: <house>{m,e,n}.cfg/.scn/.img/.pak (day/evening/night), event.stb, *.mes
  event.stb, <map>_<lang>.mes   event script + its messages (language 1 en, 3 fr, 4 de, 5 it, 6 es)
  <map>talk_<lang>.mes, fconv.bin   NPC chat texts (fconv.bin = fallback bank, section 5)
  gdata0.edt, gdata0.edt_   developer's complete Georama layout (e01..e05 only, section 5)
  mapeditor.ini             authoring-tool leftover ("DarkCloud MapAuthoringTool", project path Y:\Project\submap\..\*.epp)
```
`MapNo` -> directory name (EditInit): `0..8` -> `e01..e09` (`e0%d` with MapNo+1), `11..19` -> `s01..s09` (`s0%d` with MapNo-10), `>=20` -> `s%d` with MapNo-10; `99` = interior test (`gedit/interior/`). Only MapNo `< 5` are Georama towns (building/edit menus); `>= 5` are plain walk maps.
`EditDataDir = "gedit/" + name + "/"`.

## 1. Conventions

* little endian, floats are IEEE singles, lengths in game units (a dungeon cell is 160 units; an edit-grid cell of Norune is 100).
* Names are Shift-JIS (cp932) strings in fixed 16 byte fields, NUL terminated, **with junk (uninitialised tool memory) after the NUL**. Never compare the whole field.
* Frame (model object) names are compared by `StrCmp__FPcPc` (0x1285f0) **only up to the first `__`** on both sides, case sensitive. So the record target `h01__N` finds the object `h01`, `h01__S`, ... (`pts.frame_key`).
* Clock: the game time `NowTime` runs `0..12` (one unit = 2 clock hours, `0` = 10:00). `ConvertTime(h) = (h-10 mod 24)/2` (0x1740f0) turns the clock hours stored in data files into game time, `InvertTime(t) = 2t+10` (0x174150) back. `EdGetTime(t)` (0x187e60) maps `t` to a day phase 0 day [11.5..2.5), 1 dusk [2.5..5.5), 2 night [5.5..8.5), 3 morning [8.5..11.5); `EdCheckTime` handles wrapped windows.

## 2. `scene.scn` ('SCN\0') **(verified: 68 scene.scn + 136 interior .scn files)**

| offset | size | field |
|---|---|---|
| 0x00 | 4 | `'SCN\0'` |
| 0x04 | 4 | entry count (<= 64) |
| 0x08 | 8 | zero |
| 0x10 + 0x30*i | 0x30 | entry: `name[16]` (junk after the NUL), `u32 offset` (from file start), `u32 size`, 0x18 bytes junk |
| 0xC10 | | first block; the table always has room for 64 entries (0x10 + 64*0x30) |

Blocks are back to back, no padding, the last ends at the end of the file (verified for all 204 files). `SearchPTS(scn, name)` (0x13e4e0) strips a leading path from the wanted name, then `strcmp`s entries in order (first match wins; `s04` and `s43` contain duplicate names). Entry names are the names used by mapinfo.cfg (`GROUND "e01g02"`, `BLD_PARTS 0,"e01h01.pts"` -> `e01h01`). Every block is a `PTS`: 384 parts in the 67 `gedit/<map>/scene.scn`, 2 more in `s17/1/scene.scn`, exactly 1 in each of the 136 interior `.scn` files.

## 3. `PTS\0` block ("map part")

A part = one visual object of the map: a ground tile, a static building, a placeable Georama part (house, tree, road piece, river piece, bridge, lake) or an interior. It is read by `LoadPTS(CMapParts*, uint* pts, MAP_PARTS_INFO*, OBJ_ANIME_SEQ*, EDIT_EFFECT_INFO*, EDIT_OBJECT_TIMER*, ED_EVENT_POINT*, CMapParts* copy)` (0x1828b0), `LoadMapObject(CMapParts*, uint**, alloc)` (0x19b790), interiors by `LoadPTS(CMapParts*, uint*)` (0x19f6f0, same layout).

### 3.1 header **(verified on 522 blocks)**

| offset | size | field |
|---|---|---|
| 0x00 | 4 | `'PTS\0'` |
| 0x04 | 4 | offset of the info block (always 0xE0) |
| 0x08 | 4*16 | `name[16]` of model slots 0..3 |
| 0x48 | 4*4 | `u32 offset` of slots 0..3 (relative to the PTS block) |
| 0x58 | 4*4 | `u32 size` of slots 0..3 (0 = slot empty) |
| 0x68 | 0x18 | slot 5: `name[16], u32 offset, u32 size` |
| 0x80 | 0x18 | slot 4 |
| 0x98 | 0x18 | slot 6 |
| 0xB0 | 0x18 | slot 8 |
| 0xC8 | 0x18 | slot 7 |
| 0xE0 | | info block (3.2), which ends with the function records; **the first model starts exactly at `0xE0 + info.func_off + 0xC0*info.func_count`** |

The nine slots are handed to `LoadMapObject` as pointer array `[0..8]`; models follow the info block back to back in the file order slot 0,1,2,3,5,4,6,8,7, so the block size is the sum of everything (verified: no gaps, no tail). All present models are plain `MDS\0` containers (`tools/dc/mds.py`; the collision/camera ones use the special group layout of Milestone 3 phase B, `dccore/collision.*`).

| slot | role | usual name | what the code does with it |
|---|---|---|---|
| 0..2 | LOD 0,1,2 | `_0 _1 _2` (or just `.mds`) | `LoadMDSFileLOD`; `CObjectFrame::DrawLOD` (0x157220) picks the highest level whose squared threshold (array passed by the caller) is <= the squared distance of the part's origin to the camera; empty levels fall back to the next lower one |
| 3 | LOD 3 | `_m` ("miniature") | same, farthest level |
| 4 | shadow | `_s` | `CMapObject+0xd4`, drawn projected on the ground (`DrawShadow` 0x157760, `MGDrawShadow`) |
| 5 | collision | `_a` | `CMapObject+0xd0` movement polygons ("あたり"): the player/NPC `MoveCheck` collides against it; transformed by the part's position/rotation (`GetCollisionFrame` 0x1574f0) |
| 6 | shade | `_k` | `CMapObject+0xd8`, dark overlay ("陰") drawn with `MGDrawShade` |
| 7 | water/ripple | `_r`, `_w`, `_0` | `CMapParts+0x104`, drawn by `CEditGround::DrawRipple` (0x1a3940) for lakes/rivers |
| 8 | camera | `_c` (town), `_v` (dungeon-style scenes) | `CMapObject+0xdc`, camera collision polygons used by `CEditGround::PickUpCameraPoly` (0x1a5210) |

Slot usage on the disc: slot 0 always present (a handful of names lack the `_0`), 1: 65, 2: 53, 3: 27, 4: 24, 5: 188, 6: 57, 7: 28, 8: 95 parts of 384.

### 3.2 info block (`EPARTS_INFO_HEADER`, offsets relative to the block start)

The first 0x78 + strings part is exactly what the game itself builds from a `PARTS_INFO` directive (`EdInitToEPInfo` 0x189570), i.e. the authoring tool serialised the same structure.

| offset | type | field |
|---|---|---|
| 0x00 | u32 | header size, always 0x78 |
| 0x04 | u32 | "size": **not reliable**, it is the end of the function array minus 0..14 bytes (the game `memcpy`s this many bytes, so the last record loses its (unused) tail) |
| 0x08 | u32 | footprint width in edit-grid cells |
| 0x0C | u32 | footprint height |
| 0x10 | u32 | max number of copies the player may place (`PARTS_INFO` 4th argument "配置最大数"); 1 for most houses, 3..28 for trees/roads |
| 0x14 | u32 | 0/1 **(unknown)**: 1 on exactly two parts, the first road piece `e01r01` and first river piece `e01c01` |
| 0x18 | u32 | 0 |
| 0x1C | u32 | 0 / 1 / 2 **(unknown)**: 1 on 36 placeable houses/trees, 2 on 3 river pieces (`e02c10 e02c11 e03c08`), 0 on the other 345 parts |
| 0x20 | u32 | 0 |
| 0x24 | 3 floats | local offset of the models (initial `SetPosition` of the `CMapParts`; non-zero for 4 static objects and equal to the mapinfo position there) |
| 0x30 | 3 floats | local rotation (initial `SetRotation`), 0 everywhere |
| 0x3C | u32 | offset of the grid (always 0x78) |
| 0x40 | 6 x i32 | element/"additional item" ids (0..0x23, -1 = none); `PARTS_INFO` pairs |
| 0x58 | 6 x u32 | offsets of the six element strings |
| 0x70 | u32 | offset of the function records |
| 0x74 | u32 | number of function records |
| 0x78 | w*h bytes | footprint grid, see 3.3 |
| .. | strings | six NUL-NUL terminated strings: space separated object (frame) names that belong to element k. A leading `-` = shown when the element is NOT installed, `*` = special, else shown when installed. `EdPartsObjectOnOff` (0x1850a0) switches them with `CMapParts::FrameObjectOnOff`; the element id must be in 0..0x23 and its installed state comes from the save file (`CEditPartsInfo`, 0x199fd0..) |
| func_off | n*0xC0 | function records, 3.4 |

Element id meaning (which Atla item/requirement 0..35 is behind an id) lives in the static table read by `GetEditAtraPartsData` (0x158e00) and the Atla menus (`AtoraCompOrEvent` 0x218610, `DrawAtoraParts` 0x2182c0) - not decoded here. 213 of the 215 element object names resolve to objects of the part's models (the other is `-door1`, a `-` prefixed name that resolves after stripping).

### 3.3 footprint grid (`GetInfoData` 0x19aae0, `CMapParts::GetWidth/GetHeight` 0x19aa20/80)

`w*h` bytes, row-major (`i + j*w`, i along +X of the edit grid, j along +Z) in the part's own orientation:
* `0` = cell not used; `1..9` = the part raises the ground of that cell by that many altitude steps (`AddAlt`); `0x80` (`x`) = cell is occupied by the part; `0x81` (`d`) = occupied and "dug"/water (the cell gets no ground polygon, `RemakeGrid` collapses its quad).
* The part has a rotation index `r` in {0, 1, 2, -1} (`CMapParts+0xf8`); the model is turned by `SetRotY` = `SetRotation(0, pi*r/2, 0)` (0x19a850). For odd `r` width and height swap: with `W0,H0` the stored size and `(w,h)` the rotated one
  `r=0: g[i + j*W0]`, `r=1: g[i*W0 + (W0-1-j)]` (w=H0,h=W0), `r=2: g[W0*(H0-1-j) + (W0-1-i)]`, `r=-1: g[j + W0*(H0-1-i)]`.
* Cell of the footprint cell (i,j) in the area grid: `(cx + i - (w>>1), cz + j - (h>>1))` where `(cx,cz)` is the cell containing the part's position.
**Verified**: with this model the developer layouts `gdata0.edt` of e01, e03, e04, e05 are legal (inside the areas, no overlaps, counts <= max), see section 5.

### 3.4 function records (`EPARTS_FUNC_DATA`, 0xC0 bytes)

Everything the runtime reads is in +0x00..+0x7F; +0x80..+0xBF is editor scratch (a 3x4 world matrix + position at tool time in some records, stale memory pointers in others) and is never read **(code: all consumers listed below)**.

| offset | type | field |
|---|---|---|
| 0x00 | char[16] | label (editor name: `mapj00`, `drl00`, `smoke00`, `timer`, `light1`, `item02`, `hito12`, Japanese names for sound sources) - not used at run time |
| 0x10 | i32 | type, table below |
| 0x14 | u32 | editor-side index (0..31, varies per record, 0 for most); not read by the town code. In the interior loader (0x19ef00..) it is replaced by a pointer looked up from a table indexed by this value (`DAT_01d4d6a0[idx*0xa8]`, camera zone records use 0) |
| 0x18 | float | start, clock hours (0..24); active window `[start, end)` after `ConvertTime`, wraps over midnight (`21..5`) |
| 0x1C | float | end, 24 = always |
| 0x20 | i32 | id: MAPJUMP id (types 2,10,11,16), villager number (type 1), ladder pair id (19/20), item slot (17) |
| 0x24 | i32 | map-flag id (`CSaveData` map flag, 1..); the record is inactive once the flag is set; copied to `EDIT_EFFECT_INFO+0x14`, `OBJ_ANIME_SEQ+0x88` and `ED_EVENT_POINT+8`; the first time a map is entered `LoadPTS` initialises the flag to `(+0x28 == 0)`. 100..102 for pickable items |
| 0x28 | i32 | 1 = initially active, 0 = initially hidden (5 records). 0x2C = 0 |
| 0x30 | char[16] | target frame (object) name in the part's models; resolved with the `__` rule. If not found the effect falls back to the root frame; the event point gets no frame |
| 0x40 | 3 floats + 1.0 | position (part-local) |
| 0x50 | 3 floats | rotation Euler angles, radians (y used) |
| 0x60 | 3 floats | scale / colour / second point (type specific) |
| 0x70 | 4 floats | parameters (type specific, ints are stored as floats and `fptosi`-converted) |

Types (counts over the 384 town parts; `+` = also in interiors). `CMapParts::...Effect` = `EnterPartsEffect` (0x172ac0) -> `InitEditEffect(frame, func, EDIT_EFFECT_INFO)` (0x1669d0) maps type -> effect kind 3->1, 12->2, 13/5->3, 4->4, 14->5, 15->6, 21->7; `EDIT_EFFECT_INFO` (0x70): +0 frame name, +0x10 kind, +0x14 flag id, +0x18/+0x1C ConvertTime window, +0x20 frame ptr, +0x30/0x40/0x50/0x60 = func +0x40/0x50/0x60/0x70.

| type | name | n | meaning (code) |
|---|---|---|---|
| 1 | person | 75 | villager standing point: `+0x20` = villager number (PEOPLE/PEOPLE2 order), `+0x40` position and `+0x54` yaw relative to the part; `CEditGround::GetPeoplePos` (0x1a2d10) transforms them by the part's pose |
| 2 | mapjump | 56 | house door trigger; becomes `ED_EVENT_POINT` kind 1 (`EdInitEventPoint` 0x183d50) for the MAPJUMP with id `+0x20` of the owner part: position `+0x40`, yaw `+0x50.y`, flag `+0x24`, time window; `(int)+0x70.x` (stored in the event point at +0x24) = event number to run instead of simply entering (`MoveChara` -> `EdInitGotoInterior` 0x16d7f0 returns it) when > 0; also computes the "out" position (a fixed offset turned by the yaw) unless a type 16 exists; trigger extents 10 |
| 16 | mapjcamp | 6 | exit position (copied to the event's out position `+0x80`) |
| 10/11 | door_r / door_l | 7/49 | door leaf record of the same MAPJUMP id: sets the event's door side +1 / -1 (+0x1c) and `(int)+0x70.x` (event +0x20) = door style 0..7, which selects the open/close sound and the door motion (`EdDoorOpenSe` 0x172100, `EdDoorCloseSe`, `EdGetDoorMotion` 0x1721c0) |
| 17 | item | 197 | pick-up point -> event kind 2: `(int)+0x70..+0x7C` = (item id [`ConvertItemNo`], count, minimum Georama progress of the town (the point stays hidden while `progress+1 < value`, `CheckEventPoint` 0x183a60), ?), flag `+0x24` (100..), trigger radius 15 (horizontal distance, height window -1..+20); pressing OK gives the item (`EdGetItem` 0x1730a0, random attach if `GetAddAttachItem`), sets the flag; the polygons of the point are solid (`EdEventPointCpPoly`) |
| 18 | event | 29 | event zone -> event kind 3, only created when `(int)+0x70.x > 0`: that event number is run by `RunEvent` when the player stands inside the zone (extents = the record's scale vector `+0x60`); `(int)+0x74.x > 0` additionally draws the marker sprite |
| 19/20 | ladder_bottom / ladder_top | 12/12 | event kinds 4/5 (extents 6), paired by `+0x20`; `(int)+0x74.x` of the partner is stored; `+0x70.x` = ladder length; button OK starts the climb (`EdInitHashigo` 0x16d720) |
| 3 | fire | 119 | flame/torch effect (kind 1) on frame `+0x30`; size = scale.x |
| 12 | flame | 11 | effect kind 2 (small flame) |
| 13 / 5 | light | 85 / 0 | glow sprite (kind 3), colour/size in scale |
| 4 | smoke | 15 | chimney smoke (kind 4) |
| 14 | point_light | 25 | kind 5: a real point light; `DrawParts` (0x19b260) collects up to 4 per part, direction = (frame world position - part position) normalised, colour = scale xyz (w = 128) |
| 15 | candle | 49+ | kind 6, `CCandleEffect`; scale = size (interiors only) |
| 21 | sound_src | 22 | kind 7: positional sound: `params = (SE number, reference volume, max distance, 0)`, `scale.w == 1.0` makes it a line source from `pos` to `scale.xyz`; `EdSetSoundSrcVol` (0x171d20) mixes up to 4 SEs by distance/pan |
| 9 | timer | 169 | `EDIT_OBJECT_TIMER` (0x20: name, ConvertTime window, owner): frame `+0x30` is only shown inside `[+0x18,+0x1C)` (window lights, signboards at night) |
| 6 | obj_anime | 143 | `OBJ_ANIME_SEQ` (0x90): `+0x40.x` kind (0 rotation in degrees, 1 translation, 2 scale, 3 colour at frame `+0xd0`, 128 = 1.0), `+0x44` mode (0 constant velocity, 1 wrap to start, 2 bounce, 3 stop at end, 4 random per axis in [init,end] each tick, 5 random walk clamped, 6 one random value in [init,end] for all axes, 7 one-value random walk), `+0x48` number of steps (if > 0 delta = (end - init)/steps), `+0x50` start, `+0x60` delta per tick (60 Hz), `+0x70` end, flag `+0x24`; `InitObjAnime`/`ObjAnimePlay` 0x166010/0x1661e0. 72 of the records are "kind 0 mode 0" rotations (windmill vanes `hane`), 33 are the window flicker (kind 3, mode 6, 80..128) |
| 7 | camera_zone | interiors | box [`+0x60`,`+0x70`] in the local space of frame `+0x30` (matrix inverse * `+0x80`) selects a fixed camera for the interior (`SetCameraPos` 0x19e520) |
| 0 | empty | 3 | unused record in 3 interior files |

Per type the textual labels in the Japanese comment names (川の音 river, 水車の音 water wheel, 風車の音 windmill, 滝の音 waterfall ...) are only labels.

## 4. `mapinfo.cfg`

Parser: `LoadEditMapData(EDIT_MAP_INFO*, char* path, int mapNo)` (0x174390) -> `CScriptInterpreter` (`GetNextTAG` 0x15f100, `SearchCommand` 0x15fb80, `GetArg` 0x15f790, `PreProcess` 0x15fe30) with the directive table `Command` (0x2679D0, 61 entries x 100 bytes: name pointer, argument type list terminated by -1) and the handler array `CommandExe` (0x2691B0). The loader first tries `mapinfo.cfb` (binary form: a list of `{directive, args...}` records; absent from the disc), then renames the extension to `.cfg`.
**Verified**: `tools/dc/mapinfo.py` parses all 68 files (and all 136 interior `.cfg`) without error using exactly the game's rules.

### 4.1 grammar

* `//` to end of line and `/* ... */` are blanked first. Whitespace = space, TAB, CR, LF.
* Reading goes word by word: a word is everything up to the next whitespace. A word that starts with `A-Z` and is in the directive table starts a directive; **every other word is skipped silently** (the leftover numbers of commented-out directives, `DebugFlag ON`, `RUN_SPEED 1`, `LEVEL_FAR 10,300,500,2000` of an older engine are skipped this way in 45 files). The word `test` stops the parse. The word must be followed by whitespace.
* Arguments follow in the table order. A token ends at the first `,` or whitespace; a `,` directly in front of a token is eaten (so `1,2`, `1 2`, `1 , 2` are equal; missing commas are fine, e.g. `PEOPLE2 "p08a" 0, 1, 0.3`). The character after the token is consumed.
* Types: `S` string - token must start with `"` and contain the closing `"` (no spaces/commas inside); `I` int - only `0-9` and `-`; `F` float - only `0-9 . -` (`atof`, so `0.0.` reads as 0.0). A bad token makes the real game loop forever (`GetNextTAG` prints and spins); no file on the disc triggers it.
* Directives after an unknown/early end simply continue; there is no nesting. State is carried between directives: the current directory (`CD`), the current light slot (`LIGHT_NO`), the current owner for `OBJ_ANIME/MAPJUMP/ENTRANCE` (the last `SKY/SUN/GROUND/BUILD/WATER/*_PARTS`), the current part (`PARTS_INFO`), the last `PEOPLE2` (`TALK_*`), the current week (`TIME_TABLE_NO`).
* Any path argument is `CurrentDir + string` (`"%s%s"`), an empty string means "none".
* `if (...) { }` blocks exist in the interpreter (`ControlCode`, one built-in function `test`) but no file uses them.

### 4.2 directives (types in brackets; frequency over the 68 files in parentheses)

Environment: the file keeps up to 12 light sets, selected by `LIGHT_NO n` (1..12, 1 = first; clamped).

| directive | args | effect |
|---|---|---|
| `LIGHT_NO` (200) | [I] n | selects set n-1 for AMBIENT/LIGHT_C/FOG/BG_COL/BG_COL2. Convention in the files: 1-3 day, 4-6 dusk, 7-9 night, 10-12 morning |
| `AMBIENT` (200) | [F F F] r g b | ambient colour (0..128 scale) -> `EDIT_MAP_INFO+0x70+0x10*set` |
| `LIGHT_C` (281) | [F F F F F F I] dx dy dz r g b k | directional light k=1..4 of the set: direction (towards the light, normalised) and colour (128 = full); stored as a column of a 4x4 matrix (`+0x130+0x40*set`) and colours `+0x430` |
| `FOG` (200) | [F F I I I F F] near far r g b a_near a_far | depth fog: colour and the alpha ramp from `a_near` at `near` to `a_far` at `far` (`MGSetFogParm` 0x12e010); `EdSetLightParam` (0x188d50) blends two neighbouring sets |
| `BG_COL` (200), `BG_COL2` (198) | [F F F] | sky colour top (`+0x730`) / bottom (`+0x7f0`) |
| `DOF` (11) | [F F I F F I I] t0 t1 level near far p5 p6 | depth of field between clock hours t0..t1 (`ConvertTime`), `EdSetDOF` 0x1722a0; near/far distances 400..1000 in e01 (levels 2); last two **(guess)** |
| `SHADOW_LEVEL` (2) | [I F F I I] n d1 d2 a1 a2 | projected shadows: n passes, pass 1 for distances [0,d1) alpha a1, pass 2 [d1,d2) alpha a2 (defaults 2, 320, 740, 52, 32) |
| `WIND` (11) | [F F F F] | wind parameters (defaults 0.3 0 0.1 0.4) **(meaning unknown)** |
| `CHARA_AMBIENT` (2) | [I F F F] k r g b | ambient colour for characters standing on floor polygons of type 3 (k=1) / 4 (k=2): `VillagerCollision` 0x17f9a0 |
| `SKY_FOLLOW` (4) | [I I I] | which axes of the sky dome follow the camera (default 1 0 1; stored at `+0x1be0`) |
| `TIME_STOP` (55) | - | the day clock is frozen in this map (`+0x40`) |
| `BGM_NO` (20) | [I] | BGM number (`sound/bgm/bgm%d.snd`), default -1 |
| `SOUND_SET` (37) | [I] | ambient sound set (`sound/set/snd%d.snd`) |
| `REVERBE` (8) | [S I S I] | SPU2 reverb mode and depth for core 0 and core 1; names `OFF ROOM STUDIO_A STUDIO_B STUDIO_C HALL SPACE ECHO DELAY PIPE MAX` (defaults STUDIO_A/5, STUDIO_C/30) |
| `SE_AMBIENT_OFF` (13) | - | no ambient SE |

Resources (all paths are `CD` + name):

| directive | args | effect |
|---|---|---|
| `CD` (115) | [S] | current directory prefix, e.g. `"gedit/e01/img/"` |
| `GRD_IMG BLD_IMG SUN_IMG WATER_IMG FIRE_IMG FLER_IMG` | [S I] name flag | texture container to load from `img.pak` (type tag 1, 2, 7, 0x15, 0x18, 0x17) |
| `SKY_IMG` (51) | [I S I] n name flag | sky texture n=1..4 (type n+2) |
| `IMG` | [I S I] | generic with explicit type (not used) |
| `SKY` (59) | [I S I] n name flag | sky dome n=1..5 (n=5 = back plate); model from `mds.pak`; later `OBJ_ANIME`s attach to it |
| `SUN` (38) | [I S I] | sun/moon model n=1..4 (`sun1 sun2 sun3 moon` frames) |
| `OBJ_ANIME` (96) | [I I S F F F F F F F F F] kind mode target init(3) delta(3) end(3) | object animation on frame `target` of the current owner (rotation in degrees for kind 0); same record as 3.4 type 6; max 127 |
| `FIRE FLAME BRIGHT OBJECT_TIMER` | - | parsed but ignored (no-op handlers); never used |
| `SCN` | [S] | name of the SCN file (default `scene.scn`); never used |

Static objects and water (EDIT_MAP_INFO+0x25a0, 0x2d8 bytes each, at most 64):

| directive | args | effect |
|---|---|---|
| `GROUND` (363), `BUILD` (6), `WATER` (31) | [S x8, F x6] names..., x y z, rx ry rz (degrees) | static object; kind 1/2/0x15. Name 0 is first looked up as an SCN entry (`SearchPTS`); if absent the 8 names are file names: LOD0 LOD1 LOD2 LOD3 shadow collision shade ... . With a PTS the other names are ignored. Position/rotation override the PTS' own offset. **Quirk**: the handler loops over 9 name slots but only 8 strings are parsed; slot 9 reads the first byte of the x float, so a non-zero low mantissa byte produces a junk name (harmless, the load fails) |
| `WATER_SURFACE` (16) | [S I I F F F F F F F F F F F F F I I I I I I] tex nx nz p0(3) p1(3) origin(3) param(4) rgb(3) extra(3) | animated water sheet (`CWater`), nx*nz vertices over the rectangle (p0.x,p0.z)-(p1.x,p1.z) at height p0.y, drawn at `origin`; `param` = `SetParam` (0.1 0.015 0.0 2.0 in Norune), colour rgb, alpha 0x80 |
| `WATER_SHAKE` (21) | [I I F F] | extra wave source appended to the last WATER_SURFACE (cell x, cell z, 2 floats) - meaning of the floats **unknown** |
| `MOTION_PARTS` (5) | [S F F F F F F] | animated `.chr` model (name, pos, rot), up to 4 (windmills, flags) |

Georama editing:

| directive | args | effect |
|---|---|---|
| `EDITAREA` (9) | [I S I I F F F F F] n grid w h unit unit_alt x y z | edit area n (0..3): grid model (`e01g01.mds`), size in cells, cell size, altitude step, origin. The area origin is shifted by `-0.01*cells` (`LoadGroundData`; **guess**: so cell borders do not coincide with the integer coordinates of the models) |
| `EDITAREA_RECT` (10) | [F x6] | cursor/walk limit rectangle (two corners; up to 8; the y of the max corner is the cursor height cap) |
| `BLD_PARTS GRD_PARTS ROAD_PARTS RIVER_PARTS BRIDGE_PARTS LAKE_PARTS ON_RIVER_PARTS` | [I S I F (S)] n name flag lift (water model) | placeable part type n (0..23). Kinds (`MAP_PARTS_INFO+0x244`): building 0, ground piece 0, road 1, river 2, bridge 3, lake 4, on-river 5. `name` ending in `.pts` is an SCN entry; otherwise it is the base name of `<name>0.mds <name>1.mds <name>2.mds <name>m.mds <name>s.mds <name>a.mds <name>k.mds`. `flag` = rotation locked (1) per the file comment, `lift` (stored at `CMapParts+0x11c`) lifts flat parts a little off the ground in `DrawLOD` (1.0 for roads/trees/lakes). Parts 0..23 |
| `ROAD n` (18), `RIVER n` (16) | [I S I F (S)] | the six road variants (0..5) / sixteen river variants (0..15) belonging to the preceding `ROAD_PARTS`/`RIVER_PARTS` (+ water model for rivers). The variant used for a cell is chosen by `CEditArea::SetRoadParts/SetRiverParts` from the 4 neighbours (variant index = shape-1): shape 1 corner, 2 straight, 3 T, 4 cross, 5 isolated, 6 dead end, plus rotation 0..3; so `ROAD 0..5` = corner, straight, T, cross, isolated, dead end |
| `PARTS_INFO` (7) | [I I S I (I S)x6] w h grid max (id names)x6 | footprint of the preceding part (grid chars `0-9 x d`, rows may be separated by `|`), maximum count, six element pairs; this is the same data as the PTS info (3.2) and is only needed for parts without PTS info (roads, rivers, bridges) |
| `MAPJUMP` (60) | [I S] id map | door of the current owner: creates event point kind 1 with the destination `map` (an interior name `i01h01`, `dungeon`, or another map) |
| `ENTRANCE` (2) | [S F F F F F F F F F] side x y z rx ry rz ex ey ez | refines the preceding MAPJUMP: `L`/`R`/other = door side (-1/1/0), door position, direction, valid distance (extent) |

Villagers (`EDIT_MAP_INFO+0x18400`, 0x90 bytes, max 16):

| directive | args | effect |
|---|---|---|
| `PEOPLE2` (115) | [S I I F] model indoor_only moves speed | villager; the model is `chara/<model>.chr` (`pNNa`). Index = order of appearance (0..15). Placement comes from the `person` records of the parts |
| `PEOPLE` | [S I I I I F F F F F] | older form with explicit part/element/position (not used) |
| `TALK_EVENT` (7) | [I I] | event numbers run when talking to the last PEOPLE2 (`+0x60`, `+0x64`) |
| `TALK_ROT`, `TALK_DIR` | [I] | talk facing options of the last PEOPLE2 |
| `TIME_TABLE_NO` (9) | [I] week 0..6 | selects the table (only week 0 is read by `EdInitVillagerTable`) |
| `TIME_TABLE` (36) | [I x7] slot v0..v5 | slot 0 day, 1 dusk, 2 night, 3 morning: the six villager indices present in this phase (-1 none) |
| `PEOPLE_LIST` (4) | [I x15] | if present, `EdCreateVillagerTable` (0x185320) randomly shuffles these (`rand`) into the four phase rows instead |

Interior config files (`in/<house>/<house>{m,e,n}.cfg`, `gedit/interior/mapinfo.cfg`; 136 files, all parse): `LoadInfo` (0x19fb70) with the 15 entry table at 0x26A7F0: `AMBIENT [FFF]`, `LIGHT_C [FFFFFFI]`, `FOG [FFIIIFF]`, `BG_COL [FFF]`, `PROJECTION [F]` (camera projection, default 800), `PEOPLE [I F F F F]` (villager number, x y z yaw), `CD [S]`, `OBJ_ANIME`, `FIRE/FLAME/BRIGHT [S F F F F]`, `DEBUG`, `MOTION_PARTS [S F x9]`, `WATER_SURFACE`, `WATER_SHAKE`. The interior `.scn` (one PTS) + `.img` follow the same part format.

### 4.3 `EDIT_MAP_INFO` layout (0x2c260 bytes, written by the handlers) **(code)**

```
0x00000 char[0x40] scn name            0x00040 time_stop   0x00044 se_ambient_off  0x00048 bgm  0x0004c sound_set
0x00050 reverb idx core0, 0x54 core1, 0x58 depth0, 0x5c depth1      0x00060 wind[4]
0x00070 ambient[12][4]                  0x00130 light dir matrices [12] (0x40)    0x00430 light colours [12][4][4]
0x00730 bg top[12][4]                   0x007f0 bg bottom[12][4]                  0x008b0 fog[12] (0x14: near far r g b(bytes) a_near a_far)
0x009a0 chara ambient[4][4]             0x009e0 textures[64] (0x48: path[0x40], type, flag)
0x01be0 sky_follow[3]                   0x01bf0 sky[5] (0xf8: path, anim ids short[8] at +0x98)   0x020c8 sun[4] (0xf8)
0x025a0 static objects[64] (0x2d8)      0x0dba0 edit areas[4] (0x5c)              0x0dd10 placeable parts[24] (0x2d8)
0x12150 INIT_PARTSINFO[24] (0x124)      0x13cb0 roads[6] (0x2d8)                  0x14dc0 rivers[16] (0x2d8)
0x17b40 motion parts[4] (100)           0x17cd0 water surfaces[8] (0xc0)          0x182d0 edit rects[8] (0x20: max, min)
0x183d0 dof[7]  0x183ec shadow level    0x18400 villagers[16] (0x90)              0x18d00 time table[7][12][16 ints]
0x1a200 people_list[15]                 0x1a240 counters (obj anime, effect, timer, event next ids)
0x1a260 OBJ_ANIME_SEQ[128] (0x90)       0x1ea60 EDIT_EFFECT_INFO[64] (static objects)   0x20660 EDIT_EFFECT_INFO[64] (placeable parts)
0x22260 EDIT_OBJECT_TIMER[128] (0x20)   0x23260 ED_EVENT_POINT[256] (0x90, index 0 unused; ids are what the owners' short lists reference)
```
`MAP_PARTS_INFO` (0x2d8): `+0x000` 9 x name[0x40] (LOD0..LOD3, shadow, collision, shade, water, camera), `+0x240` flag, `+0x244` part kind (0 building, 1 road, 2 river, 3 bridge, 4 lake, 5 on-river), `+0x248` family (1 ground-like, 2 building), `+0x24c/0x250/0x254` position, `+0x258..0x260` rotation (radians), `+0x264` lift, `+0x268` short[8] animation ids, `+0x2c8` short[8] event ids.

`ED_EVENT_POINT` (0x90): +0x00 in use, +0x04 MAPJUMP id, +0x08 map-flag id, +0x0c placeable part id (or -1 for static), +0x10 kind (1 map jump, 2 item, 3 event, 4 ladder bottom, 5 ladder top), +0x14 owner `CMapParts*` (static), +0x18 door frame, +0x1c.. kind specific ints (item id, count / event number), +0x30 destination map name, +0x40/+0x44 time window, +0x50 position, +0x60 trigger extents (10 for mapjump, 15 for item, 6 for ladders, the record's scale for event zones), +0x70 rotation, +0x80 out position. `EdGetEvent` (0x184350) picks the closest active point whose distance (3D, or horizontal + height window for items) is below the extent and, for kinds 1,4,5, whose facing matches within `DAT_002a19f0` rad.

## 5. `gdata0.edt`, `fconv.bin`, save layout

### 5.1 `gdata0.edt` (+ `gdata0.edt_`) **(verified)**
Written by `CEditGround::Save(char*)` (0x1a4780 - it even writes to the developer path `host0:y:/ps2/dc_data/gdata0.edt`) and read by `CEditGround::Load(char*)` (0x1a4910). `gdata0.edt_` is a byte-identical backup (e01).

| offset | type | field |
|---|---|---|
| 0x00 | i16 | record count n |
| 0x02 | i16 | total size = 8 + 16*n + 16 (= file size, verified) |
| 0x04 | i16 | offset of the records (8) |
| 0x06 | i16 | padding (junk in e05) |
| 0x08 + 16*i | 16 | record `{i16 part, i16 rot, f32 x, f32 y, f32 z}` |
| 0x08 + 16*n | 16 | terminator `{-1, -1, 0, 0, 0}` |

`part` = index into the `BLD_PARTS/...` list of mapinfo.cfg (0..23; for roads/rivers the base part: `Load` substitutes the matching variant), `rot` in {0,1,2,-1}, (x,y,z) = `CMapParts` position = footprint centre: grid corner for even extents, cell centre for odd extents, `y = area.origin.y + alt*unit_alt`. `Load` re-places the parts with `SetMapParts(x+1.0, y, z+1.0, part, rot)` (the +1.0 is a margin against float rounding **(guess)**) in three passes: (1) every record whose part has raising cells (`ChangeAltData`, digits in the grid), (2) every record in file order, but for bridge/on-river parts (kinds 3 and 5) the first *river* part is placed at that spot instead, (3) the bridge/on-river records themselves; then `RemakeGrid` for all areas.
The identical record format is used by the save game (`CEditGround::Save(int map, CSaveData*)` 0x1a4c90: at `CSaveData + 0x6e4 + 3000*map` are 24 x 0x18 `EditPartsInfo` records, at `+0x924 + 3000*map` up to 0x82 placement records terminated by part < 0). **Role of the file**: `EditInit` reads `gdata0.edt` only when `old_main_mode == 7 && main_select_padrup == 0` (a debug/test start; it also marks all 22 part types complete); a normal start uses `EditLoad` (save data). So the five files are developer snapshots of the *completed* towns: e01 64 records (one each of parts 0-8, 10, 11 = houses/mills, 1 lake [12], 8 trees [13], 24 road pieces [14], 16 river pieces [15], 4 bridge pieces [16]), e02 67, e03 39, e04 42, e05 14.
Validity check (`georama_check.py`, **verified**): all records are inside the edit areas, on the grid, never overlapping (a non-road may sit on a road), counts <= `max` for e01, e03, e04, e05. e02 exceeds `max` for two part types (part 16: 38 pieces, max 33; part 17: 4 pieces, max 2) - a developer snapshot made before the limits were set (the placement rules themselves are not violated).

### 5.2 `fconv.bin` **(verified)**
Not a conversion table: a message bank in the `.mes` format of `dccore/text.*` (`short[0]` = table size, then `{id, offset}` pairs, text as 16-bit codes), containing the NPC chat texts of the map. `EditInit` loads `<dir><map>talk_<lang>.mes` (e.g. `gedit/e01/e01talk_1.mes`) as the chat bank and falls back to `<dir>fconv.bin`, then to the hard coded `gedit/e01/fconv.bin` if the localised file is missing. Its id set equals the English `talk_1.mes` (e01: 110 ids, 93 identical texts, 17 differ = an older wording; s01: 34 ids, 18 identical ...). Present in e01, e05 and 11 story scenes (13 files); the id set is identical in 12 of them (e05: 28 vs 29 ids). (The event script messages are `<map>_<lang>.mes` loaded by `LoadScript` 0x177800 together with `event.stb`.)

### 5.3 other files
* `img.pak` also holds `texanime.cfg` (`TEX_ANIME n,loop` / `TEX_ANIME_DATA "src",x,y,w,h,"dst",x,y,wait,flag` / `TEX_SCROLL_DATA "src",x,y,w,h,"dst",x,y,du,dv,..` / `TEX_ANIME_END`: water ripple frames and scrolling water), parsed by `CTextureAnime::LoadCFGFile`.
* `event.stb`: dungeon-style script (`CRunScript`), disassembler `tools/dc/stbdis.py`; the function number is the event number given to `RunEvent` (event points, `MapJump(map, event)`, system events).

## 6. Data model: how a town is built and walked

### 6.1 Loading sequence (`EditInit` 0x178380, in this order)
1. Reset `NowTime`, `VisitMap(MapNo)`, derive `EditMapName`/`EditDataDir` (section 0), set up nine `CDataAlloc2` arenas (`SetDataBuffer`), allocate `EditArea[4]` (0x2060 bytes each), `CEditGround` (0x20960), `ObjParts[24]` (CMapParts, 0x2a0), `MotionParts[4]` (0x11b0), `RiverParts[16]`, `RoadParts[6]`, `EditMapInfo` (0x2c270), `EdInInfo`.
2. `LoadEditMapData(EditMapInfo, "gedit/<map>/mapinfo.cfb|cfg", MapNo)` (section 4). For interior test it loads `gedit/interior/mapinfo.cfg`.
3. Sound: BGM (`BGM_NO`), `SOUND_SET`, `EdInitSoundSrc`.
4. Event/system scripts: `EdInitEventParam`, `LoadScript` (`event.stb`, `<map>_<lang>.mes`, `gedit/system/event.stb`).
5. Camera: `CCameraFollow` init; `LoadTexture` (0x180c00): fixed system textures (`meswin/mes_tex.pak`: gaiji, fuki256, syst04; the language dependent `gedit/system/esys_<lang>.pak`: `e01t02.img` (hard coded for every map), `cursor.img`, `system.img`, `pause.img`, `s_eff.img`, `whatsday.img`, `ankfont.img`) + every `*_IMG` texture of mapinfo.cfg from `img.pak` (`LoadTextureBlock`), the helper models `cursor01/02.mds bic.mds ibox_0/1.mds`, `texanime.cfg`.
6. **`LoadGroundData` (0x1815e0)**: load `mds.pak`; create `SKY` frames 1..4 (+5 back plate) and `SUN` frames with their `OBJ_ANIME`s; create the (up to 4) `CEditArea`s from `EDITAREA` (grid model from the pack, `SetSize`, `SetOffset(origin - 0.01*cells)`); load three hard-coded helper models `gedit/e01/mds/e01a03_0.mds, e01a04_0.mds, e01a05_0.mds` (for MapNo < 6, purpose unknown, probably edit cursor pieces); `MOTION_PARTS`; read `scene.scn` (`EdNPCBuffer`); `CEditPartsInfo::Initialize(map)`; `LoadObjectParts` (0x182390): for the 24+6+16 part definitions look the name up in the SCN (`SearchPTS`) and `LoadPTS` it into `ObjParts[]/RoadParts[]/RiverParts[]` (copies of a name already loaded are made with `CopyCMapParts`); if there is no PTS info the `PARTS_INFO` of mapinfo is converted by `EdInitToEPInfo`; each part gets `+0xe8` part number, `+0xf0` index, `+0x118` kind, `+0x114` flag, `+0x11c` lift. Then the static objects (`GROUND/BUILD/WATER`): `LoadPTS` (or the file based loader), position/rotation from mapinfo, kind in `+0xe4`, slot in `+0xe8`; finally the `WATER_SURFACE`s (`CWater`).
   `LoadPTS` per part (0x1828b0): copy the info block, relocate pointers (+0x3c, +0x58.., +0x70), `LoadMapObject` (9 slots), bounding box of LOD0 + collision, `InitObjAnime` for the part's animation ids, `EdInitEventPoint` (function records -> event points), flag initialisation, `EnterPartsEffect` per record (effects, timers, `ED_EVENT_POINT`s, `person` records appended to `CEditGround+0x20758`).
7. Layout: `EditLoad` (save data -> `CEditGround::Load(int, CSaveData*)`) or the `gdata0.edt` debug path, `EditPartsObjectOnOff`, `MakePartsBox`, `RemakeGrid`.
8. `GameMode = 1`, villagers (`EdCreateVillagerTable`, `EdInitVillagerTable`, `EdLoadVillager`), the player (`EdLoadMainChara("chara/c01d.chr","info.cfg")`) is put at the world origin looking along +Z with a follow camera at (0, 14, 0) - the real start position comes from the start event (`StartEventNo` passed by `MapJump(map, event)`, executed from `event.stb`) or, when coming out of a house, from `EdSearchEvent` (0x184cb0) = the matching MAPJUMP event point's out position/yaw (`GetPosRot`); fade-in; messages (`<map>talk_<lang>.mes` -> `fconv.bin`).
9. Per frame `EditLoop` (0x1797e0): `GameMode` 1 walk (`MainMode` -> `MoveChara` 0x17f460 -> `EdMoveChara` 0x16a160), 2 talk, 3 time change, 4 edit (cursor on the grid, `EditMode`), 5/7 transitions, 6 edit menu, 8 item menu, 9/10 pause menu, 0xb door opening, 0xc interior (`EditInInit`/`EditInLoop`), 0xe event script, 0x10 fishing.

### 6.2 Edit grid (`CEditArea`, 0x2060 bytes) and ground (`CEditGround`)
* `CEditArea`: +0 area id, +4 map, +8 width, +0xc height (cells, at most 16 in z and ~18 in x), +0x10 origin (x,y,z), +0x20 cell size, +0x24 altitude step, +0x28 + 0x1c*z + 0x1c0*x cells {partNo (+0), alt (+4, 0..0x7f), ? (+8), partID (+0xc), code (+0x10: 0x80/0x81 from the footprint), extra = part kind (+0x14)}, +0x2028 grid model, +0x2030 bounding box of used cells.
* World <-> cell: `ix = (int)((x - ox + 0.1)/unit)`, `iz = (int)((z - oz + 0.1)/unit)` (`GetPos` 0x16d880; `0.1` is a rounding bias), corner of a cell `(ox + ix*unit, oy + iy*unit_alt, oz + iz*unit)`. Cell height `GetAlt = origin.y + alt*unit_alt`. Norune: `EDITAREA 0 "e01g01.mds" 14 12 100 10 (-700,0,-700)` = 14x12 cells of 100 -> x in [-700.14, 699.86), z in [-700.12, 499.88). The four towns: e01 14x12 (100/10), e02 three areas 6x8, 12x6, 7x8 (100/50), e03 three areas 7x9 @y170, 11x7 @y70, 7x8 (100/50), e04 14x14 (100/10), e05 6x8 (80/10).
* Placement (`CEditGround::SetMapParts` 0x1a0470, `CEditArea::SetMapParts` 0x16e010): find the area containing the point (`GetAreaCode`, first match), `CheckParts` 0x16f200 (the whole footprint inside the area; every footprint cell with a non-zero code must be free - a cell that already has a partNo blocks unless the new part is not a road and the old one is a road; rivers need `alt <= 0` and no 4-neighbour of kind bridge/lake/on-river, lakes need `alt <= 0` and no river/bridge/on-river within one cell around, on-river parts (kind 5) only fit on a cell that holds a river, `extra == 2`), count against `max`, copy the template `ObjParts[n]` (0x2a0 bytes) into the first free of the 128 slots (`CEditGround+0x30 + 0x2a0*i`), `SetRotY`, position = cell corner (+ half a cell if the rotated width/height is odd) with y = `GetAlt`, then mark the footprint cells (`0x80/0x81` -> partNo/partID/extra/code; digits -> `AddAlt`). Roads and rivers then re-pick their shape for the cell and its 4 neighbours from the neighbour kinds (`SetRoadParts` 0x16e8b0 / `SetRiverParts` 0x16e470, code `(shape<<4)|rot`, shape 1 corner, 2 straight, 3 T, 4 cross, 5 isolated, 6 dead end; the variant index is shape-1; rivers treat kinds 2, 3 and 5 as connected; for map index 1 (e02) `CEditArea::GetPartsExtra` (0x16de90) additionally reports a river for six hard-coded cells just outside the three areas, so that river pieces at the border connect to the surrounding water). Deleting reverses it (`DeleteMapParts`).
* Static objects live in `CEditGround+0x15f40 + 0x2a0*i` (64), the edit areas' pointers at +4..+0x10, the 4 `CWater` at +0x15040, the part definitions at +0x15f30/34/38.

### 6.3 Collision, walking, camera
* The player uses the same `MoveCheck`/`CheckHit`/`GetFootPoly` as the dungeon (`dccore/collision.*`, `walk.*`). Every tick `EdMoveChara` gathers polygons with `CEditGround::PickUpPoly(CCPoly*, x, y, z)` (0x1a4ee0): a box +-30 in x/z and +-100 in y around the player is tested against (a) the bounding boxes of the 128 placed parts and their collision meshes (slot 5, `PickUpNearPoly`), (b) the 64 static objects' collision meshes, (c) for each edit area the **flat cell polygons** `CEditArea::PickUpPoly(rect)` (0x16f730): two triangles per cell, 1% larger than the cell, at `y = area.origin.y` (not the cell altitude; terrain raisers carry their own collision), normal (0,1,0), attribute short = footstep sound id of the *map* (`sound_740[NowEditMap]`); cells with code `0x81` get none. Then event-point polygons (`EdEventPointCpPoly` 0x184a50: item pickups are solid), villager polygons (`CNPCharacter::PickUpPoly`), fishing polygons. The collision response is the same `MoveCheck` library routine as in the dungeon.
* Camera: `CCameraFollow` (follow point = player + movement*7, +14.. height) with the dungeon's auto-camera: `PickUpCameraPoly(box, mask 0xffff)` (0x1a5210) gathers the camera meshes (slot 8) of placed parts (mask bit 1) and static objects (mask bit 0) plus the edit area cell polygons, `CheckCameraWidth`, `CheckHits`, `CameraAutoMove`. Free first-person view toggles with button 2 (`viewMode`).
* Player start: not geometric, see 6.1 step 8.

### 6.4 Time of day
`EdSetLightParam(t, night, EDIT_MAP_INFO, sky frame)` blends light set `i = floor(t)` with `i+1` (wrapping 11 -> 0) with weight `t - i`: ambient, the four directional lights (matrices normalised after the blend), fog (near/far/alpha/colour), background colour; the bottom sky colour is written into the vertex colours of the sky frame. `NowTime` is advanced elsewhere by the clock (`TIME_STOP` disables it).

## 7. Verification summary

| check | result |
|---|---|
| SCN: 204 files (68 scene.scn, 136 interior) | all parse, entries contiguous from 0xC10, sizes sum to the file |
| PTS: 522 blocks | header/slots/info/function array consistent; models contiguous and exactly fill the block; all models start with `MDS\0` and have a sane object table |
| function record frame targets (town, 960 non-sound-source records) | 950 resolve (`__` rule) in the part's models; the 10 others are stale names: `effect`, `effect1` (obj_anime), `func_fire00`, `null323`, `kwin1`, `wim1`, `s1601` x2, `obj6`, `fin_4` |
| textures referenced by the 384 parts' visual models (1710) | 1707 are in the map's `img.pak`; `s140103` (s30) is in s14/s40's pak, `e03t03` (e03, s33) exists nowhere |
| element object names (215) | 213 resolve (+1 `-door1`) |
| mapinfo.cfg: 68 files + 136 interior configs (135 house cfgs + `gedit/interior/mapinfo.cfg`) | all parse with the game's rules |
| mapinfo file references (988 in the 67 maps with a scene.scn) | 946 resolve; 42 dangling: `*_s.mds`/`*_k.mds` shadow names of 4 BUILD objects that the PTS does not carry, 2 WATER + 3 GROUND names in s04/s43/s11/s80/s81, 6 `fire.img/effect.img` in s12 s32 s45 s80-82, 26 villager models of s38/s43 that live in other maps' `chara/`, 1 `pat.chr` |
| `gdata0.edt` e01 e03 e04 e05 | legal layouts under the footprint/cell model (this validates 3.3, 6.2 and the position convention); e02 breaks `max` for two parts |
| `fconv.bin` | message bank, same ids as `*talk_1.mes` in 12 of 13 maps (e05: 28 vs 29 ids) |

## 8. Open questions (where to look)

1. **Meaning of the element ids** (info +0x40, 0..35) and of info +0x14 / +0x1C: `GetEditAtraPartsData` 0x158e00 (static table), `CEditPartsInfo::Initialize` 0x19a570/0x19a740, `CheckPartsInfo` 0x199f90, Atla UI `AtoraCompOrEvent` 0x218610, `DrawAtoraParts` 0x2182c0.
2. **Player start** in a town and how `StartEventNo`/`start_event_no` events place the character: `event.stb` (decode with `python tools/dc/stbdis.py gedit/e01/event.stb [function]`; e01 defines events 256,134,128,1,11,133,132,130,102,300,150,131,200..208; the script ext functions that place people are `_SET_CHARA_POS/ROT`, `_SET_NPC_POS/ROT`, `_GET_EDIT_PARTS_POS` 0x1904c0, `_HOBJ_*`; the ext name table of stbdis is shared with the monster/dungeon tables, so check names against 0x269C50), `EdSearchEvent` 0x184cb0, `RunEvent` 0x1779f0.
3. How a **new game** initialises the Georama save block (which parts/rubble stand in a fresh Norune): `CSaveData` init, `CEditGround::Load(int, CSaveData*)` 0x1a4e10, `_DELETE_ROBO_PARTS` 0x190870; `gdata0.edt` is only the debug/complete layout.
4. `LoadGroundData` loads `gedit/e01/mds/e01a03_0.mds`, `a04`, `a05` (544-byte models, stored in `CEditGround+0x2074c/50/54`, `+0x20744` = 2) for every map < 6: purpose unknown (edit cursor decoration?).
5. `WATER_SHAKE`, `WIND`, `DOF` last two values, `SHADOW_LEVEL` exact blending: `CommandWATER_SHAKE` 0x175970, `CWater::SetParam`, `EdDrawDOF` 0x1722f0 / `DepthOfField`, `DrawShadow__11CEditGround` 0x1a3be0.
6. Rotation direction of the model for `r = 1`: footprint math is verified, the sign of `SetRotY` vs the dungeon's `-90 deg * turn` convention is not (compare a placed building's door with its mapjump out position).
7. Semantics of item params 3/4 (`+0x78`,`+0x7c`), event (`+0x74`), type 5 'light2' and info `f14/f1c`; `ED_EVENT_POINT` +0x1c.. for ladders (`EdInitHashigo` 0x16d720).
8. `OBJ_ANIME` kind 3 target (`CFrame+0xd0`/`+0xc4`): `MGDraw` colour multiply? `ObjAnimePlay` 0x1661e0.
9. Village life (`EdMoveVillager` / `EdMoveVillagerSubMap`, `EdInitVilagerPosition` 0x186510, `EdSetVillagerNextPos` 0x187710): routes of the NPCs, use of `moves`/`speed`/`indoor_only`.
10. Footstep/attribute values of floor polygons (`sound_740[area]`, polygon type shorts 2,3,4) and `FootSoundEnable`.
11. Language/savegame details: `CSaveData` per-map block (3000 bytes: `GetEditPartsInfo` 0x157ff0, `GetParts` 0x158060, `GetGrdNPCData` 0x1580d0).

## Appendix A. mapinfo.cfg directive table (edit maps; `Command` 0x2679D0 / `CommandExe` 0x2691B0, read from the ELF and asserted equal to `mapinfo.TAGS`)

| # | directive | args (S string, I int, F float) | handler |
|---|---|---|---|
| 0 | `SCN` | `S` | `CommandSCN` 0x174ae0 |
| 1 | `LIGHT_NO` | `I` | `CommandLIGHT_NO` 0x174b10 |
| 2 | `AMBIENT` | `FFF` | `CommandAMBIENT` 0x174b60 |
| 3 | `LIGHT_C` | `FFFFFFI` | `CommandLIGHT_C` 0x174be0 |
| 4 | `FOG` | `FFIIIFF` | `CommandFOG` 0x174d70 |
| 5 | `BG_COL` | `FFF` | `CommandBG_COL` 0x174e80 |
| 6 | `BG_COL2` | `FFF` | `CommandBG_COL2` 0x174f00 |
| 7 | `DOF` | `FFIFFII` | `CommandDOF` 0x174f80 |
| 8 | `CD` | `S` | `CommandCD` 0x1741a0 |
| 9 | `GRD_IMG` | `SI` | `CommandGRD_IMG` 0x175100 |
| 10 | `BLD_IMG` | `SI` | `CommandBLD_IMG` 0x175130 |
| 11 | `SKY_IMG` | `ISI` | `CommandSKY_IMG` 0x175160 |
| 12 | `SUN_IMG` | `SI` | `CommandSUN_IMG` 0x1751c0 |
| 13 | `WATER_IMG` | `SI` | `CommandWATER_IMG` 0x1751f0 |
| 14 | `FIRE_IMG` | `SI` | `CommandFIRE_IMG` 0x175220 |
| 15 | `FLER_IMG` | `SI` | `CommandFLER_IMG` 0x175250 |
| 16 | `IMG` | `ISI` | `CommandIMG` 0x175280 |
| 17 | `SKY` | `ISI` | `CommandSKY` 0x1752b0 |
| 18 | `SUN` | `ISI` | `CommandSUN` 0x175350 |
| 19 | `GROUND` | `SSSSSSSSFFFFFF` | `CommandGROUND` 0x1753f0 |
| 20 | `BUILD` | `SSSSSSSSFFFFFF` | `CommandBUILD` 0x175540 |
| 21 | `WATER` | `SSSSSSSSFFFFFF` | `CommandWATER` 0x175690 |
| 22 | `WATER_SURFACE` | `SIIFFFFFFFFFFFFFIIIIII` | `CommandWATER_SURFACE` 0x1757e0 |
| 23 | `WATER_SHAKE` | `IIFF` | `CommandWATER_SHAKE` 0x175970 |
| 24 | `EDITAREA` | `ISIIFFFFF` | `CommandEDITAREA` 0x175a10 |
| 25 | `BLD_PARTS` | `ISIF` | `CommandBLD_PARTS` 0x175c90 |
| 26 | `GRD_PARTS` | `ISIF` | `CommandGRD_PARTS` 0x175d80 |
| 27 | `PARTS_INFO` | `IISIISISISISISIS` | `CommandPARTS_INFO` 0x175e70 |
| 28 | `ROAD_PARTS` | `ISIF` | `CommandROAD_PARTS` 0x176080 |
| 29 | `ROAD` | `ISIF` | `CommandROAD` 0x176170 |
| 30 | `RIVER_PARTS` | `ISIF` | `CommandRIVER_PARTS` 0x176260 |
| 31 | `RIVER` | `ISIFS` | `CommandRIVER` 0x176350 |
| 32 | `BRIDGE_PARTS` | `ISIF` | `CommandBRIDGE_PARTS` 0x1764b0 |
| 33 | `LAKE_PARTS` | `ISIFS` | `CommandLAKE_PARTS` 0x1765a0 |
| 34 | `ON_RIVER_PARTS` | `ISIFS` | `CommandON_RIVER_PARTS` 0x1766b0 |
| 35 | `OBJ_ANIME` | `IISFFFFFFFFF` | `CommandOBJ_ANIME` 0x1767c0 |
| 36 | `FIRE` | `SFFFFFF` | `CommandFIRE` 0x176970 |
| 37 | `FLAME` | `SFFFFFF` | `CommandFLAME` 0x176980 |
| 38 | `BRIGHT` | `SFFFFFF` | `CommandBRIGHT` 0x176990 |
| 39 | `OBJECT_TIMER` | `SFF` | `CommandOBJECT_TIMER` 0x1769a0 |
| 40 | `ENTRANCE` | `SFFFFFFFFF` | `CommandENTRANCE` 0x1769b0 |
| 41 | `MAPJUMP` | `IS` | `CommandMAPJUMP` 0x176bb0 |
| 42 | `PEOPLE` | `SIIIIFFFFF` | `CommandPEOPLE` 0x176cf0 |
| 43 | `TIME_TABLE_NO` | `I` | `CommandTIME_TABLE_NO` 0x176df0 |
| 44 | `TIME_TABLE` | `IIIIIII` | `CommandTIME_TABLE` 0x176e20 |
| 45 | `TIME_STOP` | `-` | `CommandTIME_STOP` 0x176ed0 |
| 46 | `SKY_FOLLOW` | `III` | `CommandSKY_FOLLOW` 0x176ef0 |
| 47 | `SHADOW_LEVEL` | `IFFII` | `CommandSHADOW_LEVEL` 0x176f30 |
| 48 | `EDITAREA_RECT` | `FFFFFF` | `CommandEDITAREA_RECT` 0x176fb0 |
| 49 | `BGM_NO` | `I` | `CommandBGM_NO` 0x177060 |
| 50 | `SOUND_SET` | `I` | `CommandSOUND_SET` 0x1770a0 |
| 51 | `REVERBE` | `SISI` | `CommandREVERBE` 0x1770e0 |
| 52 | `MOTION_PARTS` | `SFFFFFF` | `CommandMOTION_PARTS` 0x177260 |
| 53 | `PEOPLE2` | `SIIF` | `CommandPEOPLE2` 0x177330 |
| 54 | `SE_AMBIENT_OFF` | `-` | `CommandSE_AMBIENT_OFF` 0x177410 |
| 55 | `WIND` | `FFFF` | `CommandWIND` 0x177430 |
| 56 | `TALK_EVENT` | `II` | `CommandTALK_EVENT` 0x177480 |
| 57 | `CHARA_AMBIENT` | `IFFF` | `CommandCHARA_AMBIENT` 0x1774b0 |
| 58 | `TALK_ROT` | `I` | `CommandTALK_ROT` 0x177530 |
| 59 | `TALK_DIR` | `I` | `CommandTALK_DIR` 0x177550 |
| 60 | `PEOPLE_LIST` | `IIIIIIIIIIIIIII` | `CommandPEOPLE_LIST` 0x177570 |


## Water sheets (CWater, native port, 2026-10-19)
`WATER_SURFACE` = `CWater`: an nx x nz grid of heights stepped by `Hamon` every tick: `new = (sum of the 4 neighbours * c2 + ((1 - 2 c2) * 2 cur - old_new)) - damp * (cur - old_new)` with `c2 = param0^2`, `damp = param1` (`SetParam`; the other two params are unused by the simulation, param3 is used here as the texture repeat). `WATER_SHAKE` (cell x, cell z, a, b) adds `b + a * rand01` to a cell every tick (`StepWater` 0x19dae0; a negative cell = a random one; at most 4 per sheet; the cell is clamped to 1..n-2). The colour is `SetColor(rgb, 0x80)` (rgb are the three ints after the 13 floats; 128 = neutral). The three `extra` ints make the sheet follow the camera: x = camera x + dir.x * 50, y = camera y, z = camera z + dir.z * 50 (`DrawWater` 0x193...), otherwise it sits at `origin`. An empty texture name means the first texture of the first `WATER_IMG` container (e01: `e01b20`; guessed from the look, the VU draw code was not decoded). Port: `DcTown::build_sheets / step_sheet / update_sheet` (dc_town.cpp), `Town::water_textures`, `dcgeorama` prints the sheets. Norune's lake, Matataki's river and the swamp now show water (before only the ripple rims were drawn).
