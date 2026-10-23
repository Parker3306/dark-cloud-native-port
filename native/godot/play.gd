extends Node3D
# Command-line harness for the dungeon (tests and screenshots); the real game starts at game.tscn.
# Usage (after "--"):  --iso <iso> [--dungeon 0] [--floor 0] [--seed 1] [--char dun/mainchara/c01d.chr]
#   --script up:120,right:60,attack:3 (pad presets per tick)  --out <absolute prefix> --shots 60,120  --give 195,145 (item ids into the bag)
#   --character 0..5 (who to play)  --switch N --switch-at TICK (call character N in at the first shot at or after TICK; they must have joined: --join 1,2)  --teleport x,z  --to-chest n  --events 1 (print chests/atra/markers/monsters)  --menu 0|1|2|3  --load 1 (continue from user://save1.dc)

func _arg(name: String, default: String = "") -> String:
	var a := OS.get_cmdline_user_args()
	var i := a.find(name)
	return a[i + 1] if i >= 0 and i + 1 < a.size() else default

func _ready() -> void:
	var models := DcModels.new()
	if not models.open_iso(_arg("--iso", ProjectSettings.globalize_path("res://../../iso/Dark Cloud (USA).iso"))):
		print("OPEN_FAILED")
		get_tree().quit(1)
		return
	var dungeon := int(_arg("--dungeon", "0"))
	var floor_index := int(_arg("--floor", "0"))
	models.new_game(int(_arg("--seed", "1")))
	if _arg("--coop-players", "") != "":   # test: the floors get the balance of that many players (hit points, damage, monster count, drops; dccore/coop_scale.h) without a network
		models.set_coop_players(int(_arg("--coop-players")))
	if _arg("--load", "") != "":
		if models.load_game(ProjectSettings.globalize_path("user://save1.dc")):
			var info := models.get_session_status()
			dungeon = int(info["dungeon"])
			floor_index = int(info["floor"])
			print("LOADED floor ", floor_index, " ", info)
		else:
			print("LOAD_FAILED")
	for j in _arg("--join", "").split(",", false):
		models.join_party(int(j))
	if _arg("--character", "") != "":
		models.set_character(int(_arg("--character")))   # 0 Toan, 1 Xiao, 2 Goro, 3 Ruby, 4 Ungaga, 5 Osmond (they join with their first weapon)
	for gf in _arg("--game-flag", "").split(",", false):   # tests: story flags set (e.g. 24 = the horn of d01's boss door was used)
		models.cheat_set_game_flag(int(gf), true)
	if _arg("--game-int", "") != "":   # tests: --game-int index,value (the story state; 10 = the stage of the last dungeon's bosses)
		var gi := _arg("--game-int").split(",")
		models.cheat_set_game_int(int(gi[0]), int(gi[1]))
	for item in _arg("--give", "").split(",", false):
		models.cheat_give_item(int(item))
	var options := {"verbose": true, "first_floor": floor_index, "auto_pick": _arg("--manual-pick", "") == ""}   # (the item window answers by itself in the harness)
	if _arg("--sound", "") != "":   # real sound: the sound set of the dungeon, music and effects (monster sounds are logged)
		var snd: DcSound = models.build_sound()
		if snd != null:
			add_child(snd)
			options["sound"] = snd
	if _arg("--script", "") != "":
		options["script"] = DungeonRun.parse_script(_arg("--script"))
	if _arg("--pad-file", "") != "":   # portkit replay: the recorded controller input of a human (portkit/PORTKIT.md), one entry per tick
		options["script"] = preload("res://pad_file.gd").load_ticks(_arg("--pad-file"))
	if _arg("--teleport", "") != "":
		var t := _arg("--teleport").split(",")
		options["teleport"] = [float(t[0]), float(t[1]), float(t[2]) if t.size() > 2 else 30.0]
	if _arg("--to-marker", "") != "":   # stand on the marker with that event id (200: the back floor's entrance)
		options["to_marker"] = int(_arg("--to-marker"))
	if _arg("--to-chest", "") != "":
		options["to_chest"] = int(_arg("--to-chest"))
	if _arg("--events", "") != "":
		options["events"] = true
	if _arg("--menu", "") != "":
		options["menu"] = int(_arg("--menu"))
	if _arg("--out", "") != "" and _arg("--live-shots", "") == "":
		options["manual"] = true
	var net: CoopNet = null
	var live_data := {}
	if _arg("--coop", "") != "":   # --coop host | join:<ip> [--port n] [--coop-peers n] [--pname name]: players meet in the same floor (the host's seed)
		net = CoopNet.create(self)
		await net.wait_ready()
		var port := int(_arg("--port", str(CoopNet.DEFAULT_PORT)))
		if _arg("--coop") == "host":
			print("COOP host ", net.host_game(port, _arg("--pname", "Host")))
			var waited := 0.0
			while net.roster.size() < 1 + int(_arg("--coop-peers", "1")) and waited < 30.0:
				await get_tree().create_timer(0.1).timeout
				waited += 0.1
			live_data = {"dungeon": dungeon, "floor": floor_index, "seed": models.draw_coop_seed(), "status": models.get_status_bytes()}
			net.send_event("enter_dungeon", live_data)
			print("COOP peers ", net.roster.size() - 1)
		else:
			var holder := {}
			net.event_received.connect(func(_f, kind, data): if kind == "enter_dungeon": holder["d"] = data)
			print("COOP join ", net.join_game(_arg("--coop").substr(5), port, _arg("--pname", "Guest")))
			var waited2 := 0.0
			while not holder.has("d") and waited2 < 40.0:
				await get_tree().create_timer(0.1).timeout
				waited2 += 0.1
			live_data = holder.get("d", {})
			print("COOP my index ", net.my_index, " got floor ", not live_data.is_empty())
		if not live_data.is_empty():
			dungeon = int(live_data["dungeon"])
			floor_index = int(live_data["floor"])
			options["first_floor"] = floor_index
			options["coop_seed"] = live_data["seed"]
			options["coop_status"] = live_data["status"]
			options["coop_back"] = bool(live_data.get("back", false))
	var run := DungeonRun.new()
	add_child(run)
	if not run.start(models, dungeon, floor_index, _arg("--char", "dun/mainchara/c01d.chr"), options):
		get_tree().quit(1)
		return
	print("STARTED ", run.walker)
	run.left.connect(func(reason: String): print("LEFT ", reason, " ", models.get_clear_jump(), " int0 ", models.get_session_status()))
	if _arg("--trace", "") != "":
		run.walker.debug_trace(true)
	if _arg("--polys", "") != "":   # x,z: the collision polygons of the cell there (walls: normals that are not up)
		var pq := _arg("--polys").split(",")
		var plist: Array = run.walker.debug_polys_near(float(pq[0]), float(pq[1]))
		print("POLYS ", plist.size())
		var px := float(pq[0])
		var pz := float(pq[1])
		for q in plist:
			if absf(q["n"].y) < 0.7:
				if pq.size() > 2:   # x,z,range: only the walls that reach above the feet (4) within that range of the point
					var rg := float(pq[2])
					var near := false
					for k in ["v0", "v1", "v2"]:
						var v: Vector3 = q[k]
						if absf(v.x - px) < rg and absf(v.z - pz) < rg and maxf(maxf(q["v0"].y, q["v1"].y), q["v2"].y) > 4.0:
							near = true
					if not near:
						continue
				print("WALL ", q)
	if _arg("--scan", "") != "":   # x0,x1,z0,z1,step,from_y: print the floor heights under a grid
		var sc := _arg("--scan").split(",")
		var step := float(sc[4])
		var hs := run.walker.debug_floor_scan(float(sc[0]), float(sc[1]), float(sc[2]), float(sc[3]), step, float(sc[5]))
		var cols := int((float(sc[1]) - float(sc[0])) / step) + 1
		for r in range(hs.size() / cols):
			var line := ""
			for c in range(cols):
				var h := hs[r * cols + c]
				line += "  .." if h < -9000.0 else "%4d" % int(h)
			print("SCAN z=%d %s" % [int(float(sc[2]) + r * step), line])
	if _arg("--run-function", "") != "":   # tests: start that story function of the floor at once
		run.walker.debug_run_function(int(_arg("--run-function")))
	if _arg("--event-flag", "") != "":   # tests: set per-floor event flag i (e.g. the back-floor entrance unlock: 1 on d01, 0 on the others)
		run.walker.debug_set_event_flag(int(_arg("--event-flag")), 1)
	if _arg("--set-trap", "") != "":   # tests: --set-trap chest,id changes the trap id of a chest (5 = the Happy Clown)
		var st := _arg("--set-trap").split(",")
		run.walker.debug_set_chest_trap(int(st[0]), int(st[1]))
	if _arg("--swap-floor", "") != "":   # tests: swap with the back floor at once
		run.walker.debug_swap_floor()
	if _arg("--eb-auto", "") != "":   # the EB duel played perfectly (every key pressed that many ticks after its line)
		run.walker.debug_eb_autoplay(int(_arg("--eb-auto")))
	if _arg("--boss-down", "") != "":
		run.walker.debug_boss_down()
	if _arg("--boss-hp", "") != "":   # tests: weaken the boss (slot 0)
		for k in range(8 if _arg("--boss-hp-all", "") != "" else 1):
			run.walker.debug_set_monster_hp(k, int(_arg("--boss-hp")))
	if _arg("--quick", "") != "":   # put the bag item with this id into quick slot 1
		for it in run.walker.get_bag():
			if int(it["id"]) == int(_arg("--quick")):
				print("QUICK ", run.walker.assign_quick(0, int(it["slot"])))
				break
	var prefix := _arg("--out", "")
	if prefix == "":
		return   # interactive
	if _arg("--live-shots", "") != "":   # real time (the network needs frames): screenshots after N seconds; the scripted pad plays on the physics ticks
		var started := Time.get_ticks_msec()
		var cycled := false
		var exited := false
		var gifted := false
		if _arg("--near-monster", "") != "":   # stand next to that monster (offset --near-dx), to fight it
			await get_tree().create_timer(0.5).timeout
			for mo in run.walker.get_monster_info():
				if int(mo["slot"]) == int(_arg("--near-monster")):
					var mp: Vector3 = mo["pos"]
					run.walker.teleport(mp.x + float(_arg("--near-dx", "0")), mp.z - 14.0, mp.y + 12.0)
					print("NEAR monster ", mo["slot"], " at ", mp)
		for s in _arg("--live-shots").split(",", false):
			var until := float(s) * 1000.0
			while float(Time.get_ticks_msec() - started) < until:
				await get_tree().process_frame
				if _arg("--gift-at", "") != "" and run.coop != null and float(Time.get_ticks_msec() - started) > float(_arg("--gift-at")) * 1000.0 and not gifted:
					gifted = true
					var bag: Array = run.walker.get_bag()
					print("GIFT bag ", bag.size(), " -> ", run.coop.drop_item(int(bag[0]["slot"])) if bag.size() > 0 else false)
				if _arg("--exit-at", "") != "" and run.coop != null and float(Time.get_ticks_msec() - started) > float(_arg("--exit-at")) * 1000.0 and not exited:
					exited = true
					run.coop.request_floor_exit()
				if _arg("--cycle-at", "") != "" and run.coop != null and float(Time.get_ticks_msec() - started) > float(_arg("--cycle-at")) * 1000.0 and not cycled:
					cycled = true
					run.coop.cycle_character(int(_arg("--cycle", "1")))
				if _arg("--god", "") != "" and run.walker != null:
					run.walker.debug_full_life()
				if _arg("--pad-pulse", "") != "" and run.walker != null:   # --pad-pulse START,PERIOD,MASK: from START seconds a pad button (mask 64 = OK, 32 = lock-on) is pressed for a moment every PERIOD seconds
					var pp := _arg("--pad-pulse").split(",")
					var el := float(Time.get_ticks_msec() - started) / 1000.0 - float(pp[0])
					run.walker.set_hold_pad(128, 128, int(pp[2]) if el >= 0.0 and fposmod(el, float(pp[1])) < 0.12 else 0)
				if _arg("--god", "") != "" and run.walker != null:
					run.walker.debug_full_life()
				if _arg("--dran-hit-at", "") != "" and run.coop != null and float(Time.get_ticks_msec() - started) > float(_arg("--dran-hit-at").split(":")[0]) * 1000.0 and not _dran_done.has("live"):   # --dran-hit-at SECONDS:TILE: a shot hits that tile of Dran's floor
					_dran_done["live"] = true
					run.walker.debug_dran_hit(int(_arg("--dran-hit-at").split(":")[1]))
					run.walker.debug_dran_hit(int(_arg("--dran-hit-at").split(":")[1]))
			await RenderingServer.frame_post_draw
			get_viewport().get_texture().get_image().save_png("%s_live%s.png" % [prefix, s])
			var info := ""
			if net != null and run.coop != null:
				info = " index %d puppets %d" % [net.my_index, run.coop.puppets.size()]
			print("LIVE ", s, " floor ", run.floor_no, " player ", run.walker.get_player_position(), info, " coop ", run.walker.coop_debug(), " life ", run.walker.get_player_status().get("life", -1), " bag ", run.walker.get_bag().size(), " back ", run.walker.is_back_floor(), " dran ", run.walker.get_dran_state() if _arg("--dran-hit-at", "") != "" else "")
			if _arg("--live-events", "") != "":
				var chs: Array = run.walker.get_event_info()["chests"]
				print("  CHESTS ", chs.slice(0, 3))
			if _arg("--live-monsters", "") != "":
				for mo in run.walker.get_monster_info():
					if int(mo["state"]) >= 1:
						print("  MON ", mo)
		get_tree().quit()
		return
	var done := 0
	var switched := false
	var tele_done := false
	var died := false
	var revived := false
	for s in _arg("--shots", "0").split(",", false):
		var target := int(s)
		var remaining := target - done
		while remaining > 0:   # --chase N: every N ticks the player is put under the boss (slot 0), to see whether it can be hurt at all
			var chunk := mini(remaining, int(_arg("--chase", "0"))) if _arg("--chase", "") != "" else (mini(remaining, 20) if _arg("--god", "") != "" else remaining)   # (--god refills between chunks)
			if _arg("--boss-hp-keep", "") != "" and not run.walker.get_event_ui().get("running", false):   # (script-driven boss rooms respawn their monsters after the intro: the weakening is repeated)
				for mi in run.walker.get_monster_info():
					if int(mi.get("hp", 0)) > int(_arg("--boss-hp", "1")):
						run.walker.debug_set_monster_hp(int(mi["slot"]), int(_arg("--boss-hp", "1")))
			if _arg("--track", "") != "" and int(done + (target - done - remaining)) % int(_arg("--track")) < chunk:   # --track N: every N ticks the hero and the living monsters
				var tk := PackedStringArray()
				for mi in run.walker.get_monster_info():
					if int(mi.get("hp", 0)) > 0:
						var mp: Vector3 = mi["pos"]
						tk.append("%s(%.0f,%.0f,%.0f m%d)" % [mi["name"], mp.x, mp.y, mp.z, int(mi.get("motion", 0))])
				var hp3 := run.walker.get_player_position()
				print("TRK ", int(done + (target - done - remaining)), " hero (%.0f,%.0f,%.0f) lock %d " % [hp3.x, hp3.y, hp3.z, int(run.walker.get_player_status().get("lock", 0))], " ".join(tk))
			if _arg("--dran-hit", "") != "":   # --dran-hit TICK:TILE,..: a shot hits that tile of Dran's floor (0..11) from that tick on
				for dh in _arg("--dran-hit").split(",", false):
					var dp := dh.split(":")
					if int(done + (target - done - remaining)) >= int(dp[0]) and not _dran_done.has(dh):
						_dran_done[dh] = true
						run.walker.debug_dran_hit(int(dp[1]))
						run.walker.debug_dran_hit(int(dp[1]))
						print("DRAN tiles ", run.walker.get_dran_state())
			if _arg("--hold-at", "") != "" and int(done + (target - done - remaining)) >= int(_arg("--hold-from", "2300")):   # --hold-at x,z [--hold-from TICK]: the hero is put back there every chunk (where does the boss go when the player stands far away?)
				var ha := _arg("--hold-at").split(",")
				run.walker.teleport(float(ha[0]), float(ha[1]))
			if _arg("--bot", "") != "":   # tests: a player that walks up to the nearest monster body sphere and swings (set --script none:1 so that devices stay off)
				chunk = mini(remaining, 6)
				_bot_pad(run, int(done + (target - done - remaining)))
			run.advance(chunk)
			remaining -= chunk
			if _arg("--god", "") != "":
				run.walker.debug_full_life()
			if _arg("--chase", "") != "":
				for m in run.walker.get_monster_info():
					if int(m["slot"]) == 0 and int(m["state"]) >= 1:
						var bp: Vector3 = m["pos"]
						run.walker.teleport(bp.x, bp.z - float(_arg("--chase-dist", "20")), bp.y + float(_arg("--chase-drop", "12")))
		done = target
		if _arg("--teleport-at", "") != "" and not tele_done and target >= int(_arg("--teleport-at")):   # --teleport-at TICK --teleport-to x,z
			tele_done = true
			var tt := _arg("--teleport-to", "0,0").split(",")
			run.walker.teleport(float(tt[0]), float(tt[1]))
		if _arg("--die-at", "") != "" and target >= int(_arg("--die-at")) and not died:   # tests: the player dies
			died = true
			run.walker.debug_kill_player()
		if _arg("--revive-at", "") != "" and target >= int(_arg("--revive-at")) and not revived:   # tests: co-op revive
			revived = true
			run.walker.coop_revive()
		if _arg("--kill-at", "") != "" and target >= int(_arg("--kill-at")):   # tests: every monster on the floor dies
			run.walker.debug_kill_monsters()
		if _arg("--switch", "") != "" and target >= int(_arg("--switch-at", "0")) and not switched:
			switched = true
			print("SWITCH ", _arg("--switch"), models.switch_character(run.walker, int(_arg("--switch"))))
		if _arg("--freecam", "") != "":   # px,py,pz,tx,ty,tz[,fov]: a camera of our own for the screenshots
			var fc := _arg("--freecam").split(",")
			var fcam := Camera3D.new()
			add_child(fcam)
			fcam.fov = float(fc[6]) if fc.size() > 6 else 60.0
			fcam.far = 8000.0
			fcam.look_at_from_position(Vector3(float(fc[0]), float(fc[1]), float(fc[2])), Vector3(float(fc[3]), float(fc[4]), float(fc[5])))
			fcam.make_current()
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_t%d.png" % [prefix, target])
		print("SHOT ", target, " floor ", run.floor_no, " player ", run.walker.get_player_position(), " status ", run.walker.get_player_status())
		if _arg("--bag", "") != "":
			print("  BAG ", run.walker.get_bag())
		if run.walker.get_event_ui().get("running", false):
			print("  EVENT RUNNING ", run.walker.get_event_ui())
		if _arg("--eb-auto", "") != "":
			print("  EBLOG ", run.walker.get_eb_log())
		var ebui: Dictionary = run.walker.get_eb_ui()
		if not ebui.is_empty():
			print("  EB active ", ebui.get("active"), " count ", ebui.get("count"), " hit ", ebui.get("key_count"), "/", ebui.get("keys", []).size(), " result ", ebui.get("result"), " finish ", ebui.get("finish"), " intro ", ebui.get("intro"))
		if _arg("--trace", "") != "":
			var lg := run.walker.debug_log()
			for k in range(maxi(lg.size() - maxi(int(_arg("--trace")), 40), 0), lg.size()):
				print("  LOG ", lg[k])
		if _arg("--event-end", "") != "":
			print("  EVENTEND ", run.walker.debug_event_end())
		if _arg("--cols", "") != "":   # the monsters' body spheres that are on
			print("  HITSTATS ", run.walker.debug_hit_stats())
			print("  DRANTILES ", run.walker.get_dran_state())
			for mc in run.walker.debug_monster_cols():
				print("  COLS ", mc)
			for sp in run.walker.debug_player_spheres():
				print("  SPH ", sp)
		if _arg("--monsters", "") != "":
			for m in run.walker.get_monster_info():
				print("  MON ", m)
		if options.get("events", false):
			var ei: Dictionary = run.walker.get_event_info()
			print("  active event ", ei.get("active_event", -2))
			for c in ei["chests"]:
				print("  CHEST ", c)
	get_tree().quit()

