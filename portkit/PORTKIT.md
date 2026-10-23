# portkit: how to use it (read this first in a new chat or a new game project)

portkit is a small, game-independent toolbox (Python standard library; numpy / Pillow are used when installed, never required) that does the repetitive verification work locally so that a model
does not have to run scripts one by one, read long logs, or look at screenshots that did not change. It was built during the Dark Cloud native port and works for any port that has
(a) commands that exit 0 / print recognizable lines when something works and (b) commands that write screenshots.

Location: `portkit/` in the repository (copy it to a new game with `python portkit/portkit.py init <new repo dir>`). Entry point: `python portkit/portkit.py <check|shots|init>`.
Self test of the kit: `python portkit/selftest.py` (about 10 s; `PORTKIT_PURE=1` forces the no-numpy / no-Pillow code paths).

## Rules for the model (these are what saves usage)
0. **At the start of a chat run `python portkit/portkit.py status`**, and use `find` / `decomp` / `logs` (section "The other tools") instead of reading notes, dumps and logs by hand.
1. **Run `portkit check` instead of the single test scripts.** One line per check; only failures print an excerpt (20 lines). Open `.portkit/runs/<stamp>/<name>.log` only for a failing check and only the part you need.
2. **Run `portkit shots` instead of viewing screenshots.** It prints only the pictures that CHANGED, NEW or are MISSING and writes one `before | after | diff` image per change into `.portkit/shots/report/`.
   Look at those images, never at unchanged captures. Accept an intended change with `shots accept <name>` (or `--new` for first captures); never accept to make a failure go away.
