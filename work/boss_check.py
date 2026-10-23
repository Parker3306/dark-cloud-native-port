"""Boss check: every boss floor, a bot fights it (Toan; Ruby with the lock-on for the ranged ones; Ungaga with the attack button held for the d04 boss that only his blast reveals),
all monsters at 1 hp so that only reachability and the hit rules decide.
Reports whether the fight ended with 'LEFT cleared' within the tick budget, where the hero stood after the intro and how close the bot got.
The keys of the boss doors are used for real where the bot knows the way (--bot-markers: d04 moon / sun signets); the others get their flags set (d01: the quick item, a bomb, takes the use button, so the horned key door is skipped).
usage: python -I work/boss_check.py [ticks] [d01 d02 ...]"""
import os
import re
import subprocess
import sys

GODOT = os.environ.get("GODOT") or os.path.expandvars(r"%LOCALAPPDATA%/Microsoft/WinGet/Packages/GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe/Godot_v4.7.2-stable_win64_console.exe")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace(os.sep, "/")
TOAN = ("Toan", 0, 24, [])
RUBY = ("Ruby", 3, 250, ["--bot-lock", "1"])
UNGAGA = ("Ungaga", 4, 40, ["--bot-hold-ok", "1"])
XIAO = ("Xiao", 1, 250, ["--bot-lock", "1"])
GORO = ("Goro", 2, 24, [])
OSMOND = ("Osmond", 5, 250, ["--bot-lock", "1"])
OTHERS = [XIAO, GORO, OSMOND]   # (python work/boss_check.py 12000 --others: the three characters the first table leaves out)
# Toan throwing bombs (id 159 = 0x9f) with the lock-on held: what hurts a boss that hovers out of the sword's reach (Dran)
TOAN_BOMBS = ("Toan+bombs", 0, 250, ["--bot-lock", "1", "--bot-throw", "1", "--give", ",".join(["159"] * 20), "--quick", "159"])
BOSSES = [("d01", 0, 14, ["--bot-markers", "500", "--game-flag", "24,32"], [TOAN, TOAN_BOMBS, RUBY]),
          ("d02", 1, 16, [], [TOAN, RUBY]),
          ("d03", 2, 17, ["--game-int", "0,300"], [TOAN, TOAN_BOMBS, RUBY]),
          ("d04", 3, 17, ["--bot-markers", "401,400", "--give", "210,211"], [TOAN, UNGAGA]),
          ("d05", 4, 14, ["--bot-markers", "404,405", "--give", "212"], [TOAN, RUBY]),   # (c16a takes 0 from Xiao / Ruby and 10 % from Osmond on every body part: only the melee characters hurt it)
          ("d06 stage 1", 5, 24, [], [TOAN, TOAN_BOMBS, RUBY]),
          ("d06 stage 2", 5, 25, ["--game-int", "10,2", "--game-flag", "41"], [TOAN, RUBY]),
          ("d07", 6, 99, [], [TOAN, RUBY])]
ticks = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].isdigit() else "8000"
others = "--others" in sys.argv
sel = [a for a in sys.argv[1:] if not a.isdigit() and not a.startswith("--")]
for name, d, f, extra, chars in BOSSES:
    if sel and name.split()[0] not in sel and name not in sel:
        continue
    for label, ch, rng, cextra in (OTHERS if others else chars):
        args = [GODOT, "--path", ROOT + "/native/godot", "res://play.tscn", "--", "--dungeon", str(d), "--floor", str(f), "--seed", "7", "--join", "1,2,3,4,5", "--character", str(ch),
                "--god", "1", "--bot", "1", "--bot-range", str(rng), "--boss-hp", "1", "--boss-hp-all", "1", "--boss-hp-keep", "1", "--script", "none:1", "--event-end", "1"] + cextra + extra + \
               ["--out", ROOT + "/work/bosscheck", "--shots", ticks]
        try:
            p = subprocess.run(args, capture_output=True, text=True, timeout=900, cwd=ROOT)
        except subprocess.TimeoutExpired:
            print("%-12s %s: TIMEOUT" % (name, label))
            continue
        out = p.stdout + p.stderr
        cleared = re.search(r"LEFT cleared (\{[^}]*\})", out)
        end = re.search(r"EVENTEND \{ \"pos\": \(([^)]*)\)", out)
        err = [l for l in out.splitlines() if "SCRIPT ERROR" in l or l.startswith("ERROR")]
        print("%-12s %-7s: %s  intro ends at %s %s" % (name, label, "CLEARED " + cleared.group(1) if cleared else "not cleared", end.group(1) if end else "-", err[:1]))
        sys.stdout.flush()
