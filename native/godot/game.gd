extends Node
# The game shell: title screen, a village hub (stand-in for the Georama town until that is ported: rest, items, weapons, saving and the way into the dungeons)
# and the dungeon runs (DungeonRun). Options after "--":  --iso <path to the user's ISO>

const SAVE_PATH := "user://autosave.dc"   # (the automatic save of a visit to a dungeon; the slots are in save_ui.gd)
var DEFAULT_ISO := ProjectSettings.globalize_path("res://../../iso/Dark Cloud (USA).iso")
# the dungeons in story order with the system-text id of their name
const DUNGEONS := [[0, 11], [1, 13], [2, 16], [3, 23], [4, 21], [5, 22], [6, 25]]

var models := DcModels.new()
var ui: CanvasLayer
var page: Control
var run: DungeonRun
var town: TownRun
var note := ""
var town_name := "e01"
var came_from_town := false
var sound: DcSound

func _arg(name: String, default: String = "") -> String:
	var a := OS.get_cmdline_user_args()
	var i := a.find(name)
	return a[i + 1] if i >= 0 and i + 1 < a.size() else default

func _ready() -> void:
	if not models.open_iso(_arg("--iso", DEFAULT_ISO)):
		push_error("Cannot open the game disc. Start with: -- --iso <path to your Dark Cloud (USA) ISO>")
		get_tree().quit(1)
		return
	sound = models.build_sound()
	if sound != null:
		add_child(sound)
	ui = CanvasLayer.new()
	add_child(ui)
	var bg := ColorRect.new()
	bg.color = Color(0.05, 0.05, 0.1)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	ui.add_child(bg)
	if _arg("--coop", "") != "":   # --coop host | join:<ip> [--port n] [--pname name]: start (or join) a co-op session before the shell opens
		var cn := CoopNet.create(self)
		await cn.wait_ready()
		cn.event_received.connect(_on_coop_event)   # (before the connection: the host may send the running dungeon at once)
		var cport := int(_arg("--port", str(CoopNet.DEFAULT_PORT)))
		if _arg("--coop") == "host":
			cn.host_game(cport, _arg("--pname", "Host"))
		else:
			cn.join_game(_arg("--coop").substr(5), cport, _arg("--pname", "Guest"))
			await _wait_connected(cn)
		if _arg("--character", "") != "":
			models.set_character(int(_arg("--character")))
	if _arg("--town", "") != "":
		models.new_game(1)
		_enter_town(_arg("--town"), int(_arg("--event", "-1")))
	elif _arg("--village", "") != "":
		models.new_game(1)
		_show_village()
	elif _arg("--dungeons", "") != "":
		models.new_game(1)
		_show_dungeons()
	else:
		_show_title()
	if _arg("--coop-page", "") != "":
		_show_coop()
	if _arg("--live-shots", "") != "":   # real time harness: "seconds:path,seconds:path" screenshots, then quit; --auto-dungeon "d,f" --auto-after s enters a dungeon from the town
		var started := Time.get_ticks_msec()
		var shots: PackedStringArray = _arg("--live-shots").split(",", false)
		var auto_done := _arg("--auto-dungeon", "") == ""
		for sp in shots:
			var parts := sp.split("@")
			var until := float(parts[0]) * 1000.0
			while float(Time.get_ticks_msec() - started) < until:
				await get_tree().process_frame
				if not auto_done and float(Time.get_ticks_msec() - started) > float(_arg("--auto-after", "5")) * 1000.0:
					auto_done = true
					var df := _arg("--auto-dungeon").split(",")
					_enter_dungeon(int(df[0]), int(df[1]))
			await RenderingServer.frame_post_draw
			get_viewport().get_texture().get_image().save_png(parts[1])
			var cn2 := CoopNet.find(get_tree())
			print("LIVE ", parts[0], " in ", "dungeon" if run != null else ("town" if town != null else "menu"), " players ", cn2.player_count() if cn2 != null else 1)
		get_tree().quit()
		return
	if _arg("--shot", "") != "":    # screenshot of the current page, then quit (for checking the UI)
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(_arg("--shot"))
		get_tree().quit()

# ---------------------------------------------------------------------------------------------------------------------------- helpers
func _clear_page() -> void:
	if page != null:
		page.queue_free()
		page = null

func _new_page(title: String) -> VBoxContainer:
	_clear_page()
	page = CenterContainer.new()
	page.set_anchors_preset(Control.PRESET_FULL_RECT)
	ui.add_child(page)
	var box := VBoxContainer.new()
	box.custom_minimum_size = Vector2(560, 0)
	box.add_theme_constant_override("separation", 10)
	page.add_child(box)
	var l := Label.new()
	l.text = title
	l.add_theme_font_size_override("font_size", 44)
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(l)
	return box

