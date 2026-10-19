extends Node3D
# Harness for the opening: --steps 0,600,1200 --out prefix  (runs the script with the game's timing and takes a screenshot at each step count; WAIT_KEY is released at once)

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
	var snd: DcSound = models.build_sound() if _arg("--sound", "") != "" else null
	if snd != null:
		add_child(snd)
	var run := OpeningRun.new()
	add_child(run)
	if not run.start(models, snd):
		get_tree().quit(1)
		return
	run.op.set_auto_run(false)
	run.op.set_auto_key(true)
	run.set_physics_process(false)
	var prefix := _arg("--out", "")
	var done := 0
	for s in _arg("--steps", "0").split(",", false):
		var target := int(s)
		while done < target:
			var n := mini(target - done, 200)
			run.op.step_ticks(n)
			done += n
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		if prefix != "":
			get_viewport().get_texture().get_image().save_png("%s_s%d.png" % [prefix, target])
		print("SHOT ", target, " ", run.op.get_ui(), " ", run.op.get_debug(), " bgm ", snd.bgm_playing() if snd != null else false)
	get_tree().quit()
