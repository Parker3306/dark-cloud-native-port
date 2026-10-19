# Model upgrade project: notes

Plan: `~\.claude\plans\recursive-sprouting-honey.md` (Stage 0 format + disc patching, Stage 1 changed model in game, Stage 2 AI pipeline on Toan,
Stage 3 all characters/monsters). Design decision: keep every original design; scope = characters and monsters; tools = AISmith 3D (TRELLIS) + Blender 5.2 headless.

## Stage 0 status: DONE (gate passed)
A deliberately changed Toan (hat x1.8) was written with our own tools onto a copy of the ISO and loaded and drawn correctly by the real game.
Chain proven: `tools/dc/mds.py` (parse/write models) -> `tools/dc/pack.py` (rebuild pack) -> `tools/dc/disc.py` (append to DATA.DAT in the DMMY filler, patch DATA.HED + DATA.HD2 + ISO
directory records) -> PCSX2 -> game reads the new file.

### Tools (all in `tools/dc/`, tests alongside)
| File | What it does | Test result |
|---|---|---|
| `mds.py` | parse + write `.mds` models (objects, meshes, groups, materials) | **2,203 unique game models round-trip byte-identical** (19,368 meshes, 2.9 M vertex tuples). Failures (472) are map/boss pieces in `dun/mpd_pack` and a few effect/flag objects; **no character or monster model fails** (only the collision helper `dun/monstor/ecol.mds`). |
| `pack.py` | parse + write packs (`.chr/.pak/.pac/.mpd`) | 1,936/1,936 round-trip |
| `wgt.py` | parse + write skin-weight files | Toan's `.wgt` round-trips |
| `disc.py` | patch a COPY of the ISO; `put(name, data)` in place or appended; `commit()`; `verify()` | HED and HD2 agree for all 5,103 files; 300 random files read back identical |
| `patch_toan_test.py` | builds the hat x1.8 test disc | in-game result confirmed |
| `ingame_reload.py` | loads a save state, syncs the game's in-memory file index with the patched disc, calls the game's `selectChrUnit` to reload a character from disc | worked |
| `explore_*.py` | one-off format exploration helpers | |

### File formats (decoded)
- **MDS** (`mds.py` docstring has the full layout): 16-byte header; object table (count x 0x70: index, size, name[0x20], mesh offset, parent, 4x4 matrix); meshes back to back.
  Mesh header 0x40 bytes (size, #positions, #normals, #colours, group-list size/offset, #uv, #materials, offsets). Blocks: positions, group list, normals, [colours], UVs, materials,
  each followed by remembered padding. Vertex tuples = 3 ints (position, normal, uv index; +colour index if the colour offset is positive). Group types: 3 = triangle list, 4 = triangle strip.
  Material 0x60 bytes, texture name at +0x34. The game builds VU1 packets from this at load time (`CreateVUdataFromMDT`), so plain data is enough.
- **Toan's model** = a skeleton of joint objects (`chn*`, `jnt*`, `eff*`; no mesh) + rigid parts parented to joints (feet, hands, gloves, face `kao`, hair `kami1/2`, hat `boushi`, bag `kaban`,
  belt `beruto`, cloth pieces `cloth1-3`, ...) + ONE smooth-skinned body mesh `skin` (403 positions, 390 normals, 373 UVs, 58 groups, 15 materials, parent = root object 0). 77 objects total.
  `c01s.mds` = a tiny shadow model (flat-shaded).
- **`base.cfg`** (plain text script, Shift-JIS comments, run by `ReadInfo` @0x00139D10): `IMG`, `TEX_ANIME`/`TEX_ANIME_DATA` (face expressions by copying texture regions), `VERTEX_ANIME`,
  `ALLOC_MDT "cloth1"`, `ALLOC_DBUFF "skin"`, `MODEL "c01d.mds"`, `SHADOW_MODEL "c01s.mds"`, `CLOTH x.clo`, `BODY_SIZE`, `MOTION 0,"c01d.mot","c01d.bbp","c01d.wgt"`, `SHADOW_MOTION ...`,
  `KEY_START` + `KEY start,end,blend` lines (motion number -> frame range). The `.bbp` is 77 bind-pose 4x4 matrices (one per object); `.mot` holds the motion tracks.
