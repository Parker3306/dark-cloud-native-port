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
			await RenderingServer.frame_post_draw
			get_viewport().get_texture().get_image().save_png("%s_live%s.png" % [prefix, s])
			var info := ""
			if net != null and run.coop != null:
				info = " index %d puppets %d" % [net.my_index, run.coop.puppets.size()]
			print("LIVE ", s, " floor ", run.floor_no, " player ", run.walker.get_player_position(), info, " coop ", run.walker.coop_debug(), " life ", run.walker.get_player_status().get("life", -1), " bag ", run.walker.get_bag().size(), " back ", run.walker.is_back_floor())
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
			var chunk := mini(remaining, int(_arg("--chase", "0"))) if _arg("--chase", "") != "" else remaining
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
		if _arg("--monsters", "") != "":
			for m in run.walker.get_monster_info():
				print("  MON ", m)
		if options.get("events", false):
			var ei: Dictionary = run.walker.get_event_info()
			print("  active event ", ei.get("active_event", -2))
			for c in ei["chests"]:
				print("  CHEST ", c)
	get_tree().quit()
