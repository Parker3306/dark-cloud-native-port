"""Boss check: every boss floor, a bot fights it (Toan; Ruby with the lock-on for the ranged ones), all monsters at 1 hp so that only reachability and the hit rules decide.
Reports whether the fight ended with 'LEFT cleared' within the tick budget, where the hero stood after the intro and how close the bot got."""
import os
import re
import subprocess
import sys

GODOT = os.environ.get("GODOT") or os.path.expandvars(r"%LOCALAPPDATA%/Microsoft/WinGet/Packages/GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe/Godot_v4.7.2-stable_win64_console.exe")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace(os.sep, "/")
BOSSES = [("d01", 0, 14, ["--game-flag", "24"]), ("d02", 1, 16, []), ("d03", 2, 17, ["--game-int", "0,300"]), ("d04", 3, 17, ["--game-flag", "26,27"]), ("d05", 4, 14, ["--game-flag", "29"]),
          ("d06 stage 1", 5, 24, []), ("d06 stage 2", 5, 25, ["--game-int", "10,2", "--game-flag", "41"]), ("d07", 6, 99, [])]
ticks = sys.argv[1] if len(sys.argv) > 1 else "8000"
sel = sys.argv[2:]
for name, d, f, extra in BOSSES:
    if sel and name.split()[0] not in sel and name not in sel:
        continue
    for ch, rng in ((0, 24), (3, 250)):
        args = [GODOT, "--path", ROOT + "/native/godot", "res://play.tscn", "--", "--dungeon", str(d), "--floor", str(f), "--seed", "7", "--join", "1,2,3,4,5", "--character", str(ch),
                "--god", "1", "--bot", "1", "--bot-range", str(rng), "--bot-lock", "1", "--boss-hp", "1", "--boss-hp-all", "1", "--boss-hp-keep", "1", "--script", "none:1", "--event-end", "1"] + extra + \
               ["--out", ROOT + "/work/bosscheck", "--shots", ticks]
        try:
            p = subprocess.run(args, capture_output=True, text=True, timeout=500, cwd=ROOT)
        except subprocess.TimeoutExpired:
            print("%-12s %s: TIMEOUT" % (name, "Toan" if ch == 0 else "Ruby"))
            continue
        out = p.stdout + p.stderr
        cleared = re.search(r"LEFT cleared (\{[^}]*\})", out)
        end = re.search(r"EVENTEND \{ \"pos\": \(([^)]*)\)", out)
        err = [l for l in out.splitlines() if "SCRIPT ERROR" in l or l.startswith("ERROR")]
        print("%-12s %-4s: %s  intro ends at %s %s" % (name, "Toan" if ch == 0 else "Ruby", "CLEARED " + cleared.group(1) if cleared else "not cleared", end.group(1) if end else "-", err[:1]))
