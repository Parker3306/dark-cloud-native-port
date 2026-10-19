# Graphics upgrade plan

Goal: Dark Cloud that looks at least as good as the PS4 release. The PS4 version (2015) is the PS2 game
running in Sony's emulator at a higher resolution, so stage 1 alone should match or beat it; stages 2-3 go further.

## Stage 1 – emulator settings (done)

`tools/pcsx2/gamesettings/SCUS-97111_A5C05C78.ini` (only applies to Dark Cloud):

| Setting | Value | Effect |
|---|---|---|
| Widescreen patch | on | 16:9; built into PCSX2 (nemesis2000). Changes `lui v0,0x3F80` (1.0) to `0x3F40` (0.75) at `0x0012DF78` in `MGSetViewMatrix` |
| No-interlacing patch | on | Removes interlace half-pixel offset (`sceGsSetHalfOffset`) and skips `EdDrawDOF` (depth-of-field blur) |
| Internal resolution | 4x | ~2560x1792, downsampled to 1080p (supersampling) |
| Anisotropic filtering | 16x | Sharp textures at angles |
| Blending accuracy | High | More correct transparency/lighting |
| CAS sharpening | 40 | Offsets downsample softness |
| Texture replacements | on | Loads HD textures once stage 2 exists |

The GameDB already applies `halfPixelOffset: 2` for this game, which reduces upscaling blur at distance.

## Stage 2 – HD textures (offline from the disc; no playing needed)

Every texture is a standard TIM2 image inside `DATA.DAT` (`.img/.chr/.pak/.pac/.mpd/.tm2`), so the whole
set can be built from the disc instead of dumping it by playing.

Pipeline (all in `scripts/`):
1. `extract_data.py extracted data` – unpack DATA.DAT with DATA.HED (80-byte entries: name[64], offset, size, ...).
2. `tim2.py data` – scan every file for TIM2 headers. 10,022 textures found (8,993 8-bit, 803 32-bit, 203 4-bit, 23 24-bit).
3. `pcsx2_names.py data <dumps>` – compute PCSX2's replacement file name for each texture and check it against
   real dumps (verified: 5 of 12 dumps reproduced by name, and the pixels are bit-identical incl. alpha).
4. `build_originals.py data work/originals` – writes one PNG per unique name (3,306 files, 612 MB).
5. Upscale `work/originals` into `tools/pcsx2/textures/SCUS-97111/replacements` keeping file names (needs the
   Real-ESRGAN model files, not downloaded yet). Alpha must stay in the PS2 range (0x80 = opaque) like the dumps.

How PCSX2 names a texture (v2.8.2 source): `<TEX0Hash>-<CLUTHash>-<bits>.png`
- TEX0Hash = XXH3-64 of the texture's raw swizzled 256-byte GS blocks (8-bit: 16x16 px, 4-bit: 32x16, 32-bit: 8x8),
  block order left-to-right/top-to-bottom. Textures smaller than a block are hashed unswizzled.
- CLUTHash = XXH3-64 of the 256 (or 16) palette entries as u32 RGBA, raw PS2 alpha, logical (unswizzled) order.
  TIM2 8-bit palettes are stored in GS-swizzled order (entries 8-15 and 16-23 of each 32 swapped) unless clut_type bit 7.
- bits = PSM (0x13 = 8-bit, 0x14 = 4-bit, 0 = 32-bit) | log2(width) << 6 | log2(height) << 10.
- Swizzle tables are generated from PCSX2's GSTables.cpp into `scripts/gs_tables.py`.

Status (verified in the running game):
- `upscale_textures.py` upscaled all 3,306 textures 4x in ~5 min on the RTX 4090 (6.7 GB) into
  `tools/pcsx2/textures/SCUS-97111/replacements`. Models are in `tools/realesrgan` (official v0.2.5.0 bundle).
  `realesrgan-x4plus-anime` for crisp graphics (1,726), `realesrgan-x4plus` for soft art/glows (1,580), chosen by an
  edge-hardness score. The anime model invents hard edges on soft sprites (fire, glows) so it isn't used there.
- Proof PCSX2 loads them: tinting `4960e3fe646d58cf-00001600.png` (PUSH START BUTTON) pink turned the on-screen text pink.
  The real file was restored and its hash checked.
- Alpha: Real-ESRGAN leaves alpha untouched, so the PS2 scale (0x80 = opaque) survives.

