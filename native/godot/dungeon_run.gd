class_name DungeonRun
extends Node3D
# One visit to a dungeon: floor after floor (random floors only for now), the HUD, the pause menu (items, weapons, status, leaving) and game over.
# The player record and the random generator live in the DcModels session; this node builds the floors, runs them and reports how the visit ended
# through `left("escape" | "game_over")`.
#   interactive: WASD/arrows = left stick, IJKL = right stick (camera), Q/E = turn camera, R = camera behind, Space/Enter/F = action (attack, open chests),
#                Tab/Escape/Start = menu

signal left(reason: String)

const PRESETS := {
	"up": [128, 0], "down": [128, 255], "left": [0, 128], "right": [255, 128], "none": [128, 128],
	"upleft": [30, 30], "upright": [226, 30], "downleft": [30, 226], "downright": [226, 226],
	"attack": [128, 128, 64], "upattack": [128, 0, 64], "use": [128, 128, 128], "lock": [128, 128, 32], "guard": [128, 128, 8],
	# Georama edit mode: Select, Triangle, D-pad (menu bits) and L2 / R2
	"select": [128, 128, 0, 0x100], "tri": [128, 128, 0, 0x10], "dup": [128, 128, 0, 0x1000], "ddown": [128, 128, 0, 0x4000], "dleft": [128, 128, 0, 0x8000], "dright": [128, 128, 0, 0x2000],
	"l2": [128, 128, 1], "r2": [128, 128, 2], "cross": [128, 128, 64], "square": [128, 128, 128], "circle": [128, 128, 32],
}

var models: DcModels
var walker: DcWalker
var dungeon := 0
var floor_no := 0
var char_path := "dun/mainchara/c01d.chr"
var floor_nodes: Array[Node] = []
var message_label: Label
var hud_label: Label
var hud: DungeonHud
var sound: DcSound
var messages: Array = []   # [text, ticks left]
var scripted := false
var script_ticks: Array = []
var verbose := false
var opts: Dictionary = {}
var _dead_ticks := 0
var _ok_was_down := false
var _finished := false
var boss_down: Array = []   # [dungeon, floor] of the boss that fell (co-op tells the other players)
var coop: CoopDungeon   # co-op: the other players (null when playing alone)

# ---- pause menu
var pick_layer: CanvasLayer      # the window of _ITEM_USE_WINDOW (which item to use here?)
var pick_ids: Array = []
var menu_layer: CanvasLayer
var menu_list: ItemList
var menu_title: Label
var menu_info: Label
var menu_open := false
var menu_page := 0   # 0 items, 1 weapons, 2 status, 3 party, 4 leave
var menu_entries: Array = []

func start(session: DcModels, dungeon_no: int, floor_index: int, character: String = "dun/mainchara/c01d.chr", options: Dictionary = {}) -> bool:
	models = session
	dungeon = dungeon_no
	floor_no = floor_index
	char_path = character
	opts = options
	verbose = opts.get("verbose", false)
	sound = opts.get("sound", null)
	_make_hud()
	_make_event_ui()
	if opts.has("script"):
		script_ticks = opts["script"]
		scripted = true
	models.enter_dungeon_reset()
	var net := CoopNet.find(get_tree())
	if net != null and net.active:   # co-op: the floor is built from the host's seed and record so that everybody gets the same one
		models.set_coop_floor(int(opts.get("coop_seed", 0)), opts.get("coop_status", PackedByteArray()))
		coop = CoopDungeon.new()
		net.session_closed.connect(func(): if not _finished: _finish("escape"))   # the host is gone: nobody simulates the monsters any more
		hud.other_players = func() -> Array: return coop.other_players()
		coop.floor_data = {"seed": int(opts.get("coop_seed", 0)), "status": opts.get("coop_status", PackedByteArray())}
		add_child(coop)
		coop.setup(self, net)
	if not _start_floor(floor_no):
		return false
	if opts.get("coop_back", false) and walker.has_back_floor():   # a guest who comes late finds the others on the back floor
		walker.coop_swap_floor()
	if opts.has("menu"):    # open the menu on a page (for screenshots)
		_menu_toggle()
		menu_page = int(opts["menu"])
		_menu_refresh()
	return true

