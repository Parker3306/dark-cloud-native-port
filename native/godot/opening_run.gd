class_name OpeningRun
extends Node3D
# The opening cutscene (new game): DcOpening runs opdat/opening.scr; this node adds the light, the fades, the speech balloons and the keys
# (Enter / Space moves the text on, Esc skips).

signal finished

var models: DcModels
var op: DcOpening
var world_env: WorldEnvironment
var lights: Array = []
var fade_rect: ColorRect
var balloon: Panel
var balloon_label: Label
var tail: Control
var layer: CanvasLayer
var last_scene := -1
var sound: DcSound
var tail_target := Vector2.ZERO
var tail_on := false
var music_started := false

func start(session: DcModels, snd: DcSound = null) -> bool:
	models = session
	sound = snd
	op = models.build_opening()
	if op == null:
		return false
	add_child(op)
	op.get_camera().current = true
	world_env = WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	world_env.environment = env
	add_child(world_env)
	layer = CanvasLayer.new()
	layer.layer = 20
	add_child(layer)
	balloon = Panel.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.97, 0.97, 0.9, 0.92)
	style.border_color = Color(0.2, 0.2, 0.3)
	style.set_border_width_all(3)
	style.set_corner_radius_all(14)
	balloon.add_theme_stylebox_override("panel", style)
	balloon.visible = false
	balloon_label = Label.new()
	balloon_label.add_theme_color_override("font_color", Color(0.05, 0.05, 0.15))
	balloon_label.add_theme_font_size_override("font_size", 26)
	balloon_label.position = Vector2(16, 10)
	balloon.add_child(balloon_label)
	tail = Control.new()
	tail.set_anchors_preset(Control.PRESET_FULL_RECT)
	tail.mouse_filter = Control.MOUSE_FILTER_IGNORE
	tail.draw.connect(_draw_tail)
	layer.add_child(tail)
	layer.add_child(balloon)
	fade_rect = ColorRect.new()
	fade_rect.color = Color.BLACK
	fade_rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	fade_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(fade_rect)
	_apply_environment()
	_scene_music(0)
	return true

func _apply_environment() -> void:
	var e: Dictionary = op.get_environment()
	var env := world_env.environment
	env.ambient_light_color = e.get("ambient", Color(0.2, 0.2, 0.2))
	env.ambient_light_energy = 1.0
	env.background_color = e.get("bg", Color.BLACK)
	if e.has("fog_far"):
		env.fog_enabled = true
		env.fog_mode = Environment.FOG_MODE_DEPTH
		env.fog_depth_begin = e["fog_near"]
		env.fog_depth_end = e["fog_far"]
		env.fog_light_color = e["fog_color"]
		env.fog_sky_affect = 0.0
	else:
		env.fog_enabled = false
	for l in lights:
		l.queue_free()
	lights.clear()
	for l in e.get("lights", []):
		var d := DirectionalLight3D.new()
		var dir: Vector3 = l["direction"]
		add_child(d)
		d.look_at_from_position(Vector3.ZERO, -dir.normalized(), Vector3.UP)
		d.light_color = l["color"]
		d.light_energy = 1.0
		lights.append(d)

func _process(_delta: float) -> void:
	if op == null:
		return
	var ui: Dictionary = op.get_ui()
	var scene: int = ui.get("scene", 0)
	if scene != last_scene:
		last_scene = scene
		_apply_environment()
	if not music_started and sound != null and int(ui.get("steps", 0)) > 10 and int(ui.get("scene", 0)) == 0:
		music_started = true
		sound.play_bgm(0)
	var a: float = ui.get("fade_alpha", 0.0)
	fade_rect.color = Color(1, 1, 1, a) if ui.get("fade_white", false) else Color(0, 0, 0, a)
	var vp := get_viewport().get_visible_rect().size
	var k := Vector2(vp.x / 640.0, vp.y / 448.0)
	if ui.has("balloon"):
		var b: Dictionary = ui["balloon"]
		var txt: String = b["text"]
		if txt != "":
			balloon_label.text = txt.replace("\n", "\n")
			balloon.visible = true
			balloon.size = balloon_label.get_combined_minimum_size() + Vector2(32, 20)
			balloon.position = Vector2(b["x"], b["y"]) * k
			tail_on = float(b["tx"]) != 0.0
			tail_target = Vector2(b["tx"], b["ty"]) * k
			tail.queue_redraw()
		else:
			balloon.visible = false
			tail_on = false
			tail.queue_redraw()
	else:
		balloon.visible = false
		if tail_on:
			tail_on = false
			tail.queue_redraw()
	if ui.get("finished", false):
		finished.emit()
		set_process(false)
	for e in op.take_events():
		_sound_event(e)

func _draw_tail() -> void:
	if not tail_on:
		return
	var c := balloon.position + balloon.size * 0.5
	var edge := c + (tail_target - c).normalized() * minf(balloon.size.x, balloon.size.y) * 0.5
	tail.draw_colored_polygon(PackedVector2Array([edge + Vector2(-14, 0), edge + Vector2(14, 0), tail_target]), Color(0.97, 0.97, 0.9, 0.92))

# the script's sound commands: SE (port 0xE or the special bank 0xC), SE_STOP, SOUND_FADE, and the music of each scene (SoundStop before SCENE 1, 3, 5, 8, 9)
func _scene_music(scene: int) -> void:
	if sound == null:
		return
	var pk: Dictionary = op.get_music_pack(scene)
	if pk.is_empty():
		return
	sound.stop_bgm(0)
	sound.load_music_pack(pk["data"], pk["stem"])
	if scene != 0:
		sound.play_bgm(0)

func _sound_event(e: Dictionary) -> void:
	if sound == null:
		return
	var a: Array = e["a"]
	match int(e["kind"]):
		0:
			sound.play_note(14 if int(a[0]) == 0 else 12, int(a[1]), int(a[2]))
		1:
			sound.stop_note(int(a[1]), int(a[2]))
		2:
			if float(a[1]) <= 0.0:
				sound.bgm_fade_out(int(240.0 / maxf(absf(float(a[2])), 0.5)))
		7:
			var sc := int(a[0])
			if sc in [1, 3, 5, 8, 9]:
				_scene_music(sc)

func _unhandled_key_input(event: InputEvent) -> void:
	if not (event is InputEventKey) or not event.pressed or event.echo:
		return
	match event.keycode:
		KEY_ENTER, KEY_SPACE, KEY_KP_ENTER, KEY_Z:
			op.press_ok()
		KEY_ESCAPE:
			op.skip()
