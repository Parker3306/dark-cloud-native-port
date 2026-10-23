# Reusable pieces for the next game port / co-op retrofit

The user wants to do the same kind of project (native port + co-op) for other games. Everything below is written to be lifted out. Keep this list current.

## Workflow that worked (any PS2/console game)
1. **Asset formats first**: C++ readers verified against the running game (PCSX2 + PINE memory reads), then converters, then the engine front end. Keep the sim in an engine-independent library (`dccore`), the engine (Godot) only draws and reads input.
2. **Decompile once, index it**: Ghidra headless (`work/dec.ps1`, `scripts/ghidra/*.java`) dumps everything to one C file (`work/decomp/all_dun.c`) + `work/funcs_idx.txt` (name @ address, line number). Grep that instead of opening Ghidra.
3. **Port function by function with a lock-step rig**: patch the game's pad getters to read injected values, freeze a frame, read state over PINE, replay the same inputs in the port, compare per tick (`tools/dc/validate_*.py`, `coop/hooks.py` mailbox stub). Gives bit-exact sims.
4. **Research agents**: for each unknown subsystem spawn a read-only agent that decompiles and writes ONE notes file (`notes/<topic>.md`) with evidence levels (READ / VERIFIED-DATA / INFERRED). Brief it with file pointers; ask for a plug-in plan against the existing code. (Back floors, EB duel, character rooms were done like this.)
5. **Screenshot harness per subsystem** (`play.gd`, `town_play.gd`, `color_view.gd`): command-line flags (`--teleport`, `--script pad,ticks`, `--shots`, `--god`, `--trace`) so every feature can be checked headlessly. `work/smoke.sh` runs them all after each change.
6. **Notes + memory**: one section per milestone in `notes/native-plan.md`; per-topic notes; memory file for session hand-over.

## Code you can reuse as is
* `native/godot/coop/coop_net.gd` - ENet host/join, roster/player numbers, presence + events (game independent).
* `native/dccore/include/dccore/recolor.h` - per-player clothes colour for paletted/atlas textures (hue swap of saturated pixels; wash for pale cloth).
* `native/dccore/` building blocks: bounds-checked byte readers (`bytes.h`), ISO9660 + archive readers (`disc.*`, `pack.*`), TIM2 decode, deterministic game RNG wrappers (`gamemath.h`, `libm_ps2.*` bit-exact PS2 libm), the script VM (`script.*`) and message-bank parser.
* `scripts/` + `tools/dc/`: Ghidra scripts, PINE client (`coop/pine.py`), contact-sheet and texture dump tools (`ui_dump.py`).
* Harness conventions: `_arg()` helper, `--out prefix --shots n,n,...`, screenshots after `RenderingServer.frame_post_draw`.

## Co-op design pattern (how it was done here)
* Each peer simulates its own player (own save, own inventory). Presence (pos/yaw/motion/time/character/map) at 20 Hz drives puppets; discrete events are reliable messages.
* Host owns the shared world (monsters, chests, drops); clients render snapshots and send their attack spheres / requests; host replies with hits and results.
* One player enters a dungeon -> reliable event with floor + seed -> everyone loads the same floor.
* Player colour by player number (TMNT style) = texture recolour at model creation, no shaders needed.

## Gotchas worth remembering
* Bash heredocs mangle backslashes/quotes: write patch scripts with the file-write tool and run them.
* Never run foreground `sleep` in the harness; run Godot in the foreground of a call with a timeout, or in background and poll for the PNG.
* `build.ps1` builds the Godot extension; `build_core.ps1` rebuilds the command-line tools (they are separate binaries).

## Two-instance co-op test rig (reusable)
* `work/coop_smoke.sh`: starts a host and a guest Godot process on one PC with `--coop host|join:127.0.0.1`, scripted pads (`--script`), real-time screenshots (`--live-shots s,s`) and prints
  counters (`coop_debug()`); every co-op feature gets a `grep` check. Pattern: the game scenes accept the same flags, the harness never needs a second machine.
* Replicating a deterministic sim over the network: share the seed and the starting record, replicate only *events* (hit spheres, chest opened, door opened) plus snapshots of what only one
  side simulates (monsters). Swap-in scopes (`RemoteScope`) let existing single-player code run for another player's data without rewriting it.

