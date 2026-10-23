class_name TownRun
extends Node3D
# A Georama town (or story scene) you can walk through: the scene, the player, the follow camera and the light of the time of day, the villagers' talk and the
# event scripts (cutscenes) with their message windows and fades.

var models: DcModels
var walk: DcTownWalk
var scene: DcTown
var world_env: WorldEnvironment
var sun: DirectionalLight3D
var map_name := ""
var prompt: Label
var win_panels: Array = []
var win_labels: Array = []
var fade_rect: ColorRect
var sys_label: Label
var exclaim: Label
var nav_log: Array = []
var sound: DcSound
var sprite_layer: Control
var filter_rect: ColorRect
var sprites: Array = []
var back_color := Color(0, 0, 0, 0)
var ui_layer: CanvasLayer
var eb_overlay: EbOverlay
var options := {}
var edit_ui: TownEditUi
var door_return := {}   # coming out of a house: {town, pos, yaw}
var coop: CoopTown      # co-op: the other players and the character keys (null when playing alone)
signal dungeon_requested(dungeon: int, floor: int)
signal map_requested(map_no: int, event: int)
signal edit_finished   # the Georama edit mode was left: the town may look different now (co-op: the guests rebuild it)
var _was_editing := false

func start(session: DcModels, name: String, opts: Dictionary = {}) -> bool:
	models = session
	options = opts
	sound = opts.get("sound", null)
	world_env = WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	env.glow_intensity = 0.4
	env.glow_bloom = 0.03
	env.ssao_enabled = true
	env.ssao_radius = 8.0
	env.ssao_intensity = 1.2
	world_env.environment = env
	add_child(world_env)
	sun = DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 800.0
	add_child(sun)
	_make_ui()
	if "/" in name:   # a house straight away: "e01/i01h01"
		var parts := name.split("/")
		var w: DcTownWalk = models.build_interior(parts[0], parts[1], int(opts.get("event", -1)), int(opts.get("variant", 0)))
		if w == null:
			return false
		map_name = parts[0]
		door_return = {"town": parts[0], "pos": Vector3.ZERO, "yaw": 0.0}
		_swap_walk(w)
	elif not _enter_town(name, int(opts.get("event", -1)), int(opts.get("old_map", 0))):
		return false
	var net := CoopNet.find(get_tree())
	if net != null and net.active:
		coop = CoopTown.new()
		add_child(coop)
		coop.setup(self, net)
	if opts.has("script"):
		walk.set_script_ticks(opts["script"])
		walk.set_use_devices(false)
	if opts.has("teleport"):
		var t: Array = opts["teleport"]
		walk.teleport(t[0], t[1], t[2])
	return true

# the map's own sound (EditInit: the sound set and the ambient sequences; the music is started by the entry event)
func _map_sound(event: int = -1) -> void:
	if sound == null or scene == null:
		return
	var info := scene.get_info()
	if event == -2 and int(info.get("bgm", -1)) >= 0:   # no entry scene starts the music (back from a dungeon, a loaded game)
		sound.load_bgm(int(info["bgm"]))
		sound.play_bgm(0)
	var set_no: int = info.get("sound_set", -1)
	if set_no >= 0:
		sound.load_sound_set(set_no)
		sound.play_ambient(0)

func _swap_walk(new_walk: DcTownWalk) -> void:
	if walk != null:
		walk.queue_free()
		remove_child(walk)
	walk = new_walk
	add_child(walk)
	move_child(walk, 0)
	scene = walk.get_scene()
	walk.get_camera().current = true
	_apply_light()
	if coop != null:
		coop.clear_puppets()
		coop.apply_hero()

func _enter_town(name: String, event: int, old_map: int) -> bool:
	var w: DcTownWalk = models.build_town(name, options.get("complete", false), event, old_map)
	if w == null:
		return false
	map_name = name
	_swap_walk(w)
	_map_sound(event)
	return true

func _enter_interior(house: String, door_index: int) -> bool:
	var town_name := map_name
	var out := walk.get_door_out(door_index)
	var variant := 0
	var t: float = walk.get_clock()
	if t >= 2.5 and t < 5.5:
		variant = 1
	elif t >= 5.5 and t < 8.5:
		variant = 2
	var w: DcTownWalk = models.build_interior(town_name, house, -1, variant)
	if w == null:
		print("INTERIOR_FAILED ", house)
		return false
	door_return = {"town": town_name, "pos": out.get("pos", Vector3.ZERO), "yaw": out.get("yaw", 0.0)}
	map_name = town_name + "/" + house
	_swap_walk(w)
	return true