func _button(box: Control, text: String, cb: Callable, disabled := false) -> Button:
	var b := Button.new()
	b.text = text
	b.disabled = disabled
	b.add_theme_font_size_override("font_size", 26)
	b.pressed.connect(cb)
	box.add_child(b)
	return b

func _label(box: Control, text: String, size := 22) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(l)
	return l

func _focus_first(box: Control) -> void:
	for c in box.get_children():
		if c is Button and not c.disabled:
			c.grab_focus()
			return

# -------------------------------------------------------------------------------------------------------------------------------- title
func _show_title() -> void:
	_clear_page()
	page = Control.new()
	page.set_anchors_preset(Control.PRESET_FULL_RECT)
	ui.add_child(page)
	# the title art of the disc: the cloud scape behind the logo (titledat/title.pak)
	var clouds := models.get_pack_texture("titledat/title.pak", "logo")
	if clouds != null:
		var atlas := AtlasTexture.new()
		atlas.atlas = clouds
		atlas.region = Rect2(0, 0, 420, 245)
		var bg := TextureRect.new()
		bg.texture = atlas
		bg.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		bg.stretch_mode = TextureRect.STRETCH_SCALE
		bg.set_anchors_preset(Control.PRESET_FULL_RECT)
		page.add_child(bg)
	var logo_tex := models.get_pack_texture("titledat/title.pak", "main")
	if logo_tex != null:
		var logo := TextureRect.new()
		logo.texture = logo_tex
		logo.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		logo.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		logo.set_anchors_preset(Control.PRESET_TOP_WIDE)
		logo.offset_top = 30
		logo.offset_bottom = 330
		page.add_child(logo)
	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	box.position = Vector2(0, 0)
	box.custom_minimum_size = Vector2(360, 0)
	box.add_theme_constant_override("separation", 10)
	box.offset_left = -180
	box.offset_right = 180
	box.offset_top = -260
	box.offset_bottom = -40
	page.add_child(box)
	var has_save := false
	for p in SaveUi.SLOTS + [SaveUi.AUTOSAVE]:
		has_save = has_save or FileAccess.file_exists(p)
	_button(box, "New Game", _new_game)
	_button(box, "Continue", _continue_game, not has_save)
	var net := CoopNet.find(get_tree())
	_button(box, "Co-op" if net == null or not net.active else "Co-op (%d players)" % net.player_count(), _show_coop)
	_button(box, "Quit", func(): get_tree().quit())
	_focus_first(box)

# ---------------------------------------------------------------------------------------------------------------------------------- co-op
func _show_coop() -> void:
	var net := CoopNet.create(self)
	await net.wait_ready()
	if not net.event_received.is_connected(_on_coop_event):
		net.event_received.connect(_on_coop_event)
	var box := _new_page("Co-op")
	var names := ["Red", "Blue", "Green", "Yellow"]
	if net.active:
		_label(box, "You are player %d (%s).%s" % [net.my_index + 1, names[clampi(net.my_index, 0, 3)], "  You are the host." if net.is_host else ""])
		if net.is_host:
			var addr_label := _label(box, "Friends on your network join with this PC's address, port %d (UDP)." % CoopNet.DEFAULT_PORT, 18)
			if net.external_address != "":
				addr_label.text = "Friends on the internet join with %s (port %d)." % [net.external_address, CoopNet.DEFAULT_PORT]
			else:   # (opening a port changes the router's settings: only on request)
				_button(box, "Let friends on the internet join (asks the router to open port %d)" % CoopNet.DEFAULT_PORT, func():
					addr_label.text = "Asking the router (UPnP) ..."
					net.upnp_done.connect(func(a: String):
						if is_instance_valid(addr_label):
							addr_label.text = ("Friends on the internet join with %s (port %d). The router opens it for 6 hours." % [a, CoopNet.DEFAULT_PORT]) if a != "" else ("No router answered: forward UDP port %d to this PC yourself." % CoopNet.DEFAULT_PORT), CONNECT_ONE_SHOT)
					net.try_upnp())
		for p in net.roster:
			_label(box, "Player %d: %s" % [int(net.roster[p]["index"]) + 1, net.roster[p]["name"]], 20)
		_button(box, "Leave the co-op game", func():
			net.stop()
			_show_title())
		_button(box, "Back", _show_title)
		_focus_first(box)
		return
	var name_edit := LineEdit.new()
	name_edit.text = OS.get_environment("USERNAME") if OS.get_environment("USERNAME") != "" else "Player"
	name_edit.placeholder_text = "Your name"
	name_edit.add_theme_font_size_override("font_size", 24)
	box.add_child(name_edit)
	_button(box, "Host a game (port %d)" % CoopNet.DEFAULT_PORT, func():
		var err := net.host_game(CoopNet.DEFAULT_PORT, name_edit.text)
		note = "Hosting." if err == OK else "Could not host (error %d)." % err
		_show_coop())
	var ip_edit := LineEdit.new()
	ip_edit.text = "127.0.0.1"
	ip_edit.placeholder_text = "Host address"
	ip_edit.add_theme_font_size_override("font_size", 24)
	box.add_child(ip_edit)
	var status := _label(box, "")
	_button(box, "Join that game", func():
		var err := net.join_game(ip_edit.text.strip_edges(), CoopNet.DEFAULT_PORT, name_edit.text)
		if err != OK:
			status.text = "Could not connect (error %d)." % err
			return
		status.text = "Connecting ..."
		var ok: bool = await _wait_connected(net)
		if ok:
			_show_coop()
		else:
			net.stop()
			status.text = "The host did not answer.")
	_button(box, "Back", _show_title)
	_focus_first(box)