## Second port: Twisted Metal: Black (`..\TwistedMetalBlack`, started 2026-10-19)
The generic layer was pulled out of dccore into **`TwistedMetalBlack/native/portkit/`** (namespace `portkit`); copy that folder into the next port:
`bytes.h`, `iso9660.*` (full ISO9660 tree, any game), `elf.*` (+ sections and `.symtab`), `inflate.*` (raw deflate + ZIP local headers with a custom magic,
no zlib needed), `gamemath.h` / `libm_ps2.*` / `vu0.h` (EE/VU0 float rules), `tim2.*`, `recolor.*` (game-specific face/wash rules removed; thresholds are
parameters). dccore still has its own copies; switch Dark Cloud to portkit when convenient.
New-project checklist that took ~1 hour there:
1. List the ISO, extract the ELF, check its section table: **`.symtab` / `.mdebug` = debug symbols** (TMB has both: 3568 names + source file per function).
   `tools/tmb/symbols.py` (generic) writes symbols.tsv incl. source files from `.mdebug`.
2. `scripts/ghidra_import.ps1` (headless import + DecompileAll + index; ~3 min with symbols) and the generic `tools/tmb/decomp_index.py`
   (`find/show/callers/strings/file`; string scan takes data ranges from the ELF).
3. `native/env.ps1` points at the shared tools (`$env:PORT_TOOLS`, default DarkCloud\tools) and the disc (`$env:TMB_ISO`) instead of copying GBs of tools;
   `native/CMakeLists.txt` falls back to DarkCloud's godot-cpp checkout.
4. Survey tool (`tmdisc survey`: per dir/extension counts, packed/unpacked size, magics), then spawn read-only research agents per subsystem
   (formats, co-op architecture, game flow, audio/video), each writing one notes file.
5. `coop_net.gd`: `max_players` is now a var (TMB = 6) and `host_game(..., bind_ip)`; `scripts/lobby_smoke.ps1` = N headless Godot peers on 127.0.0.1
   (needs to run outside the command sandbox: binding UDP is blocked inside it).

### Arithmetic fixes found in TMB (2026-10-19) - apply to Dark Cloud's dccore/gamemath.h too
* `ee_add` returned `b` when `a == 0`: `0 + -0` gave -0, the EE gives +0. Fix: zero operands -> plain `a + b`.
* `vadd`/`vsub` computed `to_float(double(a) + double(b))`: when one operand is < 2^-29 of the other the double sum drops it before the
  truncation, but the VU truncates the exact sum (4.70510817 + -1.4e-17 -> the float just below). Fix: `gm::trunc_sum` (TwoSum + exact
  truncation). Found by TMB's BSP validator (1 in 3000 queries); Dark Cloud's rigs may have hit it as "rare divergences".

### Rig lessons from TMB milestone 3 (2026-10-19)
* **Several emulators at once**: every script that stops, moves or screenshots PCSX2 must pick its own emulator by executable path, never all
  `pcsx2-qt` processes. Until 2026-10-19 Dark Cloud's `launch_pcsx2.ps1` / `env_shots.py` / `ingame_cycle.sh` / `review.sh` and TMB's
  `boot_pcsx2.ps1` / `pcsx2_offscreen.ps1` killed each other's emulators (fixed in both repos). Each project also needs its own PINE slot.
* **Deterministic per-tick lock-step** (`TwistedMetalBlack/coop/lockstep.py`): a gated game measures huge frame times, so patch the game's timer
  to a fixed step. Also make the hardware counter a pure frame clock (one field = 9619 RCNT0 ticks per frame), because animation, timers and
  rand reseeds read it. Then two replays of a pad script are bit-identical every tick. Check this with a two-replay selftest before trusting
  any per-tick comparison; Dark Cloud's "rare divergences" may have this cause.

### Live boss / floor comparison rig (Dark Cloud, 2026-10-19) - the pattern is reusable for any game with a PINE-readable state
* `tools/dc/boss_live.py` starts a given floor in the running emulator by writing the three things the engine itself writes when a floor change runs the floor-start script (save flags / ints, the status
  bytes for dungeon / floor / old floor, then "event request = 100" and "game task = 400"), then prints hero and monster slots once per poll; `--teleport` moves the hero onto an auto marker so that
  the game's own marker code starts the event. Do not request an event function that a marker would start with extra state (it stalls); walk onto the marker instead.
* It found in one afternoon what months of self-consistent tests had not: the port placed every part of the fixed maps without the cfg's PT_BASE offset (tens of units) and rotation. Lesson: when a
  port's own tests all pass, compare ONE absolute number (where the hero stands after the arrival scene) with the real game before building bots on top of it.