func _leave_interior(event: int) -> void:
	var town_name: String = door_return.get("town", "e01")
	var w: DcTownWalk = models.build_town(town_name, options.get("complete", false), event if event >= 0 else -2, 0)
	if w == null:
		return
	map_name = town_name
	_swap_walk(w)
	var p: Vector3 = door_return.get("pos", Vector3.ZERO)
	walk.place_hero(p.x, p.y + 5.0, p.z, door_return.get("yaw", 0.0))

func _handle_nav(n: Dictionary) -> void:
	match int(n["kind"]):
		1:
			if String(n["name"]) == "dungeon":
				dungeon_requested.emit(int(map_name.substr(1, 2)) - 1, 0)
			else:
				_enter_interior(String(n["name"]), int(n.get("index", -1)))
		2:
			_leave_interior(int(n.get("event", -1)))
		3:
			map_requested.emit(int(n["map"]), int(n.get("event", -1)))
		4:
			dungeon_requested.emit(int(map_name.substr(1, 2)) - 1, int(n.get("floor", 0)))

var shop_ui: ShopUi
var modal_ui: ModalUi

func open_save() -> void:
	var s := SaveUi.new()
	ui_layer.add_child(s)
	s.setup(models, true)

func open_options() -> void:
	var opt := OptionUi.new()
	ui_layer.add_child(opt)
	opt.setup(models)

func _open_modal(kind: int, args: Array) -> void:
	var ui := ModalUi.new()
	ui_layer.add_child(ui)
	if not ui.setup(models, kind, args):   # the screens that are not made yet answer at once
		ui.queue_free()
		walk.modal_done(0)
		return
	modal_ui = ui
	ui.done.connect(func(value: int):
		modal_ui = null
		walk.refresh_names()
		walk.modal_done(value))

func open_shop(shop: int, charge: bool) -> void:
	if shop_ui != null:
		return
	shop_ui = ShopUi.new()
	ui_layer.add_child(shop_ui)
	shop_ui.setup(models, shop, charge)
	shop_ui.closed.connect(func():
		shop_ui = null
		walk.shop_done())

func is_editing() -> bool:
	return walk != null and walk.get_edit_ui().get("active", false)

func _input(event: InputEvent) -> void:
	if edit_ui != null and edit_ui.handle_key(event):
		get_viewport().set_input_as_handled()
		return
	if coop != null and walk != null and not is_editing():   # co-op: Q / E (or L1 / R1) change the character you play
		var dir := 0
		if event is InputEventKey and event.pressed and not event.echo:
			dir = -1 if event.keycode == KEY_Q else (1 if event.keycode == KEY_E else 0)
		elif event is InputEventJoypadButton and event.pressed and DisplayServer.window_is_focused():
			dir = -1 if event.button_index == JOY_BUTTON_LEFT_STICK else (1 if event.button_index == JOY_BUTTON_RIGHT_STICK else 0)   # (L3 / R3: L1 / R1 are the camera buttons of the game)
		if dir != 0:
			coop.cycle_character(dir)

func _make_ui() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 5
	ui_layer = layer
	add_child(layer)
	for i in 8:
		var p := PanelContainer.new()
		var style := StyleBoxFlat.new()
		style.bg_color = Color(0.05, 0.08, 0.2, 0.86)
		style.border_color = Color(0.9, 0.85, 0.6, 0.9)
		style.set_border_width_all(2)
		style.set_corner_radius_all(8)
		style.content_margin_left = 16
		style.content_margin_right = 16
		style.content_margin_top = 10
		style.content_margin_bottom = 10
		p.add_theme_stylebox_override("panel", style)
		p.visible = false
		var l := Label.new()
		l.add_theme_font_size_override("font_size", 26)
		l.add_theme_color_override("font_color", Color(1, 1, 1))
		p.add_child(l)
		layer.add_child(p)
		win_panels.append(p)
		win_labels.append(l)
	sprite_layer = Control.new()
	sprite_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	sprite_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	sprite_layer.draw.connect(_draw_sprites)
	layer.add_child(sprite_layer)
	filter_rect = ColorRect.new()
	filter_rect.color = Color(0, 0, 0, 0)
	filter_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	filter_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(filter_rect)
	layer.move_child(filter_rect, 0)
	layer.move_child(sprite_layer, 0)
	sys_label = Label.new()
	sys_label.position = Vector2(300, 40)
	sys_label.add_theme_font_size_override("font_size", 28)
	layer.add_child(sys_label)
	prompt = Label.new()
	prompt.position = Vector2(20, 20)
	prompt.add_theme_font_size_override("font_size", 28)
	layer.add_child(prompt)
	exclaim = Label.new()
	exclaim.text = "!"
	exclaim.add_theme_font_size_override("font_size", 48)
	exclaim.add_theme_color_override("font_color", Color(1, 0.9, 0.2))
	exclaim.visible = false
	layer.add_child(exclaim)
	eb_overlay = EbOverlay.new()   # (the EB duel of the story scenes: lane and markers, below the fade)
	layer.add_child(eb_overlay)
	eb_overlay.setup(models)
	fade_rect = ColorRect.new()
	fade_rect.color = Color(0, 0, 0, 0)
	fade_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	fade_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(fade_rect)
	edit_ui = TownEditUi.new()
	layer.add_child(edit_ui)
	edit_ui.setup(self)