- **`.wgt` skin weights** (`wgt.py`): chain of records, each 0x20-byte header [target object (76 = skin), bone object, type 20, ?, entry count, more-flag, junk] + count x 0x20-byte entries
  (u32 vertex index at +0, float weight in PERCENT at +0x10). 54 records / 474 entries for Toan. Applied in order by `MotionProc2` @0x00148860: each record blends the listed vertices toward the
  bone's transform by its weight (sequential lerp, so any linear-blend-skinning weights can be converted by choosing the order and per-step weights). Weighted vertices index the position block.
- **Pack** (`pack.py`): entries of 0x50-byte header (name[0x40], data offset 0x50, size, stride, extra) + data padded to 16.
- **Disc**: DATA.DAT followed by 63 MB filler `DMMY.`; growing DATA.DAT in place by taking filler sectors needs no rebuild. DATA.HED (80 B entries) and DATA.HD2 (32 B records + name table)
  hold the same offset/size/sector/count; the game builds a name tree in RAM at boot (`SearchFile` @0x0013E980 -> record with size +8, sector +0xC, count +0x10; `CDRead` @0x0013F560 reads
  `sector + data_sector`). **Save states made on the original disc contain the OLD index**, so tests must call `ingame_reload.py` (or boot fresh on the patched disc).

### Lessons / traps
- Never write scratch data past `0x01F60FFF`: the hook stub starts at `0x01F61000`. (A path string written there crashed the game: "Unrecognized MMI op 72616863" in the log = text executed as code.)
- PCSX2 locks an ISO it has open for writing: open it read-only (`Disc(path, readonly=True)`) from other tools; patch a copy before starting the emulator.
- Do not restart hooks over stub code that has been executed in the same emulator session without reloading a state first (state load resets the recompiler cache).

## Stage 2 groundwork (done this session)
- **Skin weights are plain linear-blend skinning in percent** (`wgt.py`, `tools/dc/explore_weights*.py`): of Toan's 403 body vertices 335 follow one joint at 100%, 65 blend two, 3 blend three; each vertex's percents sum
  to 100. The 54 records are the skeleton's joints in tree order (every ancestor of a weighted joint appears, mostly with 0 entries); only 18 joints (`jnt*`) carry entries. Header field [5] = byte distance to the
  next record (recomputed by `wgt.write`). `wgt.build(template, {vertex: {bone: percent}})` rebuilds a file with the same records. 1,257/1,257 game `.wgt` round-trip, 1,246 rebuild identically from per-vertex weights.
- **Spaces**: vertex x object world matrix (row vectors, translation in the last row, world = local x parent world) = bind-pose model space. Some parts store local coordinates (feet, hat, hair, cloth, bag, belt, gloves,
  right hand), others model-space coordinates with a cancelling matrix (face `kao`, left hand, body `skin`). Game space: Y up, +Z = character front, right-handed, Toan about 15.8 units tall.
  Blender mapping used: (x, y, z) -> (x, -z, y).
- **Rigid parts and their driving joint** (parent object): face/hair/hat -> jnt12_1 (50); l_hand -> jnt10_1 (40); r_hand -> jnt18_1 (66); l_g -> jnt9_2 (37); r_g -> jnt17_2 (63); r_f -> jnt22_2 (7); l_f -> jnt5_2 (19);
  kaban/beruto -> jnt1_1 (3); cloth1 -> head, cloth2/3 -> jnt2_1 (cloth physics from the `.clo` files, `CLOTH` in base.cfg).
- **`.img` container** (`tools/dc/img.py`): `IMG\0`, count, then 0x30-byte entries (name[0x20] at 0x10+0x30*i, u32 offset to a TIM2). Toan: c01d01, c01d01dan, c01d02..05, c01d11 (all 8-bit palettised).
  Face expressions come from `TEX_ANIME` copying 128x48 regions of c01d11 onto c01d01.
