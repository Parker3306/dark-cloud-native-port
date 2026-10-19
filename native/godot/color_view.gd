extends Node3D
# Harness for the co-op colours: the six playable characters in a row for every player colour. --out prefix  [--colors 4]

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
	var colors := int(_arg("--colors", "4"))
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
	var spacing := 34.0
	var chars: PackedStringArray = _arg("--chars", "0,1,2,3,4,5").split(",")
	for row in range(-1, colors):   # row -1: the original colours
		for ci in range(chars.size()):
			var ch := int(chars[ci])
			var n: Node3D = models.make_player_model(ch, row, int(_arg("--weapon", "-1")))
			if n == null:
				continue
			n.position = Vector3((ci - (chars.size() - 1) * 0.5) * spacing, 0, -(row + 1) * 42.0 * float(_arg("--rowscale", "1")))
			n.rotation.y = 0.0
			add_child(n)
			(n as DcCharacter).play(0, true)
			(n as DcCharacter).set_playing(false)
			(n as DcCharacter).set_time(0)
	var cam := Camera3D.new()
	cam.fov = 40
	add_child(cam)
	cam.position = Vector3(0, float(_arg("--camy", "55")), float(_arg("--camz", "150")))
	cam.look_at(Vector3(0, 12, -(colors) * 15.0), Vector3.UP)
	cam.make_current()
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png("%s_colors.png" % _arg("--out", ProjectSettings.globalize_path("res://../../work/cv")))
	get_tree().quit()