func _apply_light() -> void:
	var t: float = walk.get_clock()
	var l := scene.get_light(t)
	var env := world_env.environment
	env.ambient_light_color = l.get("ambient", Color(0.4, 0.4, 0.4))
	env.background_color = l.get("bg_top", Color.BLACK)
	if l.has("fog_far"):
		env.fog_enabled = true
		env.fog_mode = Environment.FOG_MODE_DEPTH
		env.fog_depth_begin = l["fog_near"]
		env.fog_depth_end = l["fog_far"]
		env.fog_depth_curve = 1.0
		env.fog_light_color = l["fog_color"]
		env.fog_sky_affect = 0.0
	var lights: Array = l.get("lights", [])
	if lights.size() > 0:
		var dir: Vector3 = lights[0]["direction"]
		sun.look_at_from_position(Vector3.ZERO, -dir.normalized(), Vector3.UP)
		sun.light_color = lights[0]["color"]
		sun.light_energy = 1.0

func advance(ticks: int) -> void:
	walk.step_ticks(ticks)
	_apply_light()
	_update_ui()

func _process(_delta: float) -> void:
	if walk == null:
		return
	var editing := is_editing()
	if _was_editing and not editing:
		edit_finished.emit()
	_was_editing = editing
	_apply_light()
	_update_ui()
	if sound != null:
		for e in walk.take_sound_events():
			_sound_event(e)
		sound.set_listener(walk.get_camera().global_position, -walk.get_camera().global_transform.basis.z)
	for n in walk.take_edit_notices():
		edit_ui.notice(n)
	var modal_req := walk.take_modal_request()
	if not modal_req.is_empty():
		_open_modal(int(modal_req["kind"]), modal_req["args"])
	var shop_req := walk.take_shop_request()
	if not shop_req.is_empty():
		open_shop(int(shop_req["shop"]), int(shop_req["charge"]) != 0)
	for n in walk.take_nav():
		nav_log.append(n)
		print("NAV ", n)
		_handle_nav(n)

