#!/bin/bash
# Co-op smoke test: two Godot instances on this PC (a host and a guest) play together; prints FAIL lines for anything that does not work.
#   town presence + colours, dungeon (same floor, puppets, host monsters hurt by the guest, the guest is hurt, chest sync, gift), floor exit.
cd "$(dirname "$0")/.."
GODOT="${GODOT:-$(cygpath -m "$LOCALAPPDATA")/Microsoft/WinGet/Packages/GodotEngine.GodotEngine_Microsoft.Winget.Source_8wekyb3d8bbwe/Godot_v4.7.2-stable_win64_console.exe}"
W="$(pwd -W)/work"
fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
pair() {   # name, scene, host args, guest args   (host starts first; both log to work/cs_<name>_{host,guest}.log)
  local name=$1 scene=$2 hargs=$3 gargs=$4
  (timeout 150 "$GODOT" --path native/godot res://$scene -- $hargs > work/cs_${name}_host.log 2>&1 &)
  sleep 4
  timeout 150 "$GODOT" --path native/godot res://$scene -- $gargs > work/cs_${name}_guest.log 2>&1
  sleep 6
}
# 7. the back floor: the host goes through the entrance (key item, scene), the guest follows
T="none:60,use:2,none:150"; for i in $(seq 1 30); do T="$T,attack:2,none:60"; done
pair back play.tscn "--dungeon 0 --floor 1 --coop host --give 224 --to-marker 200 --script $T --live-shots 13,28 --out $W/cs_back_host" "--coop join:127.0.0.1 --live-shots 22 --out $W/cs_back_guest"
grep -q "LIVE 22 .* back true" work/cs_back_guest.log && ok "back floor together" || bad "back floor together"
exit $fail
