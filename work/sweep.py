"""Floor sweep: every dungeon, a spread of floors, 300 ticks each with a walking pad; reports script errors, build failures and floors that did not start."""
import os
import re
import subprocess
import sys

GODOT = os.environ.get("GODOT") or os.path.expandvars(r"%LOCALAPPDATA%/Microsoft/WinGet/Packages/GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe/Godot_v4.7.2-stable_win64_console.exe")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace(os.sep, "/")
floors = {0: [0, 1, 2, 3, 4, 6, 8, 10, 13, 14], 1: [0, 1, 4, 7, 8, 9, 11, 14, 15, 16], 2: [0, 1, 5, 8, 9, 12, 16, 17], 3: [0, 1, 5, 8, 9, 12, 16, 17],
          4: [0, 1, 4, 7, 9, 12, 14, 15], 5: [0, 1, 5, 10, 15, 20, 23, 24, 25], 6: [0, 1, 10, 30, 60, 98, 99, 100]}
sel = [int(a) for a in sys.argv[1:]] or list(range(7))
bad = 0
for d in sel:
    for f in floors[d]:
        try:
            p = subprocess.run([GODOT, "--path", ROOT + "/native/godot", "res://play.tscn", "--", "--dungeon", str(d), "--floor", str(f), "--seed", "7", "--god", "1",
                                "--script", "up:60,right:60,up:60,left:60,attack:3,down:60", "--out", ROOT + "/work/sweep", "--shots", "120,360"],
                               capture_output=True, text=True, timeout=200, cwd=ROOT)
        except subprocess.TimeoutExpired:
            print("d%02d floor %d: TIMEOUT" % (d + 1, f))
            bad += 1
            continue
        out = p.stdout + p.stderr
        errs = [l for l in out.splitlines() if "SCRIPT ERROR" in l or l.startswith("ERROR") or "BUILD_FAILED" in l or "Parse Error" in l]
        started = "STARTED" in out
        shots = len(re.findall(r"^SHOT ", out, re.M))
        status = "ok" if started and shots == 2 and not errs else "PROBLEM"
        if status != "ok":
            bad += 1
            print("d%02d floor %d: %s started=%s shots=%d %s" % (d + 1, f, status, started, shots, errs[:2]))
        else:
            print("d%02d floor %d: ok" % (d + 1, f))
print("problems:", bad)
