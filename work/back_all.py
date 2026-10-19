import os
import re
import subprocess
import sys

GODOT = os.environ.get("GODOT") or os.path.expandvars(r"%LOCALAPPDATA%/Microsoft/WinGet/Packages/GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe/Godot_v4.7.2-stable_win64_console.exe")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace(os.sep, "/")


def run(args, timeout=280):
    p = subprocess.run([GODOT, "--path", ROOT + "/native/godot", "res://play.tscn", "--"] + args, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
    return p.stdout + p.stderr


dungeons = [int(a) for a in sys.argv[1:]] or list(range(7))
flag_index = {0: 1}
for d in dungeons:
    floor = 1
    out = ""
    for floor in (1, 2, 3):
        out = run(["--dungeon", str(d), "--floor", str(floor), "--seed", "5", "--events", "1", "--out", ROOT + "/work/back_all", "--shots", "1"])
        m = re.search(r'MARKER \{ "name": "[^"]*", "event": 200, "pos": \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)', out)
        if m and "kind 1 " in out:
            break
    if not m:
        print("d%02d: no marker 200 on floors 1..3" % (d + 1))
        continue
    out0 = out
    x, y, z = m.groups()
    print("d%02d floor %d marker %s,%s" % (d + 1, floor, x, z))
    taps = "none:5,use:2,none:150," + ",".join(["attack:2,none:60"] * 50)
    keys = {0: 224, 1: 225, 2: 226, 3: 228, 4: 229, 5: 230, 6: 231}
    extra = []
    if d == 4:   # d05: the plank (200) only unlocks; the ledge (201) holds the scene
        m2 = re.search(r'MARKER \{ "name": "[^"]*", "event": 201, "pos": \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)', out0)
        if m2:
            extra = ["--teleport-at", "400", "--teleport-to", "%s,%s" % (m2.group(1), m2.group(3))]
    out = run(["--dungeon", str(d), "--floor", str(floor), "--seed", "5", "--give", str(keys[d]), "--teleport", "%s,%s" % (x, z),
               "--script", taps, "--out", ROOT + "/work/back_all_%d" % d, "--shots", "400,900,1500,2200,3000"] + extra)
    for line in out.splitlines():
        if line.startswith("SHOT") or "ERROR" in line or "SCRIPT" in line:
            print("   ", re.sub(r"status \{.*", "", line)[:120])
        if "EVENT RUNNING" in line:
            t = re.search(r'"text": "([^"]*)"', line)
            print("      event", t.group(1)[:50] if t else "(no window)")
