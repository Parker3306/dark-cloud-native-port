extends Node3D
# Renders a generated dungeon floor with the C++ code and saves screenshots.
# Usage (after "--"): --iso <iso> --cfg d01main_a --seed 8097 --rooms 6 [--map 0] [--out prefix]
#   [--views overview,start,x:y:z>x:y:z,...]   each view is a name or "eye>target" (game units)
#   [--lit 0|1]  game lights (default 1)

func _arg(name: String, default: String = "") -> String:
	var a := OS.get_cmdline_user_args()
	var i := a.find(name)
	return a[i + 1] if i >= 0 and i + 1 < a.size() else default

var _cam: Camera3D

func _ready() -> void:
	var models := DcModels.new()
	if not models.open_iso(_arg("--iso", ProjectSettings.globalize_path("res://../../iso/Dark Cloud (USA).iso"))):
		print("OPEN_FAILED")
		get_tree().quit(1)
		return
	var floor_node: DcDungeon = models.build_dungeon(_arg("--cfg", "d01main_a"), int(_arg("--seed", "8097")), int(_arg("--rooms", "6")), int(_arg("--map", "0")))
	if floor_node == null:
		print("BUILD_FAILED")
		get_tree().quit(1)
		return
	print("INFO ", floor_node.get_info())
	add_child(floor_node)
	_setup_environment(floor_node.get_environment(), _arg("--lit", "1") == "1")
	_cam = Camera3D.new()
	_cam.fov = 45.0
	_cam.far = 5000.0
	add_child(_cam)
	var prefix := _arg("--out", "")
	if prefix == "":
		get_tree().quit()
		return
	var start := floor_node.get_start_position()
	var views: PackedStringArray = _arg("--views", "overview,start").split(",", false)
	var n := 0
	for v in views:
		var eye := Vector3()
		var target := Vector3()
		if v == "overview":
			eye = Vector3(8 * 160, 2600, 8 * 160 + 900); target = Vector3(8 * 160, 0, 8 * 160)
		elif v == "start":
			eye = start + Vector3(0, 120, 260); target = start + Vector3(0, 30, 0)
		else:
			var p := v.split(">")
			var e := p[0].split(":")
			var t := p[1].split(":")
			eye = Vector3(float(e[0]), float(e[1]), float(e[2])); target = Vector3(float(t[0]), float(t[1]), float(t[2]))
		_cam.position = eye
		_cam.look_at(target)
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_%d_%s.png" % [prefix, n, v.replace(":", "_").replace(">", "to")])
		n += 1
	get_tree().quit()

func _setup_environment(e: Dictionary, lit: bool) -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = e.get("background", Color.BLACK)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = e.get("ambient", Color.BLACK)
	env.ambient_light_energy = 1.0
	if not lit:
		env.ambient_light_color = Color(0.9, 0.9, 0.9)
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	if lit:
		for l in e.get("lights", []):
			var d := DirectionalLight3D.new()
			var dir: Vector3 = l["direction"]
			d.look_at_from_position(Vector3.ZERO, -dir.normalized(), Vector3.UP)   # the cfg gives the direction TOWARDS the light
			d.light_color = l["color"]
			d.light_energy = 255.0 / 128.0   # PS2 colours: 128 = full modulation
			add_child(d)
	else:
		var d := DirectionalLight3D.new()
		d.rotation_degrees = Vector3(-60, -30, 0)
		add_child(d)