Coverage (title scene, 89 textures PCSX2 dumped; `scripts/coverage.py`, `scripts/diff_dumps.py`):
- 5 exact name matches with the disc pack.
- 64 are 32-bit 64x64 with no counterpart on disc: generated every frame (animated sky/effects). Not replaceable.
- 16 are 8-bit sharing a (generic) palette with disc textures but different pictures: also generated/modified at runtime.
- 4 more with no disc match. Gameplay coverage is still unmeasured; the title scene is probably a worst case.
- Non-power-of-two textures are hashed over the full power-of-two area, including leftover junk beyond the real width.
  Example: the Dark Cloud logo is 448x64 on disc but dumped as 512x64 (overlapping pixels 100% identical). The name
  cannot be predicted from disc data alone.

Plan for what the disc pack can't cover: dump while playing (kept on in the per-game ini), then upscale the dump
PNGs directly into `replacements` under their own names (`upscale_textures.py` can be pointed at the dumps folder).

Open problems:
- 1,614 textures with non-power-of-two sizes (e.g. 640x448, mostly 8-bit) are skipped: the game draws them through
  a power-of-two TEX0 with a region, so the name needs the region form `<hash>-<clut>-r<W>x<H>-<bits>.png`.
- 23 24-bit textures skipped (PCSX2 hashes those unswizzled with a different path).
- 7 of the 12 sample dumps were not found on disc by data hash. For several the palette exists on disc but the pixels
  don't, so the game builds or modifies those textures at runtime (512x512 and some 256x256 ones). Needs a look at
  how the game uploads them (`CTextureManager::EnterTexture`, `LoadTexture`, `SetTextureInfo` in the symbols).
- 4-bit textures use the same method but none has been verified against a real dump yet.

## Stage 3 – game code patches (Ghidra)

Toggleable patches live in `tools/pcsx2/cheats/SCUS-97111_A5C05C78.pnach` (Game Properties > Cheats).

| Patch | Status | Notes |
|---|---|---|
| Max LOD | written, untested | `CObjectFrame::DrawLOD` @ `0x00157220`: nop the `bne s3,s2` at `0x00157298` so the most detailed model is always used. LOD table: up to 4 models at `this+0xB0`, chosen by squared distance vs a float table (arg 1). Callers: `CMapObject::DrawLOD`, `CMapParts::DrawLOD` |
| 60 FPS | to research | `WaitVSync(int)` @ `0x0012D070` spins until `MGGetVSyncCount()` changes; `MGFlipWaitVSync(int)` sets `mgWaitVSync` (frames to wait per flip). Setting it to 1 likely doubles speed too, so game logic timing must be found |
| Fog distance | to research | `MGSetFogParm(float,float,u8,u8,u8,float,float)` @ `0x0012E010`, script commands `CommandFOG`, dungeon `main_fogRate`/`sub_fogRate` |
| Shadows | to research | `MGBeginDrawShadow`/`MGEndDrawShadow`, `CVisualShadow`, `CommandSHADOW_LEVEL` |
| Depth of field | to decide | `EdSetDOF`, `EdDrawDOF` – disabled by the no-interlacing patch; could be re-enabled at high res |


## Stage 4 - "modern" environment look (2026-10-18)
- **Post-processing**: `tools/pcsx2/resources/shaders/dx11/shadeboost.fx` replaced by a one-pass look (bloom on bright areas, clarity/local contrast as fake ambient occlusion, ACES-like filmic curve, warm highlights / cool shadows, saturation, vignette). Original saved as `shadeboost_original.fx.bak`. Enabled with `ShadeBoost = true` in the game ini; DX11/DX12 path only (the Vulkan/OpenGL `shadeboost.glsl` are untouched). PCSX2 updates may overwrite the shader file.
- **Sampling**: `filter = 1`, `TriFilter = 2`, `dithering_ps2 = 0` in the game ini (PCSX2 warns on screen that these can break rendering in some games; none seen).
- **Textures**: `tools/ai/retexture.py` runs SDXL img2img over replacement PNGs (wrap-padded for seamless tiling, LAB statistics matched to the source, alpha restored). 71 tileable materials at denoise 0.35 (subtle), the four main dungeon wall/floor textures at 0.55 with material prompts (`5bb59e95...`, `f02ac3bc...` walls, `51c3bd79...`, `59efbece...` floors). Backups of the pre-change files: `work/env_backup/` (the 4 strong ones' ESRGAN versions are in there too if they were in the 71) .
- Dungeon-only coverage measured with a replacement-free run: 22 textures are used by the first-dungeon scenes; 8 of them are 256x256 materials. Other areas (towns, other dungeons, sky) are not yet measured/retextured.
- Finding: the strong floor pass changed packed dirt into cobblestones; fine for the look but it is a different material, not just more detail.
