"""Compare the C++ linear blend skinning with the vertex buffer the running game writes (skin object of the player, PINE slot 28011).
Freezes the player's motion at an exact time first. Usage: python -I native/tools/check_skin_live.py <time> [pack] [skin object index]"""
import json, struct, subprocess, sys, time as _t
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "coop"))
from pine import Pine
T = float(sys.argv[1]) if len(sys.argv) > 1 else 15.5
pack = sys.argv[2] if len(sys.argv) > 2 else "dun/mainchara/c01d.chr"
skin_obj = int(sys.argv[3]) if len(sys.argv) > 3 else 76
CHARA = 0x01EA1D20
with Pine(28011) as p:
    M = p.read(CHARA + 0xC20); F = p.read(CHARA + 0xBC)
    p.write(CHARA + 0xC64, p.read(CHARA + 0xC64) | 1)
    ent = p.read(M + 0x64) + p.read(M + 0x24) * 0x10
    p.write(ent, 0); p.write(ent + 4, 2000); p.write_float(ent + 8, 0.0)
    for _ in range(30):
        p.write_float(M + 0x10, T); _t.sleep(0.03)
    vis = p.read(F + skin_obj * 0x270 + 0x260)
    mdt = p.read(vis + 0x20)
    n = p.read(mdt + 0xC)
    buf = p.read_bytes(mdt + p.read(mdt + 0x10), n * 16)
    live = [struct.unpack_from("<3f", buf, 16 * i) for i in range(n)]
    t_after = p.read_float(M + 0x10)
exe = ROOT / "native" / "build" / "core_release" / "dcskin.exe"
res = subprocess.run([str(exe), __import__("os").environ.get("DC_ISO", str(ROOT / "iso" / "Dark Cloud (USA).iso")), pack, repr(t_after), "skin"] + ([sys.argv[4]] if len(sys.argv) > 4 and sys.argv[4].lstrip('-').isdigit() else []), capture_output=True, text=True).stdout.strip().splitlines()
print(res[0], "| live vertices", n, "| time", t_after)
errs = []
for line in res[1:]:
    a = line.split(); i = int(a[0]); c = [float(x) for x in a[1:]]
    errs.append((max(abs(c[k] - live[i][k]) for k in range(3)), i))
errs.sort(reverse=True)
mean = sum(e for e, _ in errs) / len(errs)
print(f"max vertex error {errs[0][0]:.5f} (vertex {errs[0][1]}), mean {mean:.6f}, vertices off by more than 0.01: {sum(1 for e,_ in errs if e > 0.01)} of {len(errs)}")

if "--diff" in sys.argv:
    import statistics
    ours = {int(l.split()[0]): [float(x) for x in l.split()[1:]] for l in res[1:]}
    d = [[live[i][k] - ours[i][k] for k in range(3)] for i in range(n)]
    print("mean diff vector", [round(statistics.mean(x[k] for x in d), 5) for k in range(3)], "spread", [round(max(x[k] for x in d) - min(x[k] for x in d), 6) for k in range(3)])
