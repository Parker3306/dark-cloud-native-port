#!/bin/bash
# Restart PCSX2 on the patched ISO and load save state 1 (the dungeon test floor, seed 8097), then wait until the dungeon frame loop runs.
# Usage: tools/dc/reset_game.sh
cd "$(dirname "$0")/../.."
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dc/launch_pcsx2.ps1 | tail -1
python -I - <<'PY'
import sys, time
sys.path.insert(0, "coop")
from pine import Pine
for _ in range(60):
    time.sleep(3)
    try:
        with Pine(28011) as p:
            if p.status() == 0:
                p.read(0x00100000)
                time.sleep(5)
                p.load_state(1)
                print("state 1 loading")
                break
    except Exception:
        pass
else:
    print("PINE never came up")
    sys.exit(1)
time.sleep(8)
from hooks import Cave, PHASES
with Pine(28011) as p:
    c = Cave(p)
    if not c.installed():
        c.install()
    hb = c.heartbeat()
    time.sleep(1)
    print("heartbeat", hb, "->", c.heartbeat(), "gameTask", p.read(0x2A35EC))
PY
