"""Force the player's motion id (CharaMain+0xC68) repeatedly and screenshot the PCSX2 window, to look at a model in several animations.
Usage: python -I tools/dc/pose_shots.py <pine_slot> <pcsx2 pid> <out prefix> <motion> [motion ...]"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "coop"))
from pine import Pine  # noqa: E402
import memmap as MM  # noqa: E402

slot, pid, prefix = int(sys.argv[1]), sys.argv[2], sys.argv[3]
with Pine(slot) as p:
    for m in sys.argv[4:]:
        end = time.time() + 1.6
        shot = False
        while time.time() < end:
            p.write(MM.CHARA_MAIN + MM.CHARA_MOTION, int(m))
            if not shot and time.time() > end - 1.0:
                subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "scripts" / "shot_pcsx2.ps1"),
                                "-Out", f"{prefix}_m{m}.png", "-ProcId", pid], capture_output=True)
                shot = True
            time.sleep(0.004)
        print("motion", m, "done", flush=True)
