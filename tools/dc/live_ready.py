"""Exit 0 when the Dark Cloud dungeon frame loop runs in PCSX2 (PINE answers, heartbeat of the code cave advances). Used as [emu] ready_cmd."""
import sys
import time

sys.path.insert(0, "coop")
from pine import Pine
from hooks import Cave

try:
    with Pine(28011) as p:
        if p.status() != 0:
            sys.exit(1)
        c = Cave(p)
        if not c.installed():
            c.install()
        a = c.heartbeat()
        time.sleep(1)
        sys.exit(0 if c.heartbeat() != a else 1)
except Exception as e:
    print("not ready:", e)
    sys.exit(1)
