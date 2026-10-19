"""Trapped-chest smoke test (d01 floor 2, seed 4): a wrong guess at an Explosion trap hurts the player, a right guess at a Curse trap releases it, the Happy Clown gives a weapon."""
import os
import re
import subprocess
import sys

GODOT = os.environ.get("GODOT") or os.path.expandvars(r"%LOCALAPPDATA%/Microsoft/WinGet/Packages/GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe/Godot_v4.7.2-stable_win64_console.exe")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace(os.sep, "/")
A = "ddown:2,none:10,ddown:2,none:10,ddown:2,none:10,circle:2,none:20,attack:2,none:35"   # the last entry of the window
B = "ddown:2,none:10,none:12,none:12,circle:2,none:20,attack:2,none:35"                     # the second entry


def run(extra, taps, shots):
    p = subprocess.run([GODOT, "--path", ROOT + "/native/godot", "res://play.tscn", "--", "--dungeon", "0", "--floor", "1", "--seed", "4"] + extra +
                       ["--teleport", "1877.4,1035.4", "--script", taps, "--out", ROOT + "/work/trap_smoke", "--shots", shots], capture_output=True, text=True, timeout=280, cwd=ROOT)
    return p.stdout


fail = False
out = run(["--set-trap", "3,3"], "none:40,attack:2,none:20,%s,%s,%s,%s,%s,%s,%s" % (A, A, A, B, A, A, A), "520,800")
life = [int(m) for m in re.findall(r'"life": (\d+)', out)]
if "The trap was an Explosion" not in out or not life or life[-1] >= 70:
    print("FAIL: wrong guess at the Explosion trap", life)
    fail = True
out = run([], "none:40,attack:2,none:20,%s,%s,%s,%s,%s" % (A, A, A, A, A), "460,500,540")
if "Successfully released the trap" not in out:
    print("FAIL: right guess at the Curse trap")
    fail = True
out = run(["--set-trap", "3,5"], "none:40,attack:2,none:20," + ",".join(["attack:2,none:60,circle:2,none:40"] * 12), "600")
if "You got" not in out:
    print("FAIL: the Happy Clown gave nothing")
    fail = True
if fail:
    sys.exit(1)
print("trapped chests ok")