func _update_ui() -> void:
	var ui: Dictionary = walk.get_ui()
	prompt.text = "[Enter] " + String(ui["prompt"]) if ui.has("prompt") else ""
	var ed: Dictionary = ui.get("edit", {})
	edit_ui.update(ed)
	if ed.get("can_edit", false) and not ed.get("active", false) and prompt.text == "" and int(ui.get("mode", 0)) == 0:
		prompt.text = "[B] Georama edit mode"
	var wins: Array = ui.get("windows", [])
	var vp := get_viewport().get_visible_rect().size
	for i in 8:
		win_panels[i].visible = false
	for w in wins:
		var i: int = w["index"]
		var panel: PanelContainer = win_panels[i]
		var label: Label = win_labels[i]
		var txt: String = w["text"]
		var cur: int = w["cursor"]
		if cur >= 0:
			var lines := String(w["full"]).split("\n")
			var shown := PackedStringArray()
			for k in lines.size():
				var ln: String = lines[k]
				if ln.begins_with("  "):
					ln = ln.substr(2)
				shown.append(("▶ " if k == cur else "   ") + ln)
			txt = "\n".join(shown)
		if w["okuri"]:
			txt += "  ▼"
		label.text = txt
		panel.visible = true
		panel.modulate.a = clampf(w["openness"], 0.0, 1.0)
		panel.reset_size()
		var sz := panel.get_combined_minimum_size()
		var pos := Vector2((vp.x - sz.x) * 0.5, vp.y - sz.y - 40.0)
		var pm: int = w["pos"]
		if pm >= 1 and pm <= 9:
			var col := (pm - 1) % 3
			var row := (pm - 1) / 3
			pos = Vector2(40.0 + (vp.x - sz.x - 80.0) * col * 0.5, 40.0 + (vp.y - sz.y - 80.0) * row * 0.5)
		elif w["has_a"]:
			var a: Vector2 = w["a"]
			pos = Vector2(clampf(a.x - sz.x * 0.5, 10.0, vp.x - sz.x - 10.0), clampf(a.y - sz.y - 30.0, 10.0, vp.y - sz.y - 10.0))
		panel.position = pos
	sprites = ui.get("sprites", [])
	sprite_layer.queue_redraw()
	filter_rect.color = ui.get("filter", Color(0, 0, 0, 0))
	var env := world_env.environment
	if ui.has("back"):
		env.background_color = ui["back"]
	if ui.has("ambient"):
		env.ambient_light_color = ui["ambient"]
	var ls: Array = ui.get("lights", [])
	if ls.size() > 0:
		var dir: Vector3 = ls[0]["direction"]
		sun.look_at_from_position(Vector3.ZERO, -dir, Vector3.UP)
		sun.light_color = ls[0]["color"]
	var fade: Dictionary = ui.get("fade", {})
	if fade.size() > 0:
		var c: Color = fade["color"]
		c.a = clampf(fade["alpha"], 0.0, 1.0)
		fade_rect.color = c
	eb_overlay.update_from(walk.get_eb_ui())
	sys_label.text = String(ui.get("system_message", ""))
	exclaim.visible = ui.get("exclamation", false)
	if exclaim.visible:
		var sp: Vector2 = walk.get_camera().unproject_position(walk.get_player_position() + Vector3(0, 40, 0))
		exclaim.position = sp

# the game shell shows its own menus on top: the town stops and hides while they are open
func set_active(on: bool) -> void:
	visible = on
	ui_layer.visible = on
	process_mode = Node.PROCESS_MODE_INHERIT if on else Node.PROCESS_MODE_DISABLED
	if on and walk != null:
		walk.get_camera().current = true

func _draw_sprites() -> void:
	var vp := get_viewport().get_visible_rect().size
	var k := Vector2(vp.x / 640.0, vp.y / 448.0)
	for s in sprites:
		var tex: Texture2D = s["tex"]
		if tex == null:
			continue
		var dst := Rect2(Vector2(s["x"], s["y"]) * k, Vector2(s["sw"], s["sh"]) * k)
		var src := Rect2(s["u"], s["v"], s["w"], s["h"])
		if s["rot"] != 0.0:
			sprite_layer.draw_set_transform(Vector2(s["rot_cx"], s["rot_cy"]) * k, s["rot"], Vector2.ONE)
		sprite_layer.draw_texture_rect_region(tex, dst, src, s["color"])
		sprite_layer.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)

func _sound_event(e: Dictionary) -> void:
	var a: int = e["a"]
	var v: Array = e["v"]
	match String(e["op"]):
		"_LOAD_BGM": sound.load_bgm(a)
		"_PLAY_BGM": sound.play_bgm(a)
		"_STOP_BGM": sound.stop_bgm(0)
		"_BGM_FADE_IN": sound.bgm_fade_in(int(v[2]) if v[2] > 0 else 60, int(v[1]))
		"_BGM_FADE_OUT": sound.bgm_fade_out(maxi(a, 1))
		"_PLAY_SE":
			if v[1] != 0.0 or v[2] != 0.0 or v[3] != 0.0:
				sound.play_se_at(a, Vector3(v[1], v[2], v[3]))
			else:
				sound.play_se(a)
		"_STOP_SE": sound.stop_se(a)
		"_EB_SE": sound.play_se(a)
		"_EB_BGM_FADE": sound.bgm_fade_out(maxi(a, 1))
		"_EB_FANFARE":
			sound.load_special(a)
			sound.play_special(0)
		"_LOAD_SOUND_SET": sound.load_sound_set(a)
		"_LOAD_SPECIAL_SE": sound.load_special(a)
		"_PLAY_SPECIAL_SE": sound.play_special(0)
		"_STOP_SPECIAL_SE": sound.stop_special()
		"_LOAD_VOICE_SET": sound.load_voice(a)
		"_PLAY_AMB_BGM": sound.play_ambient(a)
		"_STOP_AMB_BGM": sound.stop_ambient()