# parses "up:120,attack:3" into pad ticks
static func parse_script(text: String) -> Array:
	var ticks: Array = []
	for part in text.split(",", false):
		var f := part.split(":")
		var preset: Array = PRESETS.get(f[0], [128, 128])
		for i in int(f[1]):
			ticks.append([preset[0], preset[1], 128, 128, preset[2] if preset.size() > 2 else 0, preset[3] if preset.size() > 3 else 0])
	return ticks

# the floor's map file comes from the dungeon's own event script; story ("inter") and boss maps are not playable yet and use the nearest random map
func _map_for(floor_index: int) -> String:
	var choice := models.get_floor_map(dungeon, floor_index)
	var kind: int = choice.get("kind", 1)
	if choice.get("name", "") != "":   # (kind 0: story room, 1: random floor, 2: boss room: the last two are real maps now)
		return choice["name"]
	var f := floor_index
	while f > 0:
		f -= 1
		var c := models.get_floor_map(dungeon, f)
		if c.get("kind", 1) == 1 and c.get("name", "") != "":
			return c["name"]
	return "d%02dmain_a" % (dungeon + 1)

func _start_floor(n: int) -> bool:
	if walker != null:
		models.end_floor(walker)
		walker.queue_free()
		for node in floor_nodes:
			node.queue_free()
		floor_nodes.clear()
	floor_no = n
	if hud != null:
		hud.reset_slide()
	var stem := _map_for(n)
	var kind: int = models.get_floor_map(dungeon, n).get("kind", 1)
	walker = models.start_floor(stem, dungeon, n, 6, char_path)
	if walker == null:
		print("BUILD_FAILED")
		return false
	hud.set_map(walker, "dun/mpd_pack/%s.mpd" % stem)
	hud.back_dungeon = false
	_start_sound()
	models.save_game(ProjectSettings.globalize_path(SaveUi.AUTOSAVE))   # (the real game only saves in town; this is a safety copy that Continue offers)
	if verbose:
		print("FLOOR ", n, " ", stem, " kind ", kind, " ", models.get_last_info())
	add_child(walker)
	if opts.get("manual", false):
		walker.set_physics_process(false)   # the caller steps the simulation with advance()
	_setup_environment(walker.get_floor().get_environment())
	walker.set_auto_item_pick(opts.get("auto_pick", false))
	walker.get_camera().make_current()
	if scripted:
		walker.set_script_ticks(script_ticks)
		walker.set_use_devices(false)
	if coop != null:
		coop.on_floor_started()
	if opts.has("teleport") and n == int(opts.get("first_floor", n)):
		var t: Array = opts["teleport"]
		walker.teleport(t[0], t[1], t[2] if t.size() > 2 else 30.0)
	elif opts.has("to_marker") and n == int(opts.get("first_floor", n)):
		for m in walker.get_event_info()["markers"]:
			if int(m["event"]) == int(opts["to_marker"]):
				walker.teleport(m["pos"].x, m["pos"].z)
				break
	elif opts.has("to_chest") and n == int(opts.get("first_floor", n)):
		var chests: Array = walker.get_event_info()["chests"]
		var c: Dictionary = chests[int(opts["to_chest"])]
		walker.teleport(c["pos"].x, c["pos"].z + 8.0)
	if opts.get("events", false):
		var info := walker.get_event_info()
		for c in info["chests"]: print("CHEST ", c)
		for a in info["atras"]: print("ATRA ", a)
		for m in info["markers"]: print("MARKER ", m)
		for mo in walker.get_monster_info(): print("MONSTER ", mo)
	return true

# advances the simulation by ticks (for tests and screenshots; the walker's own physics process is switched off by the caller)
func advance(ticks: int) -> void:
	var remaining := ticks
	while remaining > 0:
		var chunk := mini(remaining, 30)
		walker.step_ticks(chunk)
		for i in chunk:
			hud.tick()
		remaining -= chunk
		_handle_notices()
		if walker.is_exit_requested():
			_floor_exit()

