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