- **Exporter/renderer**: `tools/dc/export_model.py` (pack -> neutral npz/json + PNG textures), `blender/dc_build.py` (builds the original in Blender, 3 meshes needed a winding flip, renders 4 views; `smooth` argument = subdivision
  for the reference only), `blender/render_glb.py` (renders any GLB + prints triangle count). Toan = 17 meshes, 2,702 triangles.
- **AI generation tested** (`tools/ai/aismith.py`, AISmith API with BLENDER_EXE set per process): reference image = smoothed (subdiv 2) studio-lit front render with transparent background.
  `generate` (TRELLIS, 1024): ~1 minute, 3.06 M triangles, faceted, no texture. `refine` (TRELLIS.2, target 20,000 vertices, image-guided): ~2 minutes, **19,530 triangles, quad reconstruction, PBR texture**, recognisably the same
  Toan (bandana, face, scarf, poncho, belt+pouch, gloves with fingers, cuffed boots). A FLAT, faceted reference gives a faceted AI mesh: always smooth the reference first.
  Files: `work/models/toan/ai/*.glb`, comparison image `work/models/toan/toan_original_vs_ai.png`.
- **Budget worry (to measure in Stage 1)**: the game expands every vertex tuple of every draw group into 3 x 16 bytes of VU data at load time (SetVuData), triangle lists cost 3 tuples per triangle, strips ~1.2, and the skin mesh
  is double-buffered (`ALLOC_DBUFF "skin"`). Toan today: 2.7 k triangles. Character pool has ~1.08 MB free next to Toan, so a safe target is probably ~5-8 k strip-ordered triangles for the body; to be measured.


## Stage 2 result: realistic Toan IN GAME (this session)
Pipeline that produced a working, animated, realistic Toan on the patched disc (all steps scripted):
1. `tools/ai/realistic_ref.py` (ComfyUI SDXL img2img, 52 % repaint) redraws a Blender reference render; `blender/dc_build.py` with `DC_BULK=1` thickens arms/shoulders first (muscle). Pick = `work/models/toan/real4/row2/real_d52_s422.png`.
2. `D:\AI\concept\cutout.py` -> `pick/toan_real_cut.png`; `tools/ai/aismith.py generate` + `refine` (TRELLIS) -> `ai2/refined_20k.glb`. **TRELLIS GLBs have no normals: always `shade smooth` before judging (the "carved wood" look was flat shading).**
3. `blender/dump_glb.py` -> mesh arrays + its 2048 texture; `tools/ai/project_texture.py` projects the sharp 2D pick onto the texture (front-facing, unoccluded texels; DIS optical-flow registration, warp the image to the mesh).
4. `tools/ai/fit_warp.py`: registers the pick to the ORIGINAL front render (flow) and warps mesh vertices into the original's proportions/game space.
5. `tools/dc/ref_regions.py` labels the original's surface by skeleton region; `blender/fit_bake.py`: weld, cut boot bridge, collapse-decimate to ~4.2 k tris, unwrap per region front/back half (ANGLE_BASED, few seams = few vertex tuples), pack, Cycles bake of the projected colour to a 2048 atlas. (Smart UV project or a cylinder map were tried first: far too many seams / overlapping layers.)
6. `tools/dc/build_character.py`: weights = nearest original surface sample (rigid parts 100 % to their joint) + 2 smoothing passes, top 3 influences; weld positions, normals, strips (greedy; leftovers as a triangle list; 2.0 tuples/tri); ONLY `skin` keeps a mesh, rigid parts' meshes removed, cloth meshes shrunk x0.001 with the transparent texture `c01d04`; new IMG = 256x256 8-bit `c01d02` (quantised from the atlas) + 64x64 transparent `c01d04`; `TEX_ANIME` blocks removed from `base.cfg`; pack rebuilt; HD atlas written under the PCSX2 replacement name. `tools/dc/patch_character.py` patches a disc copy; `tools/dc/ingame_reload.py` + `tools/dc/pose_shots.py` (forces CharaMain+0xC68 motion ids) test it.
Result: loads, no freeze, 60 fps, all tested animations (idle, run, attack, charged attack, death, throw) deform correctly with the original `.mot/.bbp`. Budget used: 2,189 positions, 4,149 triangles, 8,273 draw tuples (original 4,792 tuples), .wgt 135 KB (original 17 KB), pack 1.57 MB (original 1.62 MB).
(Updated after the flaw-fixing pass, see below.) Known flaws at first build: face expressions (TEX_ANIME) removed; poncho/turban cloth physics disabled (cloth meshes invisible); boot cuffs still joined across the feet; dark patch on the left chest (body seen through a poncho gap); leg weights are coarse (nearest-sample); pool/VRAM readout not yet taken.
PCSX2 must be closed before patching (it locks the ISO) and given ~30 s after launch before PINE answers; a launch from bash with nested quotes failed once, use the PowerShell tool.

