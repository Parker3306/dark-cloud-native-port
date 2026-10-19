#!/bin/bash
# Patch the disc with a rebuilt pack, restart PCSX2, reload Toan from disc, face the camera and take front + pose screenshots.
# Usage: tools/dc/ingame_cycle.sh <new .chr> <out prefix>      (SKIP_LAUNCH=<pid> reuses a running PCSX2 and skips patch + launch)
# PCSX2 can need 30-60 s before its VM is running (GPU start-up), so we poll PINE status until it reports 0 (running).
cd "$(dirname "$0")/../.."
CHR="$1"; OUT="$2"; CHAR="${CHAR:-0}"; DISC_NAME="${DISC_NAME:-dun\mainchara\c01d.chr}"; MOTIONS="${MOTIONS:-1 11 15 23}"
if [ -n "$SKIP_LAUNCH" ]; then
  PID="$SKIP_LAUNCH"
else
  powershell -NoProfile -Command "Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Stop-Process -Force"; sleep 3
  python -I tools/dc/patch_character.py "$CHR" "$DISC_NAME" | tail -1 || exit 1
  PID=$(powershell -NoProfile -ExecutionPolicy Bypass -File tools/dc/launch_pcsx2.ps1 | tr -d '\r')
fi
echo "pcsx2 pid $PID"
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
                print("PINE up, VM running")
                break
    except Exception:
        pass
else:
    print("PINE never reported a running VM")
PY
sleep 3
timeout 150 python -I tools/dc/ingame_reload.py 28011 $CHAR 2>&1 | tail -1
sleep 2
python -I - <<PY
import sys, time, subprocess, math
sys.path.insert(0, "coop")
from pine import Pine
import memmap as MM
with Pine(28011) as p:
    cur = p.read_float(MM.CHARA_MAIN + MM.CHARA_YAW)
    target = cur + math.pi
    if target > math.pi: target -= 2 * math.pi
    end = time.time() + 2.0; shot = False
    while time.time() < end:
        p.write_float(MM.CHARA_MAIN + MM.CHARA_YAW, target)
        p.write(MM.CHARA_MAIN + MM.CHARA_MOTION, 0)
        if not shot and time.time() > end - 1.0:
            subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "scripts/shot_pcsx2.ps1", "-Out", "${OUT}_front.png", "-ProcId", "$PID"], capture_output=True); shot = True
        time.sleep(0.003)
PY
python -I tools/dc/pose_shots.py 28011 $PID "${OUT}_pose" $MOTIONS | tail -1
python -I - <<PY
from PIL import Image
im = Image.open("${OUT}_front.png").convert("RGB")
box = tuple(int(v) for v in "${CROP:-800,430,1150,850}".split(","))
im.crop(box).resize((int((box[2]-box[0])*2.5), int((box[3]-box[1])*2.5)), Image.LANCZOS).save("${OUT}_front_crop.png")
im.crop((880, 740, 1080, 830)).resize((1000, 450), Image.LANCZOS).save("${OUT}_feet.png")
ims = []
for m in [int(x) for x in "$MOTIONS".split()][:4]:
    i = Image.open("${OUT}_pose_m%d.png" % m).convert("RGB"); w, h = i.size
    ims.append(i.crop((int(w*.33), int(h*.33), int(w*.67), int(h*.82))))
o = Image.new("RGB", (sum(i.width for i in ims), max(i.height for i in ims)))
x = 0
for i in ims: o.paste(i, (x, 0)); x += i.width
o.save("${OUT}_poses.png")
PY