# A crude fighter for the boss checks: steer to the nearest body sphere of a living monster (else the nearest monster) and swing when close.
func _bot_pad(run: DungeonRun, tick: int) -> void:
	var w: DcWalker = run.walker
	if w.get_event_ui().get("running", false):   # a scene or a message: the OK button (every 20 ticks) and the lock button for the choices
		if _bot_pressed:
			if not _bot_ran:
				print("BOT event started at tick ", tick, " hero ", w.get_player_position())
			_bot_ran = true
		w.set_hold_pad(128, 128, 64 if (tick / 10) % 2 == 0 else 32)
		return
	var hero := w.get_player_position()
	if _arg("--bot-lock-pulse", "") != "" and (tick / 6) % 20 == 0:   # tests: the lock-on button pressed every 120 ticks (once the key doors are done), whatever else the bot does
		w.set_hold_pad(128, 128, 32)
		return
	if _bot_marker_step(w, hero, tick):
		return
	var best := Vector3.ZERO
	var best_d := 1e9
	var rng_pre := float(_arg("--bot-range", "24"))
	var cols_all: Array = w.debug_monster_cols()
	var any_open := false   # (what is invulnerable right now is left alone while something else can be hurt: d05's boss guards until its helpers are down)
	for mc0 in cols_all:
		if int(mc0["hp"]) >= 1 and int(mc0["muteki"]) == 0:
			any_open = true
	for mc in cols_all:
		if int(mc["hp"]) < 1 or (any_open and int(mc["muteki"]) > 0):
			continue
		var pts: Array = []
		for c in mc["cols"]:
			if rng_pre < 100.0 and float(c["pos"].y) - float(c["r"]) - hero.y > 42.0:   # a melee fighter cannot reach what hovers higher than that: wait for it to come down
				continue
			pts.append(c["pos"])
		if pts.is_empty():
			continue
		for p in pts:
			var d: float = Vector2(p.x - hero.x, p.z - hero.z).length()
			if d < best_d:
				best_d = d
				best = p
	if best_d > 1e8 and _arg("--bot-wait-near", "") != "":   # --bot-wait-near 1: nothing is open right now (a boss whose weak point opens only in some motions): stay close to the nearest living monster and be ready
		var near_d := 1e9
		var near_p := Vector3.ZERO
		for mo in w.get_monster_info():
			if int(mo["state"]) < 1:
				continue
			var mp: Vector3 = mo["pos"]
			var dd := Vector2(mp.x - hero.x, mp.z - hero.z).length()
			if dd < near_d:
				near_d = dd
				near_p = mp
		if near_d < 1e8 and near_d > 30.0:
			var cs0 := w.get_camera_state()
			var fwd0 := Vector2(cs0["ref"].x - cs0["pos"].x, cs0["ref"].z - cs0["pos"].z).normalized()
			var right0 := Vector2(-fwd0.y, fwd0.x)
			var dn0 := Vector2(near_p.x - hero.x, near_p.z - hero.z).normalized()
			w.set_hold_pad(int(clampf(128.0 + dn0.dot(right0) * 127.0, 0.0, 255.0)), int(clampf(128.0 - dn0.dot(fwd0) * 127.0, 0.0, 255.0)), 0)
			return
	if best_d > 1e8:
		w.set_hold_pad(128, 128, 0)
		return
	var cs := w.get_camera_state()
	var fwd := Vector2(cs["ref"].x - cs["pos"].x, cs["ref"].z - cs["pos"].z).normalized()
	var right := Vector2(-fwd.y, fwd.x)
	var dv := Vector2(best.x - hero.x, best.z - hero.z)
	var dn := dv.normalized()
	var f := dn.dot(fwd)
	var sd := dn.dot(right)
	var lx := 128
	var ly := 128
	var buttons := 0
	var rng := float(_arg("--bot-range", "24"))   # a ranged character keeps pushing towards the target (to face it) and shoots from afar
	if best_d > 24.0 or rng >= 100.0:
		lx = int(clampf(128.0 + sd * 127.0, 0.0, 255.0))
		ly = int(clampf(128.0 - f * 127.0, 0.0, 255.0))
	elif best_d > 3.0:   # close: a gentle push towards it, so that the hero keeps facing it
		lx = int(clampf(128.0 + sd * 40.0, 0.0, 255.0))
		ly = int(clampf(128.0 - f * 40.0, 0.0, 255.0))
	if best_d < maxf(40.0, rng) and ((tick / 6) % 4 == 0 or _arg("--bot-hold-ok", "") != ""):   # (--bot-hold-ok: Ungaga's blast needs the attack button held)
		buttons = 64
	if rng >= 100.0 and _arg("--bot-lock", "") != "":
		if _arg("--bot-lock-pulse", "") != "":   # --bot-lock-pulse 1: a press every 120 ticks (the button toggles the lock; held it only counts once)
			if (tick / 6) % 20 == 0:
				buttons |= 32
		else:
			buttons |= 32   # (the lock-on button is held)
	if _arg("--bot-throw", "") != "":   # --bot-throw 1: the lock-on is held and the quick item (--give / --quick) is thrown at the target every 60 ticks (the way to hurt what flies out of the sword's reach)
		buttons = 0
		if int(w.get_player_status().get("lock", 0)) == 0:   # (the lock-on button toggles: press it only while the lock is off)
			if (tick / 6) % 5 == 0:
				buttons = 32
		elif (tick / 6) % 10 == 0:
			buttons = 128
	w.set_hold_pad(lx, ly, buttons)