func _wait_connected(net: CoopNet) -> bool:
	var waited := 0.0
	while net.active and waited < 8.0:
		if net.roster.has(net.my_peer()):
			return true
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	return false

func _new_game() -> void:
	models.new_game(Time.get_ticks_usec() & 0x7fffffff)
	note = "A new adventure begins."
	_clear_page()
	var opening := OpeningRun.new()
	add_child(opening)
	if opening.start(models, sound):
		await opening.finished
		if sound != null:
			sound.stop_bgm(20)
	opening.queue_free()
	var name_ui := ModalUi.new()   # after the opening Toan is named (InitNameRegist(0))
	ui.add_child(name_ui)
	name_ui.setup(models, 6, [0])
	await name_ui.done
	_enter_town("e01", -1)

func _continue_game() -> void:
	_clear_page()
	var load_ui := SaveUi.new()
	ui.add_child(load_ui)
	load_ui.setup(models, false)
	load_ui.done.connect(func(path: String):
		if path == "":
			_show_title()
			return
		note = "Game loaded."
		_enter_town("e%02d" % (clampi(models.get_last_town(), 0, 4) + 1), -2))

# ------------------------------------------------------------------------------------------------------------------------------ village
func _input(event: InputEvent) -> void:
	if town != null and event is InputEventKey and event.pressed and not event.echo and (event.keycode == KEY_TAB or event.keycode == KEY_M) and town.visible and not town.is_editing():
		get_viewport().set_input_as_handled()
		ui.visible = true
		town.set_active(false)
		_show_village()

func _show_village() -> void:
	var box := _new_page("Norune Village" if town == null else "Menu")
	var st := models.get_village_status()
	_label(box, "Life %d / %d     Water %d%%     Gold %d     Weapon: %s" % [st["life"], st["max_life"], int(100.0 * st["water"] / maxf(st["max_water"], 1.0)), st["gold"], models.get_item_name(int(st["weapon_id"]))])
	if note != "":
		_label(box, note, 20)
		note = ""
	if town != null:
		_button(box, "Return to the town", _return_to_town)
	else:
		_button(box, "Enter a dungeon", _show_dungeons)
	_button(box, "Items", func(): _show_items())
	_button(box, "Weapons", func(): _show_weapons())
	if town != null and town.walk != null and town.walk.can_edit():
		_button(box, "Georama Parts", func():
			var atla := AtlaViewUi.new()
			ui.add_child(atla)
			atla.setup(town.walk)
			atla.closed.connect(_show_village))
	if town == null:
		_button(box, "Rest at the inn", func():
			models.rest()
			note = "You feel refreshed."
			_show_village())
	_button(box, "Options", func():
		var opt := OptionUi.new()
		ui.add_child(opt)
		opt.setup(models)
		opt.closed.connect(_show_village))
	_button(box, "Save", func():
		var save_ui := SaveUi.new()
		ui.add_child(save_ui)
		save_ui.setup(models, true)
		save_ui.done.connect(func(path: String):
			if path != "":
				note = "Game saved."
			_show_village()))
	_button(box, "Title", _show_title)
	_focus_first(box)

