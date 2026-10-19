class_name OptionUi
extends Control
# The option menu (notes/menus_screens.md section 1): twelve settings on two pages. Up / Down choose a row, Left / Right change the value, Q / E turn the
# page, G sets everything back to the defaults, T back to the values from when the menu was opened, Enter / Esc leave (the values are kept either way, like the game).

signal closed

const ROWS := [
	["SAVE CURSOR POSITION", ["ON", "OFF"]],
	["VIBRATION", ["ON", "OFF"]],
	["MESSAGE DISPLAY SPEED", ["NORMAL", "SPEEDY"]],
	["SOUND", ["STEREO", "MONO"]],
	["CLOCK DISPLAY", ["ON", "OFF"]],
	["TIME PROGRESS", ["NORMAL", "SPEEDY"]],
	["MAP DISPLAY DENSITY", ["1", "2", "3", "OFF"]],
	["DISPLAY DAMAGE (ENEMY)", ["ON", "OFF"]],
	["DISPLAY DAMAGE (PARTY)", ["ON", "OFF"]],
	["ENEMY'S HP DISPLAY", ["ON", "OFF"]],
	["DISPLAY CHARACTER NAME", ["ON", "OFF"]],
	["FARSIDE SOFT FOCUS", ["ON", "OFF"]],
]
const DEFAULTS := [0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0]

var models: DcModels
var values: Array = []
var previous: Array = []
var row := 0
var page := 0
var label: Label

func setup(session: DcModels) -> void:
	models = session
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.04, 0.12, 0.94)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	label = Label.new()
	label.position = Vector2(140, 90)
	label.add_theme_font_size_override("font_size", 28)
	add_child(label)
	for i in 12:
		values.append(models.get_option(i + 1))
	previous = values.duplicate()
	_render()

func _render() -> void:
	var lines := PackedStringArray()
	lines.append("OPTION %d" % (page + 1))
	lines.append("")
	for i in 6:
		var r := page * 6 + i
		var opts: Array = ROWS[r][1]
		var text := ""
		for k in opts.size():
			text += ("[%s] " if k == values[r] else " %s  ") % opts[k]
		lines.append(("▶ " if r == row else "   ") + "%-26s %s" % [ROWS[r][0], text])
	lines.append("")
	lines.append("[Q] [E] page    [Left] [Right] change    [G] defaults    [T] previous    [Enter] done")
	label.text = "\n".join(lines)

func _commit() -> void:
	for i in 12:
		models.set_option(i + 1, values[i])

func _unhandled_key_input(event: InputEvent) -> void:
	if not visible or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	var k: int = event.keycode
	if k == KEY_UP or k == KEY_W:
		row = page * 6 + posmod(row - page * 6 - 1, 6)
	elif k == KEY_DOWN or k == KEY_S:
		row = page * 6 + posmod(row - page * 6 + 1, 6)
	elif k == KEY_LEFT or k == KEY_A:
		var n: int = ROWS[row][1].size()
		values[row] = posmod(values[row] - 1, n)
	elif k == KEY_RIGHT or k == KEY_D:
		var n: int = ROWS[row][1].size()
		values[row] = posmod(values[row] + 1, n)
	elif k == KEY_Q or k == KEY_E:
		page = 1 - page
		row = page * 6 + (row % 6)
	elif k == KEY_G:
		values = DEFAULTS.duplicate()
	elif k == KEY_T:
		values = previous.duplicate()
	elif k == KEY_ENTER or k == KEY_SPACE or k == KEY_ESCAPE or k == KEY_X:
		_commit()
		closed.emit()
		queue_free()
		get_viewport().set_input_as_handled()
		return
	else:
		return
	_render()
	get_viewport().set_input_as_handled()
