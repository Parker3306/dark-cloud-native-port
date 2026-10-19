"""Rotate the player to several yaw offsets (motion forced to idle) and screenshot each. Usage: yaw_shots.py <pid> <out prefix> <deg> [deg ...]"""
import math, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "coop"))
from pine import Pine  # noqa: E402
import memmap as MM  # noqa: E402
pid, prefix = sys.argv[1], sys.argv[2]
with Pine(28011) as p:
    base = p.read_float(MM.CHARA_MAIN + MM.CHARA_YAW) + math.pi
    for d in sys.argv[3:]:
        t = base + math.radians(float(d))
        t = (t + math.pi) % (2 * math.pi) - math.pi
        end = time.time() + 1.6; shot = False
        while time.time() < end:
            p.write_float(MM.CHARA_MAIN + MM.CHARA_YAW, t)
            p.write(MM.CHARA_MAIN + MM.CHARA_MOTION, 0)
            if not shot and time.time() > end - 0.8:
                subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "scripts" / "shot_pcsx2.ps1"),
                                "-Out", f"{prefix}_y{d}.png", "-ProcId", pid], capture_output=True); shot = True
            time.sleep(0.003)