* Call bash scripts from Python with the Git bash path (`C:/Program Files/Git/bin/bash.exe`): plain `bash` resolves to a WSL stub here.
* **sin/cos for |x| > ~201** (TMB unit 1, 2026-10-19): portkit's `ps2_sinf`/`ps2_cosf` used an `fmod` stand-in for newlib's large-argument
  reduction and gave different bits there. TMB's portkit now ports newlib's path, quirks included. Dark Cloud's `libm_ps2.cpp` came from the same
  code, so check it before trusting large angles. Other libm facts: the game's `atan2f(+-0, +-0)` returns +0, and VU0 transposes turn -0 into +0.
* **Denormals** (TMB unit 13, 2026-10-20): the EE FPU and VU0 have no denormals; a result below FLT_MIN becomes a zero of its sign. TMB's
  portkit `gamemath.h` now flushes after every modelled op (`flush_denormal`). Dark Cloud's `dccore/gamemath.h` does not yet: port the change.
* **NaN / inf results** (TMB 2026-10-22): NaN/inf results clamp to +-FLT_MAX like PCSX2 Normal clamp (NaN -> +FLT_MAX; portkit `gm::clamp_nonfinite`, also sqrt of a negative = sqrt(|x|)); Dark Cloud's dccore gamemath.h lacks it.
* **Whole-tick lock-step recipe** (TMB milestone 3): first validate every unit per call on RAM snapshots, then let the port free-run from one
  snapshot and compare every tick with the gated game. Run the timer step BEFORE Update, hand non-ported systems' writes over from the game
  between ticks, and load the rand state at probe sites. Write each such deviation down (TMB notes/vehicle.md 11). TMB reached 48/48 runs of
  600-1000 ticks identical this way.

- Live co-op tests: add a `--X-at SECONDS` flag to the `--live-shots` loop (play.gd) rather than the tick-based bot; print the state you want to compare in the `LIVE` line and grep the guest log (`work/coop_smoke.sh`).
- Whole-floor live comparison (`tools/dc/validate_back_all.py` + `native/dccore/tools/dcback.cpp`, shared helpers `tools/dc/liverig.py`): call the game's own generator functions through the Cave mailbox one call at a time (`call_batch` with several calls is NOT guaranteed to run them in order), `srand` right before the function that draws numbers, set the globals the real caller sets (`NowDngMap`, `BtUraDongeon`) before calling functions that use them, and re-dump the status record before every trial (floor starts change it). `tools/dc/probe_preset.py` shows how to wrap a game function with a tail-jump stub that records its stack frame.
- Hunting stale-memory reads of the original: record the callee's frame in the real call chain (not through the mailbox, whose stack differs) and look for constants across seeds; pointers to known globals show up as pairs of int16 (low, high).

### Supervising background agents (TMB, 2026-10-20)
* Agent briefs end with a standard footer (TwistedMetalBlack `notes/agent_rules.md`): finish or stop all background work, run the last
  validation in the foreground, release the emulator, never start other rigs.
* `TwistedMetalBlack/scripts/stuck_check.ps1` lists a project's processes as busy / waiting on child / IDLE (from a CPU sample), and each
  rig as in use or UNUSED (from its PINE clients). `-Kill` stops idle ones by pid/path. Copy it to the next project and change only the
  root and the process patterns.

### Audio in portkit (TMB milestone 4.5, 2026-10-21)
Lifted from dccore's sound code into `TwistedMetalBlack/native/portkit` (namespace `portkit::audio`), game-independent:
* `ps2_adpcm.{h,cpp}`: `Pcm`, `adpcm_decode` (= dccore `snd::adpcm_decode`, golden-checked against TMB's vag.py / bnk.py on 166 samples
  + 3 VAG streams) and `adpcm_block` + `AdpcmState` for block-by-block streams (pause / continue keep the decoder state).