func _show_dungeons() -> void:
	var box := _new_page("Where to?")
	for d in DUNGEONS:
		var deepest: int = models.get_deepest_floor(d[0])
		var text: String = models.get_system_text(d[1])
		if deepest >= 0:
			text += "   (floor %d reached)" % (deepest + 1)
		_button(box, text, _show_floors.bind(d[0]))
	_button(box, "Back", _show_village)
	_focus_first(box)

func _show_floors(dungeon: int) -> void:
	var box := _new_page(models.get_system_text(DUNGEONS[dungeon][1]))
	var deepest: int = models.get_deepest_floor(dungeon)
	var last := mini(deepest + 1, 30)
	_label(box, "Which floor do you want to start from?")
	for f in range(last + 1):
		_button(box, "Floor %d" % (f + 1), _enter_dungeon.bind(dungeon, f))
	_button(box, "Back", _return_to_town if town != null else _show_dungeons)
	_focus_first(box)

func _show_items() -> void:
	var box := _new_page("Items")
	var list := ItemList.new()
	list.custom_minimum_size = Vector2(560, 360)
	list.add_theme_font_size_override("font_size", 24)
	var slots: Array = []
	for it in models.get_bag():
		slots.append(it["slot"])
		list.add_item(models.get_item_name(int(it["id"])), models.get_item_icon(int(it["id"])))
	box.add_child(list)
	list.gui_input.connect(func(ev):   # co-op: D drops the selected item for the other players
		if ev is InputEventKey and ev.pressed and ev.keycode == KEY_D and town != null and town.coop != null:
			var sel := list.get_selected_items()
			if not sel.is_empty() and town.coop.drop_item(int(slots[sel[0]])):
				note = "Dropped it for the others."
				_show_items())
	list.item_activated.connect(func(i):
		var r := models.use_bag_item(int(slots[i]))
		note = "Used it." if r.get("used", false) else "That cannot be used here."
		_show_items())
	_label(box, "Enter: use the item.%s %s" % ["  D: drop it for the others." if town != null and town.coop != null else "", note], 18)
	note = ""
	_button(box, "Back", _show_village)
	list.grab_focus()

func _show_weapons() -> void:
	var wm := WeaponMenuUi.new()
	ui.add_child(wm)
	wm.setup(models, null)
	wm.closed.connect(_show_village)
	return
	var box := _new_page("Weapons")
	var list := ItemList.new()
	list.custom_minimum_size = Vector2(560, 360)
	list.add_theme_font_size_override("font_size", 24)
	var slots: Array = []
	for w in models.get_weapons():
		slots.append(w["slot"])
		list.add_item("%s%s  +%d   %.0f/%d" % ["> " if w["current"] else "  ", models.get_item_name(int(w["id"])), w["level"], w["durability"], w["max_durability"]], models.get_item_icon(int(w["id"])))
	box.add_child(list)
	list.item_activated.connect(func(i):
		models.equip_weapon(int(slots[i]))
		_show_weapons())
	_label(box, "Enter: equip the weapon.", 18)
	_button(box, "Back", _show_village)
	list.grab_focus()

# ----------------------------------------------------------------------------------------------------------------------------- dungeon
# co-op: whoever picks a floor asks the host; the host builds the common seed and tells everybody (including itself) to go
func _enter_dungeon(dungeon: int, floor_index: int) -> void:
	var net := CoopNet.find(get_tree())
	if net != null and net.active and run == null:
		if net.is_host:
			var data := {"dungeon": dungeon, "floor": floor_index, "seed": models.draw_coop_seed(), "status": models.get_status_bytes()}
			net.send_event("enter_dungeon", data)
			_start_dungeon(dungeon, floor_index, data)
		else:
			net.send_event_to_host("enter_request", {"dungeon": dungeon, "floor": floor_index})
			note = "Waiting for the host ..."
		return
	_start_dungeon(dungeon, floor_index, {})

# a guest walks in the host's town: it asks for the host's Georama layout (the host answers from its save)
var _town_layouts := {}
func _coop_fetch_town(net: CoopNet, town_index: int) -> void:
	if net.is_host or town_index < 0 or town_index > 5:
		models.clear_town_layout_overrides()
		return
	_town_layouts.erase(town_index)
	net.send_event_to_host("town_layout_request", {"town": town_index})
	var waited := 0.0
	while not _town_layouts.has(town_index) and waited < 4.0:
		await get_tree().create_timer(0.05).timeout
		waited += 0.05
	if _town_layouts.has(town_index):
		models.set_town_layout_override(town_index, _town_layouts[town_index])

