#!/bin/bash
# Smoke test of the native port: command line tools and short Godot harness runs; prints FAIL lines for anything that errors.
cd "$(dirname "$0")/.."
ISO="iso/Dark Cloud (USA).iso"
GODOT="${GODOT:-$(cygpath -m "$LOCALAPPDATA")/Microsoft/WinGet/Packages/GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe/Godot_v4.7.2-stable_win64_console.exe}"
fail=0
check() { if ! "$@" > work/smoke_last.log 2>&1; then echo "FAIL: $*"; tail -3 work/smoke_last.log; fail=1; else echo "ok:   $1"; fi; }
T=native/build/core_release
check $T/dcgeorama.exe "$ISO"
check $T/dcshop.exe "$ISO"
check $T/dcweapon.exe "$ISO"
check $T/dcopening.exe "$ISO"
godot_run() {
  name=$1; shift
  rm -f work/smoke_$name.png
  timeout 200 "$GODOT" --path native/godot "$@" > work/smoke_$name.log 2>&1
  if grep -qE "SCRIPT ERROR|ERROR: [^B]" work/smoke_$name.log; then echo "FAIL: godot $name"; grep -E "SCRIPT ERROR|ERROR: [^B]" work/smoke_$name.log | head -3; fail=1; else echo "ok:   godot $name"; fi
}
godot_run town res://town_play.tscn -- --town e01 --complete 0 --atla 0 --event -2 --teleport 0,0,0 --script none:2,select:1,none:10,cross:1 --edit-part 0 --edit-at 8 --out $(pwd -W)/work/smoke_town --shots 20
godot_run shop res://town_play.tscn -- --town e01 --complete 0 --event -2 --gold 500 --shop 0 --out $(pwd -W)/work/smoke_shop --shots 3
godot_run dungeon res://play.tscn -- --seed 5 --script up:20,attack:3 --out $(pwd -W)/work/smoke_dungeon --shots 60
godot_run opening res://opening_play.tscn -- --out $(pwd -W)/work/smoke_opening --steps 100
check python work/back_smoke.py
check python work/trap_smoke.py
exit $fail
