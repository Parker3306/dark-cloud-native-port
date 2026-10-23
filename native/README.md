# Dark Cloud native port (work in progress)

Layout
- `dccore/`  engine-independent C++20 library: original file formats (pack, MDS, TIM2/IMG, WGT, ...), later game rules. Unit tests run against the user's own extracted `data/` folder (never committed, never shipped).
- `godot/`   Godot 4 project that renders, plays sound, takes input and hosts the UI; talks to dccore through a thin GDExtension.
- `tools/`   one-off converters (Python from `tools/dc` is the reference implementation of every format until its C++ twin passes the same tests).

Rules
1. No Square Enix assets are ever copied into this repository or a release. The game reads the player's own disc image on first run.
2. dccore must compile and test without Godot installed.
3. Every format reader in C++ is checked byte-for-byte against the existing Python reader before it replaces it.

Play (after `powershell -File nativeuild.ps1`; Godot paths come from `native\env.ps1`)
- `powershell -File native\play.ps1` starts the game: title screen (New Game / Continue / Co-op), the village, the dungeons. It reads your disc image `iso\Dark Cloud (USA).iso` (`--iso <path>` for another one).
- Keys: WASD / arrows move, Space / Enter / F = action (talk, open, attack), X = lock on / cancel, Z = use the quick item, `[` `]` = pick the quick item, T = triangle (the duels), R = camera behind, Q / E = camera left / right (in a co-op town they change the character), IJKL = right stick, Tab / Esc = menu, F11 = full screen. A gamepad works too.
- Co-op (up to 4 players, notes/coop_native.md): one player presses Host on the Co-op page, the others Join with his address (port 8211 UDP; the UPnP button opens it on the router for 6 hours, never automatically). Each player gets a colour; in a dungeon `[` `]` change the character, D in the item list drops an item for the others.
- The game volume starts at 5 % (`godot/launch_volume.gd`).

Test harnesses (scripted, for screenshots and regression runs): `work/smoke.sh` (command line tools + short Godot runs + the back floor and trapped chest scenarios), `work/coop_smoke.sh` (two Godot instances on this PC). Flags are listed at the top of `godot/play.gd` (dungeon), `town_play.gd` (town) and `game.gd` (shell).

Older viewers
- Characters: `godot --path native\godot res://main.tscn -- --game dun/mainchara/c01d.chr --motions 0,1,11 --out prefix`
- A generated floor: `godot --path native\godot res://floor_view.tscn -- --cfg d01main_a --seed 8097 --rooms 6 --views overview,start --out prefix`
- Walk a floor with the game's own movement and camera: `godot --path native\godot res://walk_view.tscn -- --cfg d01main_a --seed 8097 --rooms 6`

dccore command line tools (built by `native\build_core.ps1` into `native\build\core_release`): `dcgolden` (digest of every reader, compared with Python by `native\tools\run_golden.ps1`),
`dcdungeon` (floor layout for a seed), `dcmpd` (what the readers make of a pack), `dccoll` (collision probes), `dcmove` (random MoveCheck cases), `dcwalk` (headless walking).
Live-game checks against PCSX2 (need `tools\dc\reset_game.sh` first): `tools/dc/validate_gen.py`, `validate_polys.py`, `validate_move.py`, `validate_walk.py`.
