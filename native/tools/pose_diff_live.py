"""Like check_pose_live.py but lists every joint whose local matrix differs from the live game."""
import json, struct, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "coop"))
from pine import Pine
pack = sys.argv[1] if len(sys.argv) > 1 else "dun/mainchara/c01d.chr"
iso = __import__("os").environ.get("DC_ISO", str(ROOT / "iso" / "Dark Cloud (USA).iso"))
modeljson = sys.argv[2] if len(sys.argv) > 2 else str(ROOT / "work/models/toan/model.json")
CHARA = 0x01EA1D20
FORCE_TIME = float(sys.argv[3]) if len(sys.argv) > 3 else None
import time as _t
with Pine(28011) as p:
    M = p.read(CHARA + 0xC20); F = p.read(CHARA + 0xBC)
    if FORCE_TIME is not None:                  # freeze the motion (flag bit 0 of CCharacter+0xC64 = "stop") and put it at an exact time
        p.write(CHARA + 0xC64, p.read(CHARA + 0xC64) | 1)
        ent = p.read(M + 0x64) + p.read(M + 0x24) * 0x10      # key table entry of the playing motion: open its frame range so the game does not wrap the time
        p.write(ent, 0); p.write(ent + 4, 2000); p.write_float(ent + 8, 0.0)
        for _ in range(30):
            p.write_float(M + 0x10, FORCE_TIME)
            _t.sleep(0.03)
    for _ in range(20):
        t0 = p.read_float(M + 0x10)
        live = [struct.unpack("<16f", p.read_bytes(F + i * 0x270 + 0x1D0, 64)) for i in range(len(json.load(open(modeljson))['objects']))]
        if t0 == p.read_float(M + 0x10): break
exe = ROOT / "native" / "build" / "core_release" / "dcpose.exe"
res = subprocess.run([str(exe), iso, pack, repr(t0)], capture_output=True, text=True).stdout.strip().split("\n")
names = json.load(open(modeljson))["objects"]
print("time", t0)
for line in res[1:]:
    parts = line.split(); i = int(parts[0]); vals = [float(x) for x in parts[1:]]
    e = max(abs(a - b) for a, b in zip(vals[16:32], live[i]))
    if e > float(sys.argv[4]) if len(sys.argv) > 4 else e > 1e-3: print(i, names[i]["name"], "parent", names[i]["parent"], "err", round(e, 3))