func _physics_process(_delta: float) -> void:
	if walker == null or _finished:
		return
	_flicker_torches()
	hud.tick()
	if sound != null:
		var cam := walker.get_camera()
		sound.set_listener(cam.global_position, -cam.global_transform.basis.z)
	_handle_notices()
	_update_event_ui()
	var st := walker.get_player_status()
	if st.get("game_over", false) and coop != null:   # co-op: nobody is thrown out of the floor; he gets up at the entrance a few seconds later
		_dead_ticks += 1
		if _dead_ticks > 240:
			_dead_ticks = 0
			walker.coop_revive()
			_say("Back on your feet!", 120)
		return
	if st.get("game_over", false):
		_dead_ticks += 1
		var ok := Input.is_physical_key_pressed(KEY_SPACE) or Input.is_physical_key_pressed(KEY_ENTER) or Input.is_physical_key_pressed(KEY_F) or Input.is_joy_button_pressed(0, JOY_BUTTON_A)
		if _dead_ticks > 90 and ok and not _ok_was_down:
			_dead_ticks = 0
			_finish("game_over")
		_ok_was_down = ok
		return
	_dead_ticks = 0
	if walker.is_exit_requested():
		_floor_exit()
	for m in messages:
		m[1] -= 1
	messages = messages.filter(func(m): return m[1] > 0)
	var text := ""
	for m in messages:
		text += m[0] + "\n"
	message_label.text = text

# co-op: the host ended the dungeon for everybody (a boss fell, or it left)
func coop_end(reason: String, boss: Array, jump: Dictionary) -> void:
	if _finished:
		return
	if reason == "cleared":
		if boss.size() == 2:
			models.boss_cleared(int(boss[0]), int(boss[1]))   # (the story of this player's own save moves on too)
		elif jump.has("map"):
			models.set_clear_jump(int(jump["map"]), int(jump["event"]))
		_finish("cleared")
	else:
		_finish("escape")

# the stairs were taken: alone the next floor is built at once; in co-op the host decides for everybody (CoopDungeon)
func _floor_exit() -> void:
	if coop != null:
		coop.request_floor_exit()
		walker.clear_exit_request()
		return
	_start_floor(floor_no + 1)

# co-op: the host's next floor (its seed and record); every player builds the same one
func coop_next_floor(seed: int, status: PackedByteArray) -> void:
	models.set_coop_floor(seed, status)
	if coop != null:
		coop.floor_data = {"seed": seed, "status": status}
	_start_floor(floor_no + 1)

# the dungeon's sound set and music (selectMapNo: BGM 100 + n, SOUND_SET 100 + n; the voices of the character in play)
var _sound_dungeon := -1
func _start_sound() -> void:
	if sound == null or _sound_dungeon == dungeon:
		return
	_sound_dungeon = dungeon
	sound.load_sound_set(100 + dungeon)
	sound.load_bgm(100 + dungeon)
	sound.play_bgm(0)
	sound.load_voice(walker.get_character())

func _finish(reason: String) -> void:
	if sound != null:
		sound.stop_bgm(20)
	_finished = true
	if reason == "game_over":
		models.game_over(walker)
	else:
		models.end_floor(walker)
	if coop != null:
		coop.on_finish(reason)
	left.emit(reason)

# ---- the story events of the dungeon script: their message windows and fades (the scenes themselves are not drawn)
var ev_layer: CanvasLayer
var eb_overlay: EbOverlay
var ev_fade: ColorRect
var ev_panel: Panel
var ev_label: Label

func _make_event_ui() -> void:
	ev_layer = CanvasLayer.new()
	ev_layer.layer = 15
	add_child(ev_layer)
	eb_overlay = EbOverlay.new()   # (the EB duel's lane and markers, below the fades and windows)
	ev_layer.add_child(eb_overlay)
	eb_overlay.setup(models)
	ev_fade = ColorRect.new()
	ev_fade.set_anchors_preset(Control.PRESET_FULL_RECT)
	ev_fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	ev_fade.color = Color(0, 0, 0, 0)
	ev_layer.add_child(ev_fade)
	ev_panel = Panel.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.05, 0.18, 0.88)
	style.border_color = Color(0.9, 0.9, 1.0, 0.9)
	style.set_border_width_all(2)
	style.set_corner_radius_all(10)
	ev_panel.add_theme_stylebox_override("panel", style)
	ev_panel.anchor_left = 0.12
	ev_panel.anchor_right = 0.88
	ev_panel.anchor_top = 0.72
	ev_panel.anchor_bottom = 0.95
	ev_panel.visible = false
	ev_layer.add_child(ev_panel)
	ev_label = Label.new()
	ev_label.set_anchors_preset(Control.PRESET_FULL_RECT)
	ev_label.offset_left = 18
	ev_label.offset_top = 12
	ev_label.offset_right = -18
	ev_label.offset_bottom = -12
	ev_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	ev_label.add_theme_font_size_override("font_size", 24)
	ev_panel.add_child(ev_label)

