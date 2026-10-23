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
# 1. town: both see each other
pair town town_play.tscn "--town e01 --event -2 --complete 1 --teleport 103,10,300 --coop host --live-shots 11,17 --out $W/cs_town_host" "--town e01 --event -2 --complete 1 --teleport 125,10,300 --coop join:127.0.0.1 --character 3 --live-shots 8 --out $W/cs_town_guest"
grep -q "puppets 1" work/cs_town_host.log && grep -q "puppets 1" work/cs_town_guest.log && ok "town presence" || bad "town presence"
# 1b. four players in one town: the last guest sees the three others
(timeout 150 "$GODOT" --path native/godot res://town_play.tscn -- --town e01 --event -2 --complete 1 --teleport 103,10,300 --coop host --live-shots 24 --out $W/cs_p4_host > work/cs_p4_host.log 2>&1 &)
sleep 4
(timeout 150 "$GODOT" --path native/godot res://town_play.tscn -- --town e01 --event -2 --complete 1 --teleport 125,10,300 --coop join:127.0.0.1 --live-shots 18 --out $W/cs_p4_g1 > work/cs_p4_g1.log 2>&1 &)
sleep 3
(timeout 150 "$GODOT" --path native/godot res://town_play.tscn -- --town e01 --event -2 --complete 1 --teleport 82,10,300 --coop join:127.0.0.1 --live-shots 15 --out $W/cs_p4_g2 > work/cs_p4_g2.log 2>&1 &)
sleep 3
timeout 150 "$GODOT" --path native/godot res://town_play.tscn -- --town e01 --event -2 --complete 1 --teleport 103,10,330 --coop join:127.0.0.1 --live-shots 11 --out $W/cs_p4_g3 > work/cs_p4_g3.log 2>&1
sleep 12
grep -q "puppets 3 index 3" work/cs_p4_g3.log && ok "four players in one town" || bad "four players in one town"
# 2. dungeon: same floor, the guest hurts a host monster, the guest is hurt, a chest opens for both
S="none:150"; for i in $(seq 1 40); do S="$S,attack:3,none:25"; done
pair dun play.tscn "--dungeon 0 --floor 1 --coop host --near-monster 7 --near-dx -60 --live-monsters 1 --live-events 1 --god 1 --live-shots 8,14 --out $W/cs_dun_host" "--coop join:127.0.0.1 --near-monster 7 --near-dx 0 --god 1 --script $S --live-shots 11 --out $W/cs_dun_guest"
h=$(grep -o '"remote_hits": [0-9]*' work/cs_dun_host.log | tail -1 | grep -o '[0-9]*$')
[ "${h:-0}" -ge 1 ] && ok "guest hits host monster ($h hits)" || bad "guest hits host monster"
g=$(grep -c "cells" work/cs_dun_host.log); [ "$g" -ge 1 ] && ok "dungeon built" || bad "dungeon built"
# 3. the guest is hurt by a host monster
pair hurt play.tscn "--dungeon 0 --floor 1 --coop host --near-monster 7 --near-dx -200 --god 1 --live-shots 12 --out $W/cs_hurt_host" "--coop join:127.0.0.1 --near-monster 7 --near-dx 0 --script none:2000 --live-shots 5,11 --out $W/cs_hurt_guest"
l=$(grep -o 'life [0-9]*' work/cs_hurt_guest.log | tail -1 | grep -o '[0-9]*$')
[ "${l:-70}" -lt 70 ] && ok "guest hurt by a host monster (life $l)" || bad "guest hurt by a host monster"
# 4. a chest the guest opens is open on the host
pair chest play.tscn "--dungeon 0 --floor 1 --coop host --live-events 1 --god 1 --live-shots 5,12 --out $W/cs_chest_host" "--coop join:127.0.0.1 --to-chest 0 --live-events 1 --script none:150,attack:3,none:400 --live-shots 9 --out $W/cs_chest_guest"
grep -A1 "LIVE 12" work/cs_chest_host.log | grep -q '"opened": true' && ok "chest sync" || bad "chest sync"
# 5. a gift from the host reaches the guest's bag
pair gift play.tscn "--dungeon 0 --floor 1 --coop host --give 151 --gift-at 3 --live-shots 9 --out $W/cs_gift_host" "--coop join:127.0.0.1 --teleport 160,1625 --live-shots 8 --out $W/cs_gift_guest"
grep -q "bag 1" work/cs_gift_guest.log && ok "item gift" || bad "item gift"
# 6. the stairs move everybody
pair exit play.tscn "--dungeon 0 --floor 1 --coop host --live-shots 14 --out $W/cs_exit_host" "--coop join:127.0.0.1 --exit-at 3 --live-shots 10 --out $W/cs_exit_guest"
grep -q "LIVE 14 floor 2" work/cs_exit_host.log && grep -q "LIVE 10 floor 2" work/cs_exit_guest.log && ok "floor exit together" || bad "floor exit together"
# 7. the back floor: the host goes through the entrance (key item, scene), the guest follows
T="none:60,use:2,none:150"; for i in $(seq 1 30); do T="$T,attack:2,none:60"; done
pair back play.tscn "--dungeon 0 --floor 1 --coop host --give 224 --to-marker 200 --script $T --live-shots 13,28 --out $W/cs_back_host" "--coop join:127.0.0.1 --live-shots 22 --out $W/cs_back_guest"
grep -q "LIVE 22 .* back true" work/cs_back_guest.log && ok "back floor together" || bad "back floor together"
# 8. Dran's floor: a tile the host's shot breaks is broken on the guest too
pair dran play.tscn "--dungeon 0 --floor 14 --coop host --god 1 --script none:1 --event-end 1 --dran-hit-at 6:3 --live-shots 12 --out $W/cs_dran_host" "--coop join:127.0.0.1 --god 1 --script none:1 --event-end 1 --dran-hit-at 999:0 --live-shots 12 --out $W/cs_dran_guest"
grep -q "LIVE 12 .* dran \[3, 3, 3, 0," work/cs_dran_guest.log && ok "Dran tile sync" || bad "Dran tile sync"
exit $fail
