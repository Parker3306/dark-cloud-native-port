"""Restart PCSX2 on the patched disc, load each save state and screenshot it. Usage: env_shots.py <out prefix> [slots...]"""
import subprocess, sys, time
sys.path.insert(0, "coop")
from pine import Pine
out = sys.argv[1]; slots = [int(x) for x in sys.argv[2:]] or [1, 2]
subprocess.run(["powershell", "-NoProfile", "-Command", "Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*\DarkCloud\tools\*' } | Stop-Process -Force"]); time.sleep(3)
pid = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "tools/dc/launch_pcsx2.ps1"], capture_output=True, text=True).stdout.strip()
for _ in range(60):
    time.sleep(3)
    try:
        with Pine(28011) as p:
            if p.status() == 0:
                p.read(0x00100000); break
    except Exception: pass
for slot in slots:
    with Pine(28011) as p: p.load_state(slot)
    time.sleep(5)
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "scripts/shot_pcsx2.ps1", "-Out", f"{out}_s{slot}.png", "-ProcId", pid], capture_output=True)
print("done")
