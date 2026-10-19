class_name SaveUi
extends Control
# The save / load screen (notes/menus_screens.md section 2, simplified): three slots and, when loading, the automatic save of the last visit to a dungeon.
# Up / Down choose, Enter saves (asks before overwriting) or loads, Esc leaves. `done(path)` gives the file that was loaded ("" when nothing was) or saved.

signal done(path: String)

const SLOTS := ["user://save1.dc", "user://save2.dc", "user://save3.dc"]
const AUTOSAVE := "user://autosave.dc"
const TOWNS := ["Norune", "Matataki", "Brownboo", "Queens", "Muska Lacka"]

var models: DcModels
var saving := true
var paths: Array = []
var infos: Array = []
var row := 0
var asking := false
var label: Label
var note := ""

func setup(session: DcModels, save_mode: bool) -> void:
	models = session
	saving = save_mode
	paths = SLOTS.duplicate()
	if not saving:
		paths.append(AUTOSAVE)
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.04, 0.12, 0.95)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	label = Label.new()
	label.position = Vector2(140, 100)
	label.add_theme_font_size_override("font_size", 28)
	add_child(label)
	_reload()
	_render()

func _reload() -> void:
	infos.clear()
	for p in paths:
		infos.append(models.peek_save(ProjectSettings.globalize_path(p)))

func _slot_text(i: int) -> String:
	var n := "Auto save" if paths[i] == AUTOSAVE else "Slot %d" % (i + 1)
	var d: Dictionary = infos[i]
	if not d["exists"]:
		return "%-10s  - empty -" % n
	var secs := int(d["play_time"]) / 60
	var town := int(d["town"])
	return "%-10s  %s   day %d   %d:%02d:%02d   %d G   %s" % [n, d["name"], d["day"], secs / 3600, (secs / 60) % 60, secs % 60, d["gold"], TOWNS[town] if town >= 0 and town < TOWNS.size() else ""]

func _render() -> void:
	var lines := PackedStringArray()
	lines.append("SAVE" if saving else "LOAD")
	lines.append("")
	for i in paths.size():
		lines.append(("▶ " if i == row else "   ") + _slot_text(i))
	lines.append("")
	if asking:
		lines.append("Overwrite this slot?   [Enter] Yes   [Esc] No")
	elif note != "":
		lines.append(note)
	else:
		lines.append("[Enter] %s    [Esc] back" % ("save" if saving else "load"))
	label.text = "\n".join(lines)

func _unhandled_key_input(event: InputEvent) -> void:
	if not visible or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	var k: int = event.keycode
	note = ""
	if asking:
		if k == KEY_ENTER or k == KEY_SPACE:
			_do()
		elif k == KEY_ESCAPE or k == KEY_X:
			asking = false
	elif k == KEY_UP or k == KEY_W:
		row = posmod(row - 1, paths.size())
	elif k == KEY_DOWN or k == KEY_S:
		row = posmod(row + 1, paths.size())
	elif k == KEY_ENTER or k == KEY_SPACE:
		if saving and infos[row]["exists"]:
			asking = true
		elif not saving and not infos[row]["exists"]:
			note = "Nothing is saved there."
		else:
			_do()
	elif k == KEY_ESCAPE or k == KEY_X:
		done.emit("")
		queue_free()
	else:
		return
	if is_inside_tree():
		_render()
		get_viewport().set_input_as_handled()

func _do() -> void:
	asking = false
	var path: String = paths[row]
	var real := ProjectSettings.globalize_path(path)
	var ok := models.save_game(real) if saving else models.load_game(real)
	if ok:
		done.emit(path)
		queue_free()
	else:
		note = "That did not work."
