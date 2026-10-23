extends Node3D
# Harness: the first motions of a playable character side by side, each frozen a few frames into its motion (is the run motion really a run?). --char 0 --out prefix

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
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.35, 0.4, 0.45)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.8, 0.8, 0.8)
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, 25, 0)
	add_child(sun)
	var ch := int(_arg("--char", "0"))
	var motions: PackedStringArray = _arg("--motions", "0,1,2,3,4,5").split(",")
	for i in range(motions.size()):
		var m := int(motions[i])
		var n: Node3D = models.make_player_model(ch, -1, -1)
		var dc := n as DcCharacter
		var info: Dictionary = dc.get_motion_info(m)
		print("MOTION ", m, " of ", dc.get_motion_count(), " ", info)
		n.position = Vector3((i - (motions.size() - 1) * 0.5) * 34.0, 0, 0)
		add_child(n)
		dc.play(m, true)
		dc.set_playing(false)
		dc.set_time(float(info.get("start", 0)) + float(_arg("--at", "6")))
	var cam := Camera3D.new()
	cam.fov = 40
	add_child(cam)
	cam.position = Vector3(0, 40, 150)
	cam.look_at(Vector3(0, 12, 0), Vector3.UP)
	cam.make_current()
	for k in range(3):
		await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png("%s_motions.png" % _arg("--out", ProjectSettings.globalize_path("res://../../work/mv")))
	get_tree().quit()