### Flaw-fixing pass (results)
- **Boots**: fixed (cut the bridging faces in `fit_bake.py`; leg weights now use the connected component's side so the nearly touching feet never borrow each other's weights).
- **Strips**: groups are now at most 48 tuples (`chunk_strip_groups`) so the game never has to split a strip.
- **Poncho weights**: poncho vertices prefer the original cloth samples (torso joint) over arm skin (`cloth_bias`).
- **Pool measurement** (PINE table at 0x01F06640, 16-byte records {base, 0, used, capacity}): Toan's model pool is the record at 0x01F06660, capacity 0x33450 (214,096 B); original Toan uses 0x21F38 (139,064 B), the new model 0x2A6AC (173,740 B) = 81 % full, only ~40 KB headroom. So other characters/monsters must stay within ~+25 % of their original model's size in their own pool. (VU data and the wgt live elsewhere and did not trigger any problem.)
- **PCSX2 start-up stall**: `C:\Program Files\Tesseract-OCR` on PATH makes pcsx2-qt load the wrong `libharfbuzz-0.dll` ("Entry Point Not Found: DllMain" modal popup). `tools/dc/launch_pcsx2.ps1` launches PCSX2 with Tesseract filtered out of PATH (process-local). PCSX2 also needs 20-60 s before PINE reports a running VM (`tools/dc/ingame_cycle.sh` polls for status 0). Screenshots can pick up an open menu if a popup is showing.
- **Still open**: a dark patch beside the scarf on Toan's right chest (baked colour there is skin-toned, weights are torso, normals and cloth were ruled out; likely reversed-winding faces in the TRELLIS shell culled by the game, 97 conflicting edges remain after `orient_consistent`, which cannot be fully fixed by local flips); face expressions (needs eye-region art + `TEX_ANIME` rows aimed at the atlas); cloth physics for the poncho.
- Tools added: `tools/dc/{ingame_cycle.sh,launch_pcsx2.ps1,yaw_shots.py,patch_character.py,ref_regions.py}`, `tools/ai/fix_dark_torso.py`, `blender/render_zoom.py`.