# --bot-markers 401,400: before it fights, the bot walks to each marker (by event id) in turn and uses it (the item-use button; automatic markers start by being stepped on) and waits until the event it started has ended.
var _bot_mi := 0
var _dran_done := {}
var _bot_ran := false
var _bot_pressed := false
var _bot_ends := 0
var _bot_best := 1e9
var _bot_progress_tick := 0
var _bot_detour_until := -1
var _bot_side := 1.0
var _bot_since := -1
func _bot_marker_step(w: DcWalker, hero: Vector3, tick: int) -> bool:
	var ids := _arg("--bot-markers", "").split(",", false)
	if _bot_mi >= ids.size():
		return false
	if _bot_since < 0:
		_bot_since = tick
	var want := 0
	var target := Vector3.ZERO
	var found := false
	var is_on := false
	var radius := 6.0
	var waypoint := ids[_bot_mi].contains(":")   # x:z = a point to walk to (round an obstacle)
	if waypoint:
		var xz := ids[_bot_mi].split(":")
		target = Vector3(float(xz[0]), 0.0, float(xz[1]))
		found = true
		radius = 14.0
	else:
		want = int(ids[_bot_mi])
		for m in w.get_event_info()["markers"]:
			if int(m["event"]) == want:
				radius = float(m["radius"]) * 0.7
				target = m["pos"]
				found = true
				is_on = bool(m["on"])
	var running: bool = w.get_event_ui().get("running", false)
	if waypoint and not running and Vector2(target.x - hero.x, target.z - hero.z).length() <= radius:
		_bot_mi += 1
		_bot_since = -1
		_bot_best = 1e9
		return true
	var ends := int(w.debug_event_end().get("count", 0))
	if _bot_pressed and ends > _bot_ends:   # (a short event starts and ends between two looks)
		_bot_ran = true
	if _bot_ran and not running:   # the event it started has ended
		print("BOT marker ", want, " done at tick ", tick, " hero ", hero)
		_bot_mi += 1
		_bot_ran = false
		_bot_pressed = false
		_bot_since = -1
		_bot_best = 1e9
		return true
	if not found or (not is_on and tick - _bot_since > 600) or tick - _bot_since > 3000:
		print("BOT marker ", want, " skipped (found ", found, " on ", is_on, ") at tick ", tick, " hero ", hero)
		_bot_mi += 1
		_bot_since = -1
		return true
	var d := Vector2(target.x - hero.x, target.z - hero.z)
	var buttons := 0
	var lx := 128
	var ly := 128
	if d.length() > radius:
		var cs := w.get_camera_state()
		var fwd := Vector2(cs["ref"].x - cs["pos"].x, cs["ref"].z - cs["pos"].z).normalized()
		var right := Vector2(-fwd.y, fwd.x)
		var dn := d.normalized()
		if tick < _bot_detour_until:   # no progress for a while: go sideways round whatever is in the way (the side changes each time)
			dn = Vector2(-dn.y, dn.x) * _bot_side + dn * 0.2
			dn = dn.normalized()
		else:
			if d.length() < _bot_best - 8.0:
				_bot_best = d.length()
				_bot_progress_tick = tick
			elif tick - _bot_progress_tick > 150:
				_bot_side = -_bot_side
				_bot_detour_until = tick + 240
				_bot_progress_tick = tick + 240
				_bot_best = d.length()
		lx = int(clampf(128.0 + dn.dot(right) * 127.0, 0.0, 255.0))
		ly = int(clampf(128.0 - dn.dot(fwd) * 127.0, 0.0, 255.0))
	elif is_on and not waypoint and (tick / 6) % 3 == 0:
		buttons = 128   # the item-use button
		if not _bot_pressed:
			print("BOT using marker ", want, " at tick ", tick)
			_bot_ends = int(w.debug_event_end().get("count", 0))
		_bot_pressed = true
	w.set_hold_pad(lx, ly, buttons)
	return true