* `spu2_adsr.h` (`Spu2Env` = dccore's `Env`, unchanged), `spu2_reverb_standin.h` (dccore's `Reverb`, size / wet parameters).
* `voice_mixer.{h,cpp}`: dccore's `Engine` voice core without hsyn/libmidi: sample or `StreamSource` voices, rate, ADSR, L/R gain ramps,
  8 volume groups, priority stealing with a short fade, pause, soft limiter, stats, `write_wav_stereo`. The game's driver sits on top
  (TMB: `tmcore/sound.h` = 989snd: SBv2 banks, MIDI type-4 sounds, VAGp streams; Dark Cloud: its hsyn banks / SCEIMidi player could
  move onto it). Single-threaded contract (render from the engine's main thread into an AudioStreamGenerator; see TMB `tm_audio.cpp`).
* Pattern for "which sound plays when": port the game's sound.cpp as presentation code reading the sim image + a presentation event
  stream (TMB `tmcore/sound_game.cpp`, `present.h`), own RNG, sound handles kept outside the sim; rollback = remember sounds per event id
  and stop them on retraction.
* (TMB milestone 6, audio agent, 2026-10-22) `spu2_reverb.{h,cpp}`: the real SPU2 reverb (PCSX2 / Mednafen structure, half-rate L/R, 39-tap
  FIR resampling, integer maths) with libsd 2.1.0's own 10 presets (from LIBSD.IRX) - any PS2 game that sets sceSdSetEffectAttr modes.
  `voice_mixer`: SPU2-style 4-tap Gaussian interpolation (sigma 0.5, fitted to PCSX2 recordings), loudness-aware voice stealing, 1 ms
  look-ahead output limiter (replaces the soft clip). **Muted reference recordings of the original**: PCSX2 2.8's media capture records the
  SPU2 output BEFORE the output mute / volume - set the rig ini to audio-only WAV capture (EnableVideoCapture = false, EnableAudioCapture =
  true, CaptureContainer = wav, AudioCaptureCodec = pcm_s16le, hotkey ToggleVideoCapture = Keyboard/F11) and post F11 to the off-screen
  window (TMB `scripts/pcsx2_key.ps1`, `tools/tmb/audio_ref.py` plays isolated sounds through the game's own snd_ API via the call
  mailbox, `audio_compare.py` = level / pitch / envelope / spectral metrics vs the port's render). Dark Cloud's hsyn engine could be
  calibrated the same way. Also learned: 989snd SQUARES the voice level (x*x/0x7F00): volumes and the pan law act quadratically.

### PS2 movies (.PSS) in portkit (TMB milestone 6, movies agent, 2026-10-22)
* `portkit/pss.{h,cpp}` (+ `tests/test_pss.cpp`): streaming MPEG-2 program-stream demuxer for PS2 movies: video (stream 0xE0) as a raw MPEG-2 elementary
  stream, Sony audio (private_stream_1 substream 0xA0, "SShd"/"SSbd" header: type 1 = 16-bit PCM, type 0x10 = SPU ADPCM via `adpcm_block`) as de-interleaved
  PCM, `extract(Iso9660, file, m2v, wav)` writes both from a disc image in 4 MB reads. FFmpeg does not recognise this audio, so any PS2 game with PSS
  needs it. TMB's movies are PCM (68 of 282 have audio), not ADPCM.
* Pipeline used by TMB (`native/godot/movies/`): demux -> `ffmpeg -f mpegvideo -i X.m2v -i X.wav -c:v libtheora -q:v 8 -c:a libvorbis -q:a 4 -f ogg` ->
  Godot's `VideoStreamTheora` (SAR 46:45 is kept, display 16:9). FFmpeg is only RUN (separate process), never linked or bundled by default: the common
  Windows build (gyan.dev full) is GPL; only an LGPL build may ship with a package. `OS.execute_with_pipe` must be non-blocking on Windows (a blocking
  read never saw EOF). The GDScript classes (MovieStore / MovieConverter / MoviePlayer / MovieMenu) are generic apart from `MovieCatalog`.

- **portkit** (`portkit/`, read `portkit/PORTKIT.md`): game-independent local tooling that saves model usage: `portkit check` (all regression / live-comparison checks from `portkit.toml`, one line each, excerpts only for failures, retries and emulator reset hooks, history of new / fixed failures) and `portkit shots` (screenshot baselines: captures, compares, reports only changed pictures with a before | after | diff image). `portkit init <dir>` sets it up in a new game repository (copies the kit, manifest template, CLAUDE.md section). Self test: `python portkit/selftest.py`.

## portkit (workflow tools, 2026-10-22)
`portkit/` (Python, standard library; copy with `python portkit/portkit.py init <repo>`): check, shots, status, find, logs, emu, multi, replay, decomp, live, bisect, assets (manual: `portkit/PORTKIT.md`,
test: `python portkit/selftest.py`). Game-side pieces: `native/godot/` has `pad_file.gd` (record / replay of controller input, PK_RECORD_PAD + --pad-file) and `native/gdext/src/dc_pad.h` (the C++ version).