## Character 2: Xiao (c04b) in game (this session)
Pipeline = the Toan one, parametrised: `tools/dc/build_character.py <name> <fit dir> --pack dun/mainchara/<id>.chr --model-dir work/models/<id> [--cfg key]` with `CONFIGS` (skin object, meshes to keep, body texture name, textures to keep, `full_img`), `auto_profile()` derives leg/arm/torso numbers from the joints, `tools/dc/ref_regions.py <model> <out> auto <keep meshes>`, `tools/ai/redraw_grid.py` (SDXL grid), `blender/fit_bake.py` (`DC_FEET_CUT` optional boot cut). Test loop: `CHAR=1 DISC_NAME='dun\mainchara\c04b.chr' MOTIONS="1 11 13 23" tools/dc/ingame_cycle.sh <pack> <out prefix>`.
- **How to test a character that is not in the party**: `selectChrUnit` has no availability check but loads the weapon from `UserStatus+0x450C+chara*0xAA8` (-1 for characters you do not own -> bad path -> the game resets to the BIOS). `ingame_reload.py` now writes `defWeapon[chara]+1` and slot 0 first; then `selectChrUnit(chara, 1)` works from the first-dungeon save state.
- **Bugs that cost hours**: (1) the builder still wrote Toan's texture name `c01d02` into the new material, so every other character drew with a texture that does not exist (black noise, lit facets) - always check `mesh.texture_names()` of the output; (2) PCSX2 start-up popup from Tesseract's DLL (see above); (3) loading screenshots can show an open menu.
- **Memory** (character-model pool record 0x01F06660 {base,0,used,capacity}): capacity 210,000 B for Toan's and Xiao's slot. Stock Xiao uses 176,477; the new Xiao at 2,400 triangles (4,630 draw tuples, 1,210 positions) uses 191,581 (91 %); 3,200 triangles (97 %) overflowed into the next pool and froze the game; 1,500 triangles used 175 KB. The next pools in the chain have only ~20 KB of slack, so the limit is about stock + 15 KB. The texture container (IMG) does NOT count against this pool in the tests that loaded; keeping the full stock IMG with big meshes did freeze (not understood, avoid: use `full_img="tiny"` or only the textures needed).
- Xiao specifics: rigid parts removed, the root-attached mesh `o` (105 tris behind the waist, vertex-animated) must stay (removing it resets the game); the hand-up model `c04bhand` and `hand_up.cfg` are untouched and still use the original `c04b02` texture; HD file written under its PCSX2 name.
- The first Xiao redraw tile the user picked (r2c2, seed 111) had a red headband the original lacks; seed 114 at 52 % is the same look without it and was used.

## Character 3: Ruby (c05a) in game
- Pick: `work/models/c05a/redraw2/row0/real_d46_s201.png` (46 %). Config `CONFIGS["c05a"]`: body texture `c05a03`, keeps `c05a02`/`c05a04` (the hand-up arm model `c05arm` uses c05a02-04, so its textures now look wrong in that mode), `full_img="tiny"`.
- **Ruby's skeleton quirk**: the MDS object matrices are an arms-down rest pose (rigid hands/bracelets hang at the hips) while the skin mesh and `.bbp` are a T-pose. `.bbp` = a chain of LOCAL bind matrices (row vectors): bind world = local @ parent bind world (`build_character.write_bind` writes `bind_world.json`). Rigid parts are brought to the bind pose with `p @ inv(rest_world[bone]) @ bind_world[bone]` (only when the gap is 0.05..7 units; Toan/Xiao heads give bogus 13-unit gaps) both for the weight reference samples (`reference_samples`) and for the reference render (`blender/dc_build.py`, `DC_BIND=0` disables). Render with `DC_ORTHO=22 DC_CENTER_Y=8.8` (she is 17.5 tall; `tools/ai/fit_warp.py` reads the same env vars).
- Joint regions now come from bone NAMES when they say so (sdr/arm/hand, leg/foot/toe, mune/spine/hip, face) else from the skeleton/position (`joint_regions`).
- Result: 3,996 triangles (7,429 draw tuples) -> pool 180,239 B (85.8 %); stock Ruby uses 133,139 B (63 %) and the next pool has ~70 KB spare, so Ruby has more room than Xiao. Rule of thumb measured here: ~17 bytes of pool per triangle. All tested motions (run, aim, raised arm, knocked down) deform correctly.
- Regression: Toan rebuilt with the new builder still renders correctly.

## Character 4: Ungaga (c10b) in game
- Pack `dun/mainchara/c10b.chr` contains files named `c10a.*` (stem differs from the pack name: `CONFIGS["c10b"]["stem"]="c10a"`). Textures c10a01 (128x128), c10a02/03 (256x256); body = `c10a02`, `c10a03` becomes the 64x64 transparent cloth texture (his two cloth meshes are shrunk and the `CLOTH` lines removed as for Toan). Pick: `work/models/c10b/redraw/row1/real_d50_s112.png`. Render framing `DC_ORTHO=26 DC_CENTER_Y=11` (he is 22 tall); in-game screenshot crop `CROP="780,250,1170,850"` for `ingame_cycle.sh`, character index 4.
- Joint regions are now derived from the skeleton (leaf jnt of a chain decides arm/leg/head by its position, rest OR bind pose; branching joints are torso, branching inside the head is head) because height/shoulder heuristics failed for him.
- 4,495 triangles, 8,377 draw tuples -> pool 145,805 B (69 %); stock Ungaga uses 107,229 B (51 %), so he could go to ~6,000 triangles. All tested motions work, the staff (a separate weapon file) is held correctly.