func _on_coop_event(_from: int, kind: String, data: Dictionary) -> void:
	var net := CoopNet.find(get_tree())
	if _arg("--coop-debug", "") != "":
		print("COOP event ", kind, " from ", _from)
	match kind:
		"town_layout_request":   # (host)
			if net != null and net.is_host:
				net.send_event_to(_from, "town_layout", {"town": int(data["town"]), "bytes": models.get_town_layout(int(data["town"]))})
		"town_layout":
			_town_layouts[int(data["town"])] = data["bytes"]
		"town_edited":   # (guest) the host rebuilt something in the town we are in
			if net != null and not net.is_host and town != null and run == null and town_name == String(data["town"]):
				_enter_town(town_name, -2)
		"enter_request":   # (host)
			if net != null and net.is_host and run == null:
				_enter_dungeon(int(data["dungeon"]), int(data["floor"]))
		"enter_dungeon":
			if run == null:
				_start_dungeon(int(data["dungeon"]), int(data["floor"]), data)

func _start_dungeon(dungeon: int, floor_index: int, coop_data: Dictionary) -> void:
	_clear_page()
	came_from_town = town != null
	if town != null:
		town.queue_free()
		town = null
	ui.visible = false
	run = DungeonRun.new()
	add_child(run)
	run.left.connect(_on_dungeon_left)
	var run_opts := {"sound": sound}
	if not coop_data.is_empty():
		run_opts["coop_seed"] = coop_data["seed"]
		run_opts["coop_status"] = coop_data["status"]
		run_opts["coop_back"] = bool(coop_data.get("back", false))
	if not run.start(models, dungeon, floor_index, "dun/mainchara/c01d.chr", run_opts):
		run.queue_free()
		run = null
		ui.visible = true
		note = "The dungeon could not be built."
		_show_village()

func _on_dungeon_left(reason: String) -> void:
	models.clear_coop_floor()
	models.save_game(ProjectSettings.globalize_path(SAVE_PATH))
	var jump: Dictionary = models.get_clear_jump() if reason == "cleared" else {}
	run.queue_free()
	run = null
	ui.visible = true
	if reason == "cleared" and not jump.is_empty():   # a boss was beaten: on to the town or story map the story names
		if jump.has("floor"):   # (dungeon 6: the second boss follows at once)
			_enter_dungeon(int(jump["dungeon"]), int(jump["floor"]))
			return
		_on_map_requested(int(jump["map"]), int(jump["event"]))
		return
	note = "You were knocked out and lost half of your gold." if reason == "game_over" else "You escaped from the dungeon."
	if came_from_town:
		_enter_town(town_name, -2)
	else:
		_show_village()

# ---------------------------------------------------------------------------------------------------------------------------------- town
func _enter_town(name: String, event: int) -> void:
	var net := CoopNet.find(get_tree())
	if net != null and net.active and name.length() == 3 and name.begins_with("e"):
		await _coop_fetch_town(net, int(name.substr(1)) - 1)
	_clear_page()
	if town != null:
		town.queue_free()
		town = null
	ui.visible = false
	town_name = name
	if name.length() == 3 and name.begins_with("e"):
		models.set_last_town(int(name.substr(1)) - 1)
	town = TownRun.new()
	add_child(town)
	var opts := {"event": event, "complete": false, "sound": sound}
	if not town.start(models, name, opts):
		town.queue_free()
		town = null
		ui.visible = true
		note = "The town could not be built."
		_show_village()
		return
	town.dungeon_requested.connect(_on_dungeon_gate)
	town.map_requested.connect(_on_map_requested)
	town.edit_finished.connect(_on_town_edited)

# co-op: the host left the Georama edit mode; everybody else in that town gets the new buildings
func _on_town_edited() -> void:
	var net := CoopNet.find(get_tree())
	if net != null and net.active and net.is_host and town_name.length() == 3 and town_name.begins_with("e"):
		net.send_event("town_edited", {"town": town_name})

func _on_dungeon_gate(dungeon: int, floor_index: int) -> void:
	ui.visible = true
	town.set_active(false)
	_show_floors(clampi(dungeon, 0, 6))

func _on_map_requested(map_no: int, event: int) -> void:
	var name := ""
	if map_no >= 1 and map_no <= 9:
		name = "e%02d" % map_no
	elif map_no >= 12:
		name = "s%02d" % (map_no - 11)
	if name != "" and models.has_file("gedit/%s/mapinfo.cfg" % name):
		_enter_town(name, event)

func _return_to_town() -> void:
	if town != null:
		town.set_active(true)
		ui.visible = false
		_clear_page()
	else:
		_show_village()
