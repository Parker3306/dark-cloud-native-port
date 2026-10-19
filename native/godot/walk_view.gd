extends Node3D
# Play (or script-test) a generated dungeon floor with the game's own movement and camera.
# Usage (after "--"):  --iso <iso> --cfg d01main_a --seed 8097 --rooms 6 [--map 0] [--char dun/mainchara/c01d.chr]
#   interactive: WASD/arrows = left stick, IJKL = right stick (camera), Q/E = turn camera, R = camera behind. Gamepad 0 works too.
#   automated:   --script up:120,right:60,...   (stick presets: up down left right upleft ... none, each followed by :ticks)
#                --shots 60,120,...  --out prefix   saves a screenshot after those ticks, then quits
#                --lit 0|1  game lights on (default 1), --fov 45

func _arg(name: String, default: String = "") -> String:
	var a := OS.get_cmdline_user_args()
	var i := a.find(name)
	return a[i + 1] if i >= 0 and i + 1 < a.size() else default

const PRESETS := {
	"up": [128, 0], "down": [128, 255], "left": [0, 128], "right": [255, 128], "none": [128, 128],
	"upleft": [30, 30], "upright": [226, 30], "downleft": [30, 226], "downright": [226, 226],
	"attack": [128, 128, 64], "upattack": [128, 0, 64],
}

func _ready() -> void:
	var models := DcModels.new()
	if not models.open_iso(_arg("--iso", ProjectSettings.globalize_path("res://../../iso/Dark Cloud (USA).iso"))):
		print("OPEN_FAILED")
		get_tree().quit(1)
		return
	var walker: DcWalker = models.build_walker(_arg("--cfg", "d01main_a"), int(_arg("--seed", "8097")), int(_arg("--rooms", "6")), int(_arg("--map", "0")), _arg("--char", "dun/mainchara/c01d.chr"), int(_arg("--floor", "0")), _arg("--monsters", "1") == "1", int(_arg("--arrange-seed", "-1")))
	if walker == null:
		print("BUILD_FAILED")
		get_tree().quit(1)
		return
	print("INFO ", models.get_last_info())
	for m in walker.get_monster_info():
		print("MONSTER ", m)
	add_child(walker)
	_make_hud(walker)
	_setup_environment(walker.get_floor().get_environment(), _arg("--lit", "1") == "1")
	walker.get_camera().fov = float(_arg("--fov", "31.3"))
	walker.get_camera().make_current()
	if _arg("--overview", "") != "":    # a fixed look from above (game units): --overview x,y,z,tx,ty,tz
		var o := _arg("--overview").split(",")
		walker.override_view(Vector3(800, 0, 0), 0.0, Vector3(float(o[0]), float(o[1]), float(o[2])), Vector3(float(o[3]), float(o[4]), float(o[5])))
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_overview.png" % _arg("--out", "view"))
		get_tree().quit()
		return
	if _arg("--teleport", "") != "":    # x,z in game units
		var t := _arg("--teleport").split(",")
		walker.teleport(float(t[0]), float(t[1]))
	var script_text := _arg("--script", "")
	if script_text != "":
		var ticks: Array = []
		for part in script_text.split(",", false):
			var f := part.split(":")
			var preset: Array = PRESETS.get(f[0], [128, 128])
			for i in int(f[1]):
				ticks.append([preset[0], preset[1], 128, 128, preset[2] if preset.size() > 2 else 0])
		walker.set_script_ticks(ticks)
		walker.set_use_devices(false)
	if _arg("--view", "") != "":    # px,py,pz,yaw,cx,cy,cz,rx,ry,rz: static placement for comparing with a game screenshot
		var v := _arg("--view").split(",")
		walker.override_view(Vector3(float(v[0]), float(v[1]), float(v[2])), float(v[3]), Vector3(float(v[4]), float(v[5]), float(v[6])), Vector3(float(v[7]), float(v[8]), float(v[9])))
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_view.png" % _arg("--out", "view"))
		get_tree().quit()
		return
	var prefix := _arg("--out", "")
	if prefix == "":
		return   # interactive
	walker.set_physics_process(false)
	var done := 0
	for s in _arg("--shots", "0").split(",", false):
		var target := int(s)
		walker.step_ticks(target - done)
		done = target
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_t%d.png" % [prefix, target])
		for m in walker.get_monster_info():
			if m.get("state", 0) == 2: print("  awake ", m)
		print("SHOT ", target, " player ", walker.get_player_position(), " camera ", walker.get_camera_state(), " status ", walker.get_player_status())
	get_tree().quit()

func _setup_environment(e: Dictionary, lit: bool) -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = e.get("background", Color.BLACK)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = e.get("ambient", Color.BLACK) if lit else Color(0.9, 0.9, 0.9)
	env.ambient_light_energy = 1.0
	if lit and e.get("fog_far", 0.0) > 0.0:
		env.fog_enabled = true
		env.fog_mode = Environment.FOG_MODE_DEPTH
		env.fog_depth_begin = e["fog_near"]
		env.fog_depth_end = e["fog_far"]
		env.fog_depth_curve = 1.0
		env.fog_light_color = e["fog_color"]
		env.fog_sky_affect = 1.0
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	if lit:
		for l in e.get("lights", []):
			var d := DirectionalLight3D.new()
			var dir: Vector3 = l["direction"]
			d.look_at_from_position(Vector3.ZERO, -dir.normalized(), Vector3.UP)   # the cfg gives the direction TOWARDS the light
			d.light_color = l["color"]
			d.light_energy = 255.0 / 128.0
			add_child(d)
	else:
		var d := DirectionalLight3D.new()
		d.rotation_degrees = Vector3(-60, -30, 0)
		add_child(d)

var _hud_label: Label
var _hud_walker: DcWalker

func _make_hud(walker: DcWalker) -> void:
	# life readout (the game's own life gauge is part of Milestone 5)
	_hud_walker = walker
	var layer := CanvasLayer.new()
	_hud_label = Label.new()
	_hud_label.position = Vector2(16, 12)
	_hud_label.add_theme_font_size_override("font_size", 26)
	_hud_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_hud_label.add_theme_constant_override("outline_size", 6)
	layer.add_child(_hud_label)
	add_child(layer)

func _process(_delta: float) -> void:
	if _hud_label == null or _hud_walker == null:
		return
	var st := _hud_walker.get_player_status()
	if st.is_empty():
		_hud_label.text = ""
	elif st.get("game_over", false):
		_hud_label.text = "GAME OVER"
	else:
		_hud_label.text = "LIFE %d / %d    GOLD %d    GAUGE %d    WEAPON %.1f/%d  EXP %d" % [st["life"], st["max_life"], st["gold"], st["gauge"], st["weapon_durability"], st["weapon_max_durability"], st["weapon_exp"]]