## Quality review pass (2026-10-18), paused before Osmond/Goro by the user's request
Review loop: `python -I tools/dc/patch_all.py` (all four characters on one disc copy) then `tools/dc/review.sh <out dir>` (fresh PCSX2 per character, 4 yaw angles, one sheet each). **A second state load in the same emulator session aborts the recompiler ("Impossible block clearing failure")**, so every character gets its own emulator.
Improvements made: (1) back views redrawn and projected (`tools/ai/redraw_back.py`, `project_texture.py --back`), (2) surfaces neither view covers take the colour of the nearest covered surface (`tools/ai/fill_uncovered.py`; the TRELLIS side colours were blotchy: cyan dots on Ruby's trousers), (3) black un-baked texels inside UV islands are inpainted and the atlas is lifted to a target mean luminance (the game's lighting darkens a realistic albedo: Toan/Ungaga were 51-58, stock textures are 85-150), (4) the head gets 1.7x texel density (`DC_FACE_SCALE`), (5) Xiao used the wrong TRELLIS mesh (first pick with the red headband) in one rebuild: `tools/ai/rebuild_char.sh` now has the per-character mesh dir (`PRJ`). Whole chain per character: `tools/ai/rebuild_char.sh toan|xiao|ruby|ungaga`.
Tried and dropped: side-view redraw projection (works as an image, but TRELLIS' depth does not match a drawn side view; DIS registration smears the legs).
Open: Xiao's dress and Toan's poncho show dark patches (triangles whose vertex normals point inward on thin double shells), faces are small/dim, no face expressions, no cloth motion, Ruby/Xiao hand-up models use mismatched textures.

## Next
1. `blender/fit_and_skin.py`: align the AI mesh to the original bind pose (landmark/ICP), merge original parts into one weighted reference (rigid parts 100% to their joint), transfer weights, limit to 3 influences, decimate to budget.
2. Texture: bake the AI base colour into a game-budget 8-bit atlas + keep the HD version for the PCSX2 replacement pack (name via scripts/pcsx2_names.py).
3. Mesh export with triangle strips (`mds.py`), new `.wgt` (`wgt.build`), keep `.bbp/.mot/base.cfg`, handle face `TEX_ANIME` (keep the original face part or map the face region), cloth objects, shadow model.
4. Stage 1 budget tests in game (pool/VRAM readout), then Stage 3 batch.

## (older) Next
1. `blender/dc_import.py`: import Toan (parts + skin mesh + bone frames + weights) into Blender as the ground truth for fitting; render the reference views.
2. Validate the skinning math against the live game (read the deformed `skin` vertex block from RAM for a pose and compare with my own skinning of the same pose).
3. Stage 1: write a changed `skin` mesh with matching `.wgt` (e.g. subdivided) and watch it animate; measure pool/VRAM budgets.
4. Stage 2: AISmith generate -> retopo -> fit and skin -> bake textures (game-budget + HD) -> build_model -> disc.


## Update (native port, milestone 3): the Goro texture mystery is solved
`c06a01.img` (Goro's dungeon texture) is an `IM2 (NUL-terminated magic)` container and so are about two thirds of all containers: their 8-bit textures are stored in GS PSMT8 page/block order and need the standard PSMT8 un-swizzle before use (`scripts/tim2.py`, `dccore/tim2.cpp`). Goro now renders correctly in the native viewer (bear hat, green tunic, fur collar). The earlier "textures are scrambled / reading as 512x128 is smoother" observations were this swizzle. Anything in this log that says a texture decoded wrongly for an IM2 container (Goro, NPC forms, town) was affected; the PCSX2 replacement-texture work was not (the emulator hashes the GS view). Goro (c06b) can be resumed whenever the user wants to go on with the model upgrade.