func _update_event_ui() -> void:
	if eb_overlay != null and walker != null:
		eb_overlay.update_from(walker.get_eb_ui())
	if ev_layer == null:
		return
	var ev: Dictionary = walker.get_event_ui()
	var running: bool = ev.get("running", false)
	var alpha: float = ev.get("fade_alpha", 0.0)
	var col: Color = ev.get("fade_color", Color.BLACK)
	ev_fade.color = Color(col.r, col.g, col.b, alpha)
	var text := ""
	for w in ev.get("windows", []):
		text += str(w["text"]) + ("  v" if w.get("waiting", false) else "") + "
"
	if text == "" and ev.has("system_message"):
		text = str(ev["system_message"])
	ev_label.text = text.replace("\n", "
")
	ev_panel.visible = text != "" and running

func _say(text: String, ticks: int = 180) -> void:
	messages.append([text, ticks])
	if verbose:
		print("MSG ", text)
	if messages.size() > 5:
		messages.pop_front()

# notices from dccore/event_world.h (Notice::Kind)
const NOTICE_TEXT := {
	0: "Got %d", 1: "Got %d", 2: "Got the dungeon map", 3: "Got the magic crystal", 4: "The bag is full (%d stays in the chest)",
	5: "The attach list is full", 6: "No room for another weapon", 7: "Got an Atla", 8: "A gate key is needed", 9: "The gate opened",
	10: "A door key is needed", 11: "The door opened", 12: "", 13: "Only Toan can take this", 14: "",
}

func _handle_notices() -> void:
	hud.add_hits(walker.take_hit_values())
	for n in walker.take_notices():
		var kind: int = n["kind"]
		if kind == 12:
			continue
		if kind == 30:   # the boss is down and the screen has faded out: the clear scene is skipped, the story moves on
			_say("The guardian of the dungeon has fallen.", 120)
			boss_down = [int(n["a"]), int(n["b"])]
			_finish("cleared")
			return
		if kind == 15:   # a chest was a monster
			_say("It was a monster!", 90)
			if coop != null and not coop.net.is_host:   # (the host's monsters: it has to wake it up)
				coop.net.send_event_to_host("mimic", {"slot": int(n["a"])})
			continue
		if kind == 40:   # a story script sent the player to another map (_MAP_JUMP)
			models.set_clear_jump(int(n["a"]), int(n["b"]))
			_finish("cleared")
			return
		if kind == 41:   # the floor swapped with its back floor (_SET_URA_DUNGEON): new lighting, the other floor's map
			hud.back_dungeon = int(n["a"]) == 1
			hud.set_map(walker, "dun/mpd_pack/%s.mpd" % _map_for(floor_no))
			for node in floor_nodes:
				node.queue_free()
			floor_nodes.clear()
			_setup_environment(walker.get_floor().get_environment())
			if coop != null:
				coop.on_ura_swap(int(n["a"]) == 1)
			continue
		if kind == 43:   # _ITEM_USE_WINDOW: the player picks the item to use on the thing in front of him
			_open_item_pick()
			continue
		if kind == 42:   # _SET_FLOOR_TITLE: the banner (it says BACK on the back floor)
			hud.show_title(int(n["a"]) == 1)
			continue
		if kind == 34:   # the EB duel's sounds
			if sound != null:
				match String(n["op"]):
					"_EB_SE": sound.play_se(int(n["a"]), -1.0, 0.0)
					"_EB_BGM_FADE": sound.bgm_fade_out(maxi(int(n["a"]), 1))
					"_EB_FANFARE":
						sound.load_special(int(n["a"]))
						sound.play_special(0)
			continue
		if kind == 33:   # a monster's sound
			if sound != null:
				if n.get("stop", false):
					sound.stop_se(int(n["a"]))
				else:
					sound.play_se_at(int(n["a"]), n["b"])
			continue
		if kind == 31 or kind == 32:   # a sound of the player's actions / a thrown item went off
			if sound != null:
				if kind == 31:
					sound.play_se(int(n["a"]), -1.0, 0.0)
				else:
					var item := int(n["a"])
					if item >= 0xa1 and item <= 0xa5:
						sound.play_se_at(item - 0x3c, n["b"])
					elif item == 0x98:
						sound.play_se_at(0x69, n["b"])
					sound.play_se_at(0x6c, n["b"])
			continue
		if kind == 14:
			var tm: String = models.article_text(300 + int(n["a"]))
			_say(tm if tm != "" else "A strange circle...", 150)
			continue
		if kind == 20:
			_say("%s broke!" % models.get_item_name(int(n["a"])), 150)
			continue
		if kind == 21:
			_say("%s was repaired" % models.get_item_name(int(n["a"])))
			continue
		if kind == 22:
			_say("%s is wearing out" % models.get_item_name(int(n["a"])))
			continue
		var t: String = NOTICE_TEXT.get(kind, "event %d" % kind)
		if "%d" in t:
			t = t.replace("%d", models.get_item_name(int(n["a"])))
		_say(t)

func _setup_environment(e: Dictionary) -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = e.get("background", Color.BLACK)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = e.get("ambient", Color.BLACK)
	env.ambient_light_energy = 1.0
	if e.get("fog_far", 0.0) > 0.0:
		env.fog_enabled = true
		env.fog_mode = Environment.FOG_MODE_DEPTH
		env.fog_depth_begin = e["fog_near"]
		env.fog_depth_end = e["fog_far"]
		env.fog_depth_curve = 1.0
		env.fog_light_color = e["fog_color"]
		env.fog_sky_affect = 1.0
	# the modern pass: filmic tone mapping, a little glow around the torches, ambient occlusion in the corners
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	env.glow_intensity = 0.6
	env.glow_bloom = 0.05
	env.ssao_enabled = true
	env.ssao_radius = 6.0
	env.ssao_intensity = 1.6
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	floor_nodes.append(we)
	for l in e.get("lights", []):
		var d := DirectionalLight3D.new()
		var dir: Vector3 = l["direction"]
		d.look_at_from_position(Vector3.ZERO, -dir.normalized(), Vector3.UP)
		d.light_color = l["color"]
		d.light_energy = 255.0 / 128.0
		if floor_nodes.size() < 3:   # (the first light casts the shadows: characters and pillars on the floor)
			d.shadow_enabled = true
			d.directional_shadow_max_distance = 700.0
			d.shadow_bias = 0.05
			d.shadow_opacity = 0.75
			d.shadow_normal_bias = 1.5
		add_child(d)
		floor_nodes.append(d)

# ---- menu
func _make_menu() -> void:
	menu_layer = CanvasLayer.new()
	menu_layer.layer = 10
	var panel := Panel.new()
	panel.position = Vector2(120, 80)
	panel.size = Vector2(900, 480)
	menu_layer.add_child(panel)
	menu_title = Label.new()
	menu_title.position = Vector2(20, 12)
	menu_title.add_theme_font_size_override("font_size", 28)
	panel.add_child(menu_title)
	menu_list = ItemList.new()
	menu_list.position = Vector2(20, 60)
	menu_list.size = Vector2(540, 400)
	menu_list.add_theme_font_size_override("font_size", 22)
	panel.add_child(menu_list)
	menu_info = Label.new()
	menu_info.position = Vector2(580, 60)
	menu_info.size = Vector2(300, 400)
	menu_info.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	menu_info.add_theme_font_size_override("font_size", 20)
	panel.add_child(menu_info)
	menu_layer.visible = false
	add_child(menu_layer)

const MENU_PAGES := 5

func _menu_refresh() -> void:
	var st := walker.get_player_status()
	menu_list.clear()
	menu_entries.clear()
	match menu_page:
		0:
			menu_title.text = "ITEMS    (Enter: use,  1/2/3: set as quick item,  Left/Right: page,  Tab: close)"
			for it in walker.get_bag():
				menu_entries.append(it)
				menu_list.add_item(models.get_item_name(int(it["id"])), models.get_item_icon(int(it["id"])))
			menu_info.text = "Life %d / %d\nWater %d%%\nGold %d" % [st["life"], st["max_life"], int(100.0 * st["water"] / maxf(st["max_water"], 1.0)), st["gold"]]
		1:
			menu_title.text = "WEAPONS    (Enter: equip)"
			for w in walker.get_weapons():
				menu_entries.append(w)
				var mark := "> " if w["current"] else "  "
				menu_list.add_item("%s%s  +%d   %.0f/%d" % [mark, models.get_item_name(int(w["id"])), w["level"], w["durability"], w["max_durability"]], models.get_item_icon(int(w["id"])))
			menu_info.text = "Equipped weapon: %s" % models.get_item_name(int(st["weapon_id"]))
		2:
			menu_title.text = "STATUS"
			menu_list.add_item("Life   %d / %d" % [st["life"], st["max_life"]])
			menu_list.add_item("Water  %d / %d" % [int(st["water"]), int(st["max_water"])])
			menu_list.add_item("Gold   %d" % st["gold"])
			menu_list.add_item("Weapon %s  EXP %d" % [models.get_item_name(int(st["weapon_id"])), st["weapon_exp"]])
			menu_info.text = "Floor %d" % (floor_no + 1)
		3:
			menu_title.text = "PARTY    (Enter: switch to the character)"
			for c in models.get_party():
				if not c["joined"]:
					continue
				menu_entries.append(c)
				var mark2 := "> " if int(c["character"]) == walker.get_character() else "  "
				menu_list.add_item("%s%s   Life %d / %d" % [mark2, c["name"], c["life"], c["max_life"]])
			menu_info.text = "Everyone who has joined can be called in at any time (a character with no life left cannot)."
		4:
			menu_title.text = "LEAVE THE DUNGEON"
			menu_list.add_item("Escape to the village")
			menu_list.add_item("Stay")
			menu_info.text = "Leaving ends this visit; what you carry stays with you."
	if menu_list.item_count > 0:
		menu_list.select(0)

func _menu_toggle() -> void:
	menu_open = not menu_open
	menu_layer.visible = menu_open
	if coop != null:   # (in co-op the world does not wait for one player's menu)
		walker.set_input_blocked(menu_open)
	else:
		walker.set_physics_process(not menu_open)
	if menu_open:
		menu_page = 0
		_menu_refresh()
		menu_list.grab_focus()

func _menu_activate() -> void:
	var sel := menu_list.get_selected_items()
	if sel.is_empty() or menu_page == 2:
		return
	if menu_page == 3:
		var c3: Dictionary = menu_entries[sel[0]]
		if models.switch_character(walker, int(c3["character"])):
			_say("%s takes over" % c3["name"])
			_menu_toggle()
		else:
			_say("%s cannot fight now" % c3["name"])
		return
	if menu_page == 4:
		if sel[0] == 0:
			_finish("escape")
		else:
			_menu_toggle()
		return
	if menu_page == 1:   # the weapon page
		var wm := WeaponMenuUi.new()
		menu_layer.add_child(wm)
		wm.setup(models, walker)
		wm.closed.connect(func(): _menu_refresh())
		return
	var e: Dictionary = menu_entries[sel[0]]
	if menu_page == 0:
		var r := walker.use_bag_item(int(e["slot"]))
		if r.get("used", false):
			_say("Used %s" % models.get_item_name(int(e["id"])))
		else:
			_say("Cannot use that here")
	elif menu_page == 1:
		walker.equip_weapon(int(e["slot"]))
	_menu_refresh()

func _open_item_pick() -> void:
	if pick_layer != null:
		return
	pick_layer = CanvasLayer.new()
	pick_layer.layer = 11
	var panel := Panel.new()
	panel.position = Vector2(300, 120)
	panel.size = Vector2(560, 420)
	pick_layer.add_child(panel)
	var title := Label.new()
	title.text = "Use which item?"
	title.position = Vector2(20, 12)
	title.add_theme_font_size_override("font_size", 28)
	panel.add_child(title)
	var list := ItemList.new()
	list.position = Vector2(20, 60)
	list.size = Vector2(520, 300)
	list.fixed_icon_size = Vector2i(32, 32)
	list.add_theme_font_size_override("font_size", 22)
	panel.add_child(list)
	pick_ids.clear()
	for it in walker.get_bag():
		var id := int(it["id"])
		pick_ids.append(id)
		var n := int(it["value"])
		list.add_item("%s%s" % [models.get_item_name(id), (" x%d" % n) if n > 1 else ""], models.get_item_icon(id))
	if list.item_count == 0:
		list.add_item("(nothing to use)")
	else:
		list.select(0)
	add_child(pick_layer)
	list.grab_focus()
	list.item_activated.connect(func(i: int): _close_item_pick(pick_ids[i] if i < pick_ids.size() else -1))
	var note := Label.new()
	note.text = "[Enter] Use   [Esc] Cancel"
	note.position = Vector2(20, 372)
	note.add_theme_font_size_override("font_size", 20)
	panel.add_child(note)

func _close_item_pick(id: int) -> void:
	if pick_layer == null:
		return
	pick_layer.queue_free()
	pick_layer = null
	walker.modal_done(id)

func _unhandled_input(event: InputEvent) -> void:
	if pick_layer != null:   # (the item window has the keys)
		if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
			_close_item_pick(-1)
			get_viewport().set_input_as_handled()
		elif event is InputEventJoypadButton and event.pressed and event.button_index == JOY_BUTTON_B:
			_close_item_pick(-1)
			get_viewport().set_input_as_handled()
		return
	if walker == null or menu_layer == null or _finished:
		return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_TAB or event.keycode == KEY_ESCAPE:
			_menu_toggle()
			get_viewport().set_input_as_handled()
		elif not menu_open and coop != null and (event.keycode == KEY_BRACKETLEFT or event.keycode == KEY_BRACKETRIGHT):   # co-op: the next / previous character
			coop.cycle_character(-1 if event.keycode == KEY_BRACKETLEFT else 1)
		elif menu_open and event.keycode == KEY_RIGHT:
			menu_page = (menu_page + 1) % MENU_PAGES
			_menu_refresh()
		elif menu_open and event.keycode == KEY_LEFT:
			menu_page = (menu_page + MENU_PAGES - 1) % MENU_PAGES
			_menu_refresh()
		elif menu_open and (event.keycode == KEY_ENTER or event.keycode == KEY_SPACE):
			_menu_activate()
		elif menu_open and menu_page == 0 and coop != null and event.keycode == KEY_D:   # co-op: drop the selected item for the other players
			var sel2 := menu_list.get_selected_items()
			if not sel2.is_empty() and coop.drop_item(int(menu_entries[sel2[0]]["slot"])):
				_say("Dropped it for the others")
				_menu_refresh()
		elif menu_open and menu_page == 0 and event.keycode >= KEY_1 and event.keycode <= KEY_3:
			var sel := menu_list.get_selected_items()
			if not sel.is_empty() and walker.assign_quick(event.keycode - KEY_1, int(menu_entries[sel[0]]["slot"])):
				_say("Set as quick item %d" % (event.keycode - KEY_1 + 1))
				_menu_refresh()
	elif event is InputEventJoypadButton and event.pressed and event.button_index == JOY_BUTTON_START:
		_menu_toggle()

func _make_hud() -> void:
	_make_menu()
	var layer := CanvasLayer.new()
	hud_label = Label.new()
	hud_label.position = Vector2(16, 12)
	hud_label.add_theme_font_size_override("font_size", 26)
	hud_label.add_theme_color_override("font_outline_color", Color.BLACK)
	hud_label.add_theme_constant_override("outline_size", 6)
	hud = DungeonHud.new()
	layer.add_child(hud)
	hud.setup(models)
	layer.add_child(hud_label)
	message_label = Label.new()
	message_label.position = Vector2(16, 130)
	message_label.add_theme_font_size_override("font_size", 22)
	message_label.add_theme_color_override("font_outline_color", Color.BLACK)
	message_label.add_theme_constant_override("outline_size", 5)
	layer.add_child(message_label)
	add_child(layer)

func _process(_delta: float) -> void:
	if walker == null or _finished:
		return
	var st := walker.get_player_status()
	hud.set_state(st, floor_no, dungeon)
	hud.visible = not st.is_empty() and not menu_open
	if st.get("game_over", false):
		hud_label.text = "GAME OVER - press the action button"
	else:
		hud_label.text = ""

var _flicker_t := 0.0
func _flicker_torches() -> void:
	_flicker_t += 0.12
	var i := 0
	for l in get_tree().get_nodes_in_group("torch"):
		i += 1
		l.light_energy = 2.0 + 0.5 * sin(_flicker_t * 3.0 + i * 1.7) + 0.25 * sin(_flicker_t * 7.3 + i * 0.9)
