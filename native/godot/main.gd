extends Node3D
# Usage (after "--"):
#   --iso <iso> [--game dun/mainchara/c01d.chr | --file <pack on disk>] [--times 15.5,80,...] [--yaws 0,35] [--out prefix] [--cam-y 8 --cam-z 38]
# Builds the character with the C++ code, poses it at each motion time (frame number), writes one screenshot per (time, yaw), quits.

func _arg(name: String, default: String = "") -> String:
	var a := OS.get_cmdline_user_args()
	var i := a.find(name)
	return a[i + 1] if i >= 0 and i + 1 < a.size() else default

func _ready() -> void:
	var models := DcModels.new()
	var iso := _arg("--iso", ProjectSettings.globalize_path("res://../../iso/Dark Cloud (USA).iso"))
	if not models.open_iso(iso):
		print("OPEN_FAILED")
		get_tree().quit(1)
		return
	var node: DcCharacter
	if _arg("--file") != "":
		node = models.build_character_file(_arg("--file"))
	else:
		node = models.build_character(_arg("--game", "dun/mainchara/c01d.chr"))
	if node == null:
		print("BUILD_FAILED")
		get_tree().quit(1)
		return
	print("INFO ", models.get_last_info())
	add_child(node)
	node.set_playing(false)
	_setup_scene()
	var prefix := _arg("--out", "")
	if prefix == "":
		get_tree().quit()
		return
	var times: PackedStringArray = _arg("--times", "").split(",", false)
	if _arg("--motions", "") != "":    # pick a time inside each listed motion number (35 % of its frame range)
		times = PackedStringArray()
		for m in _arg("--motions").split(",", false):
			var info: Dictionary = node.get_motion_info(int(m))
			times.append(str(snappedf(float(info.get("start", 0)) + (float(info.get("end", 0)) - float(info.get("start", 0))) * 0.35, 0.1)))
	if times.is_empty():
		times = PackedStringArray([str(node.get_motion_info(0).get("start", 0))])
	for t in times:
		node.set_time(float(t))
		for yaw in _arg("--yaws", "0").split(","):
			node.rotation_degrees.y = float(yaw)
			await RenderingServer.frame_post_draw
			await RenderingServer.frame_post_draw
			get_viewport().get_texture().get_image().save_png("%s_t%s_y%s.png" % [prefix, t, yaw])
	get_tree().quit()

func _setup_scene() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.32, 0.33, 0.36)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.75, 0.75, 0.8)
	env.ambient_light_energy = 0.55
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, -30, 0)
	sun.light_energy = 1.1
	add_child(sun)
	var cam := Camera3D.new()
	cam.fov = 30.0
	var cy := float(_arg("--cam-y", "8.0"))
	cam.position = Vector3(0, cy, float(_arg("--cam-z", "38.0")))
	add_child(cam)
	cam.look_at(Vector3(0, cy, 0))
