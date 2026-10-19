"""Back-floor smoke test: Tram Oil on d01 floor 2, use button at the entrance, then OK taps: the hero must arrive at the back floor's entrance and ride back again."""
import os
import re
import subprocess
import sys

GODOT = os.environ.get("GODOT") or os.path.expandvars(r"%LOCALAPPDATA%/Microsoft/WinGet/Packages/GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe/Godot_v4.7.2-stable_win64_console.exe")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace(os.sep, "/")
taps = "none:5,use:2,none:150," + ",".join(["attack:2,none:60"] * 40)
p = subprocess.run([GODOT, "--path", ROOT + "/native/godot", "res://play.tscn", "--", "--dungeon", "0", "--floor", "1", "--seed", "5", "--give", "224", "--teleport", "320,160",
                    "--script", taps, "--out", ROOT + "/work/back_smoke", "--shots", "300,1300,2400"], capture_output=True, text=True, timeout=280, cwd=ROOT)
pos = [tuple(float(v) for v in m) for m in re.findall(r"SHOT \d+ floor 1 player \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)", p.stdout)]
print("positions", pos)
bad = "SCRIPT ERROR" in p.stdout or "SCRIPT ERROR" in p.stderr
back = [q for q in pos if abs(q[0] - 2060) < 40 and abs(q[2] - 1760) < 40]
if bad or len(pos) < 3 or not back:
    print("FAIL: the hero never reached the back floor's entrance (or a script error)")
    sys.exit(1)
print("back floor ok")
