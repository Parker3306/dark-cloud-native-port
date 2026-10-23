#!/bin/bash
# Review every character on the ALL-PACKS disc (tools/dc/patch_all.py): launch PCSX2 once, switch to each character, photograph it from four sides.
# Usage: tools/dc/review.sh <out dir>
cd "$(dirname "$0")/../.."
OUT="$1"; mkdir -p "$OUT"
for spec in "toan 0 800,430,1150,850" "xiao 1 800,430,1150,850" "ruby 3 800,420,1150,850" "ungaga 4 780,250,1170,850"; do
  set -- $spec; NAME=$1; CH=$2; CROP=$3
  # a fresh emulator per character: loading a state again after the hook ran once made the recompiler abort ("Impossible block clearing failure")
  powershell -NoProfile -Command "Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Where-Object { \$_.Path -like '*\DarkCloud\tools\*' } | Stop-Process -Force"; sleep 3
  PID=$(powershell -NoProfile -ExecutionPolicy Bypass -File tools/dc/launch_pcsx2.ps1 | tr -d '
')
  python -I - <<'PY'
import sys, time
sys.path.insert(0, "coop")
from pine import Pine
for _ in range(60):
    time.sleep(3)
    try:
        with Pine(28011) as p:
            if p.status() == 0:
                p.read(0x00100000); break
    except Exception:
        pass
PY
  sleep 3
  timeout 150 python -I tools/dc/ingame_reload.py 28011 $CH 2>&1 | tail -1
  sleep 2
  python -I tools/dc/yaw_shots.py $PID "$OUT/$NAME" 0 45 90 180 > /dev/null 2>&1
  python -I - <<PY
from PIL import Image
box = tuple(int(v) for v in "$CROP".split(","))
ims = [Image.open("$OUT/${NAME}_y%d.png" % a).convert("RGB").crop(box) for a in (0, 45, 90, 180)]
w, h = ims[0].size
o = Image.new("RGB", (w * 4, h))
for i, im in enumerate(ims): o.paste(im, (i * w, 0))
o.save("$OUT/${NAME}_sheet.png")
PY
done
