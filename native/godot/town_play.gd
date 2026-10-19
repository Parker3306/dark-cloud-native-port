extends Node3D
# Command-line harness for the towns: --town e01 --complete 1 --event 128 --script up:120 --teleport x,y,z --out <prefix> --shots 60,120 --cam x,y,z,tx,ty,tz

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
	models.new_game(1)
	ModalUi.auto_choose = _arg("--auto-modal", "") != ""
	var options := {"complete": _arg("--complete", "1") == "1", "event": int(_arg("--event", "-1")), "old_map": int(_arg("--old-map", "0")), "variant": int(_arg("--variant", "0"))}
	if _arg("--script", "") != "":
		options["script"] = DungeonRun.parse_script(_arg("--script"))
	if _arg("--teleport", "") != "":
		var t := _arg("--teleport").split(",")
		options["teleport"] = [float(t[0]), float(t[1]), float(t[2])]
	if _arg("--atla", "") != "":   # every part (and with 2 every tip in its house) of a town's Atla
		models.grant_atla(int(_arg("--atla")), _arg("--atla-install", "0") == "1")
	if _arg("--fish-records", "") != "":   # kind:size,kind:size ...
		for r in _arg("--fish-records").split(",", false):
			var kv := r.split(":")
			models.fish_add_record(int(kv[0]), int(kv[1]))
	if _arg("--fp", "") != "":
		models.fish_set_points(int(_arg("--fp")))
	if _arg("--gold", "") != "":
		models.set_gold(int(_arg("--gold")))
	for g in _arg("--give", "").split(",", false):
		models.give_item(int(g))
	var snd: DcSound = models.build_sound() if _arg("--sound", "") != "" else null
	if snd != null:
		add_child(snd)
	options["sound"] = snd
	var net: CoopNet = null
	if _arg("--coop", "") != "":   # --coop host | join:<ip>  [--port n] [--pname name] [--character n]: two instances of this scene play in one town
		net = CoopNet.create(self)
		await net.wait_ready()
		var mode := _arg("--coop")
		var port := int(_arg("--port", str(CoopNet.DEFAULT_PORT)))
		if mode == "host":
			print("COOP host ", net.host_game(port, _arg("--pname", "Host")))
		else:
			print("COOP join ", net.join_game(mode.substr(5), port, _arg("--pname", "Guest")))
			var waited := 0.0
			while net.my_index == 0 and waited < 10.0:
				await get_tree().create_timer(0.1).timeout
				waited += 0.1
			print("COOP my index ", net.my_index)
		if _arg("--character", "") != "":
			models.set_character(int(_arg("--character")))
	var run := TownRun.new()
	add_child(run)
	if not run.start(models, _arg("--town", "e01"), options):
		get_tree().quit(1)
		return
	if _arg("--live-shots", "") != "":   # real time (the network needs frames): screenshots after N seconds, the scripted pad plays on the physics ticks
		run.walk.set_physics_process(true)
		var started := Time.get_ticks_msec()
		var gifted := false
		for s in _arg("--live-shots").split(",", false):
			var until := float(s) * 1000.0
			while float(Time.get_ticks_msec() - started) < until:
				await get_tree().process_frame
				if _arg("--gift-at", "") != "" and run.coop != null and float(Time.get_ticks_msec() - started) > float(_arg("--gift-at")) * 1000.0 and not gifted:
					gifted = true
					var bag: Array = models.get_bag()
					print("GIFT bag ", bag.size(), " -> ", run.coop.drop_item(int(bag[0]["slot"])) if bag.size() > 0 else false)
			await RenderingServer.frame_post_draw
			get_viewport().get_texture().get_image().save_png("%s_live%s.png" % [_arg("--out", "work/live"), s])
			var info := ""
			if net != null and run.coop != null:
				info = " roster %s puppets %d index %d" % [net.roster, run.coop.puppets.size(), net.my_index]
			print("LIVE ", s, " player ", run.walk.get_player_position(), info, " bag ", models.get_bag().size())
		get_tree().quit()
		return
	if _arg("--manual", "1") == "1":   # only the ticks asked for (the physics frames that pass while a screenshot is taken must not run the game on)
		run.walk.set_physics_process(false)
	if _arg("--fish-auto", "") != "":
		run.walk.set_fish_auto(true)
	if _arg("--eb-auto", "") != "":   # the EB duel of a story scene played perfectly (every key pressed that many ticks after its line)
		run.walk.debug_eb_autoplay(int(_arg("--eb-auto")))
	if _arg("--shop", "") != "":
		run.open_shop(int(_arg("--shop")), _arg("--charge", "0") == "1")
	var weapon_ui: WeaponMenuUi = null
	if _arg("--weapon-menu", "") != "":
		weapon_ui = WeaponMenuUi.new()
		run.ui_layer.add_child(weapon_ui)
		weapon_ui.setup(models, null)
	print("TOWN ", run.scene.get_info())
	if _arg("--cam", "") != "":
		var c := _arg("--cam").split(",")
		run.walk.set_free_camera(Vector3(float(c[0]), float(c[1]), float(c[2])), Vector3(float(c[3]), float(c[4]), float(c[5])))
	var prefix := _arg("--out", "")
	if prefix == "":
		return
	var done := 0
	var edit_part := int(_arg("--edit-part", "-1"))   # choose a Georama part from the board at tick --edit-at
	var edit_at := int(_arg("--edit-at", "5"))
	var keys: Array = []   # "tick:key" pairs for the Georama menus
	for k in _arg("--keys", "").split(",", false):
		var f := k.split(":")
		keys.append([int(f[0]), f[1]])
	for s in _arg("--shots", "0").split(",", false):
		var target := int(s)
		var events: Array = []
		if edit_part >= 0 and done < edit_at and edit_at <= target:
			events.append([edit_at, ""])
		for k in keys:
			if k[0] > done and k[0] <= target:
				events.append(k)
		events.sort_custom(func(a, b): return a[0] < b[0])
		for e in events:
			run.advance(e[0] - done)
			done = e[0]
			if e[1] == "":
				print("SELECT ", run.walk.georama_select_part(edit_part), " ", run.walk.get_edit_ui())
			elif e[1].begins_with("act") and run.shop_ui != null:
				run.shop_ui._activate(int(e[1].substr(3)))
			else:
				var ev := InputEventKey.new()
				ev.keycode = OS.find_keycode_from_string(e[1])
				ev.pressed = true
				if weapon_ui != null and is_instance_valid(weapon_ui):
					weapon_ui._unhandled_key_input(ev)
				elif run.shop_ui != null:
					run.shop_ui._unhandled_key_input(ev)
				else:
					run.edit_ui.handle_key(ev)
				run.advance(0)
		run.advance(target - done)
		done = target
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_t%d.png" % [prefix, target])
		print("SHOT ", target, " mode ", run.walk.get_mode(), " player ", run.walk.get_player_position(), " near ", run.walk.get_near_event(), " ui ", run.walk.get_ui())
		var ebs: Dictionary = run.walk.get_eb_ui()
		if not ebs.is_empty():
			print("  EB active ", ebs.get("active"), " count ", ebs.get("count"), " hit ", ebs.get("key_count"), "/", ebs.get("keys", []).size(), " result ", ebs.get("result"), " finish ", ebs.get("finish"), " intro ", ebs.get("intro"))
		if _arg("--town-events", "") != "":
			for e in run.walk.get_town_events():
				print("  TEV ", e)
		if _arg("--villagers", "") != "":
			for v in run.walk.get_villagers():
				print("  VIL ", v)
	get_tree().quit()