3. After a code change run `check --tags fast` (seconds to a minute), before finishing run the whole `check`; run `--tags live` / `slow` only when the change touches what they cover.
4. Add a check or a shot for every bug you fix, in `portkit.toml`, so that it cannot come back unnoticed. Prefer making the underlying script print `ok:` / `FAIL:` lines than writing a parser.
5. Do not start long loops yourself (polling, sleeping). Run `check` (it has timeouts, retries and kills the process tree of a timed-out command) and read the summary.
6. Never kill processes by name (other projects' jobs may run on the machine); kill only PIDs you started. portkit already kills only the tree of its own timed-out command.

## Commands
```
python portkit/portkit.py check                         all checks, in manifest order
python portkit/portkit.py check smoke sweep             only these
python portkit/portkit.py check --tags fast,build       checks with one of these tags
python portkit/portkit.py check --failed                only what failed in the last run
python portkit/portkit.py check --list                  names, tags, descriptions (no run)
python portkit/portkit.py check --stop-on-fail --excerpt 40 --json
python portkit/portkit.py shots                         capture every shot, compare with baselines, report changes   (= `shots all`)
python portkit/portkit.py shots run|compare|list        capture only / compare what is already captured (--no-run) / list
python portkit/portkit.py shots accept --new            baselines for shots that have none;  `accept name1 name2` or `accept --all`
python portkit/portkit.py shots --tags hero             subset (also: names)
python portkit/portkit.py init <dir>                    set up portkit in a new game repository (copies the kit, writes portkit.toml + a CLAUDE.md section, adds .portkit/ to .gitignore)
```
Exit codes: 0 all fine (NEW shots and SKIPPED checks are not failures), 1 something failed, 2 no manifest. The manifest is `portkit.toml`, searched upward from the current directory (`--manifest FILE`).
Output example:
```
portkit check: 3 check(s), logs in .portkit\runs\20261022-195442
 PASS  smoke        20.2s  10 ok
 PASS  sweep       147.6s  problems: 0
 FAIL  coop_smoke   92.1s  9 ok  (NEW)
summary: 2 pass, 1 fail
--- coop_smoke: matched fail_regex: FAIL: floor exit together (full log: .portkit\runs\...\coop_smoke.log)
    <excerpt>
```
`(NEW)` = it passed in the previous run, `(fixed)` = it failed before, `[2 attempts]` = it needed a retry.

## The manifest (`portkit.toml`, TOML; a commented template is `portkit/templates/portkit.toml`)
```toml
[project]
name  = "Game port"
root  = "."                              # commands run from here (relative to the manifest)
shell = ["C:/Program Files/Git/bin/bash.exe", "-c"]   # how a command STRING runs (list: paths with spaces). Default: cmd /c on Windows, bash -c elsewhere.
[project.env]                            # environment of every command
GODOT = "C:/path/Godot_console.exe"
[defaults]
timeout = 900

[[check]]
name = "smoke"                  # required, unique
cmd  = "bash work/smoke.sh"     # string (run by `shell`) or list (run directly)
desc = "..."                    # shown by --list
tags = ["fast", "game"]
cwd = "native"                  # default: root
timeout = 600
env = { KEY = "value" }
expect_exit = [0]               # default [0]
pass_regex  = ["100% tests passed"]     # every one must match the log (regexes are multi-line: ^ and $ match at line ends)
fail_regex  = ["^FAIL", "error:"]       # any match fails the check (even with exit code 0)
count_regex = "^ok:"  count_min = 10    # at least that many matching lines; the summary shows the count (count_label = "ok")
summary_regex = "(tests passed.*)"      # the LAST match is shown in the summary line (group 1 if present)
retries = 2                             # more attempts when it fails ...
retry_regex = "TimeoutError|Connection reset"   # ... and the log matches this (omit: retry any failure; a timeout always retries)
before_retry = "bash tools/reset_emulator.sh"   # runs between attempts
prep = "..."                            # runs before every attempt
requires = ["iso/game.iso"]             # file missing -> SKIP (no failure)
group = "emulator"                      # optional, see [[group]]

[[group]]                               # shared setup (once before the first member) / teardown (after the last member)
name = "emulator"  setup = "bash tools/start.sh"  teardown = "bash tools/stop.sh"

[shots]
threshold = 8                           # per-channel difference for a pixel to count as changed
max_changed_pct = 0.3                   # a shot is CHANGED above this share of changed pixels
# dir = ".portkit/shots/current"  baseline = ".portkit/shots/baseline"   (defaults; the baselines are game renders: do NOT commit them)

[[shot]]
name = "town_run"
cmd  = "\"$GODOT\" --path game res://scene.tscn -- --out {dir}/{name} --shots 60"   # placeholders: {out} {dir} {name} {root}; forward slashes are substituted
out  = "{dir}/{name}_t60.png"           # the file the command must have written (default {dir}/{name}.png)
masks = [[x, y, w, h]]                  # regions to ignore (clock, random particles, counters)
max_changed_pct = 1.5                   # per-shot limit (animation that follows real time needs a looser one)
tags = ["hero"]  timeout = 180  cwd = "..."
```
Logs of commands are written to FILES (never pipes: a command that leaves an emulator running would hang a pipe).

## The other tools (all local; each saves reading, sleeping or re-doing by hand)
```
python portkit/portkit.py status                         start of a chat: git state, last check / shot results, open TODO items, pickup note, emulator (~20 lines)
python portkit/portkit.py find lock-on boss [--code]     best snippets of the notes (and code) containing ALL terms: use it BEFORE opening a notes file
python portkit/portkit.py logs FILE [--errors] [--tail N] [--grep RE]   a long log in a few lines: noise dropped ([logfilter]), repeated lines counted, errors grouped; no manifest needed
python portkit/portkit.py emu status|start|stop|reset|ensure|load-state N|save-state N   the project's emulator (found by exe PATH, never by name); `ensure` repairs a crashed one
python portkit/portkit.py multi list | run NAME [--interactive] | stop | logs NAME [ROLE]   several program instances together (co-op host + guests); non-interactive = a check
python portkit/portkit.py replay list | record NAME | run [NAMES] | accept NAME | info NAME   a human plays once, the input is replayed headless forever (see below)
python portkit/portkit.py decomp index | dossier FUNC | callers FUNC | callees FUNC | search REGEX | list [PATTERN] | dump   a decompiler dump without reading it
python portkit/portkit.py live list | run NAME [--trials N] [--seed S]   random-trial comparison port <-> running original (you write a small module)
python portkit/portkit.py bisect CHECK --good REV [--bad REV]   the commit that broke a check, in a throw-away git worktree (your tree is untouched)
python portkit/portkit.py assets iso FILE [--find RE] [--extract DIR] | inventory DIR | sheet PATH... --out F [--tim2]   disc listing/extraction, folder inventory, texture contact sheet
```
Habits: `status` first; `find` / `decomp dossier` instead of grepping and reading; `logs` instead of `cat`/`tail` on a log; `emu ensure` as `prep`/`before_retry` of live checks; `multi run` instead
of launching windows by hand (`--interactive` leaves them open for the user); `bisect` when a check that passed last week fails now; `live` modules for new comparison rigs.

### Manifest sections of these tools (all optional; the `[emu]`, `[decomp]`, `[[multi]]`, `[[replay]]`, `[[live]]` sections of the Dark Cloud `portkit.toml` are working examples)
```
[status]   todo = "TODO.md" (lines "- [ ] item"; "## heading" groups)   pickup = "notes/pickup.md" (first lines shown)   max_items = 8
[find]     notes = ["notes", "README.md"]   code = ["src", "tools"]   (paths searched; --code adds the code ones)
[logfilter] drop = ['^Vulkan', ...]        regexes of lines that are noise (used by `logs` and by the excerpts of failing checks); use single quotes for backslashes
[emu]      exe, iso, args, cwd, pine_port, state_slot, boot_timeout, settle_seconds, minimized, drop_path (PATH entries removed for the emulator only), ready_cmd (exit 0 = game usable)
[decomp]   source = "dump.c" (functions start with "// ==== NAME @ ADDR"; header_regex changes that)   symbols = ["symbols.csv"]   dump_cmd
[bisect]   link = ["iso", "data"] (untracked dirs shared with the worktree)   prep = "build command run at every step"
[replays]  dir, env_var = "PK_RECORD_PAD"        [[replay]] name, record_cmd, cmd (with {input}, {dir}, {name}), out (screenshot case) or pass_regex/fail_regex (check case), timeout, tags
[multi_layout] screen = [1920, 1080]  top = 40   [[multi]] name, timeout, start_gap, done_regex, expect = [{role, regex, count}], forbid = [...]; [[multi.instance]] role, cmd, env
           placeholders in cmd: {role} {index} {n} {x} {y} {w} {h} {log} {dir}
[[live]]   name, module, trials, seed, connect = "pine" | "none", tol, allow_mismatch, retries      module functions: trial(ctx, i, rng) -> {live, port, params} (+ setup/rerun/shrink/teardown/make_cave)
```

### Replay: what the game must do (the recorder / player is ~60 lines; Godot version `portkit/godot/pad_file.gd`, C++ version `dc_pad.h` in the Dark Cloud gdext)
* Recording: when the environment variable named by `[replays] env_var` is set, append one text line per game tick to that file: `lx ly rx ry buttons menu` (integers: sticks 0..255 with 128 centre,
  `buttons` = the PS2 libpad word with 1 = pressed: L2 1, R2 2, L1 4, R1 8, Tri 0x10, Circle 0x20, Cross 0x40, Square 0x80, Select 0x100, L3 0x200, R3 0x400, Start 0x800, Up 0x1000, Right 0x2000, Down 0x4000, Left 0x8000).
* Replay: a `--pad-file PATH` argument makes the game read its input from those lines instead of the devices, one line per tick, then idle. Use a FIXED seed and tick count so the end state is repeatable.
* Case kinds: with `out` it is a screenshot comparison (accept the first result with `replay accept NAME`), without it a log check. Recordings are tiny and hold no game data: they may be committed.

### Pitfalls of these tools
* Emulator PIDs are found by executable PATH; two projects may run two emulators at once, so never `Stop-Process -Name`.
* `emu reset` loads state slot `state_slot` and waits for `ready_cmd`; a hung emulator is fixed by `emu ensure`, not by waiting.
* `multi` kills only the instances it started; give the host `start_gap` seconds before the guests.
* `live` modules must call game functions one at a time through the mailbox (the code cave); a trial that raises a connection error is retried after `emu ensure`.
* Windows: write regexes with single-quoted TOML strings; paths in commands use forward slashes; shell is Git bash (plain `bash` can be the WSL stub).
* `find` needs every term to appear within a few lines; broaden with fewer terms if nothing comes back.

## Making a port testable with portkit (what to do in a new game)
1. `python portkit/portkit.py init <repo>` and edit `portkit.toml`.
2. Give every test script a clear verdict: exit code 0 only on success, lines starting `ok:` / `FAIL:` (or one summary line such as `problems: 0`). Then a check is 4 lines of TOML.
3. Make screenshot scenes deterministic: fixed seed, fixed tick/frame count (not wall-clock waits), no random camera; expose `--out` and a frame number on the command line. If something animates by
   real time, give that shot a looser `max_changed_pct` or a mask, and say why in a comment.
4. Tag checks `fast` (seconds to a minute: build + unit + smoke), `slow` (minutes), `live` (needs an emulator / the original game running: use `requires`, `retries`, `before_retry` for crashes).
5. First baselines: `shots` once, look at the captures ONCE (or trust them if the checks around them pass), then `shots accept --new`.
6. Keep `portkit.toml` current: a new feature gets a check and, when it is visual, a shot.

## Other reusable tooling in the Dark Cloud repository (see `notes/reusable.md` for the full list and the lessons)
- `coop/pine.py` (PINE socket to PCSX2: read / write memory, save states), `coop/hooks.py` (a code cave + mailbox to call game functions from outside; call them ONE AT A TIME, a batch is not always run in order),
  `coop/mips.py` (tiny MIPS assembler), `tools/dc/liverig.py`, `tools/dc/validate_*.py` (compare the port with the running game: the pattern for any new game).
- `scripts/ghidra/*` (headless decompile / dump), `tools/dc/pak_ls.py`, `dcdisasm` (script disassembler) as examples of per-format tools.
- Native port layout: a standalone C++ library (`native/dccore`) + a Godot front end + command-line tools per subsystem; the live-comparison tools call the same C++ code.

## Pitfalls that cost time (generic)
- Windows: plain `bash` may be the WSL stub; use the full Git bash path in `shell`. Heredocs through a shell can eat backslashes (`\n` in C++ strings): write source files with the editor tool or a script file.
- Emulators crash after many save-state loads: use `retries` + `before_retry` (reset), do not loop by hand.
- An emulator that is launched by a script inherits its output handle: always redirect to a file (portkit does).
- Screenshots of animated scenes differ slightly between runs when the animation follows real time: judge with `max_changed_pct`, not with equality.
- Do not commit game data or renders (ISO, extracted files, screenshots / baselines): `.portkit/` is in `.gitignore`.
