class_name AtlaViewUi
extends Control
# The Atla board of the town as it stands (read only: the village menu's "Georama Parts"): pages of parts with their tips, installed or still missing.
# Left / Right turn the page, Up / Down read the description of a tip, Esc closes.

signal closed

var walk: DcTownWalk
var pages: Array = []
var page := 0
var row := 0
var label: Label

func setup(town_walk: DcTownWalk) -> void:
	walk = town_walk
	pages = walk.georama_board()
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.04, 0.12, 0.95)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	label = Label.new()
	label.position = Vector2(120, 70)
	label.add_theme_font_size_override("font_size", 24)
	add_child(label)
	_render()

func _render() -> void:
	var lines := PackedStringArray()
	if pages.is_empty():
		lines.append("No Georama parts yet. Collect Atla in the dungeons.")
	else:
		var p: Dictionary = pages[page]
		lines.append("Georama parts  %d / %d     [Left] [Right] page   [Esc] close" % [page + 1, pages.size()])
		lines.append("")
		lines.append("%s     copies %d / %d%s" % [p["name"], p["placed"], p["max"], "     COMPLETE" if p["complete"] else ""])
		var shown: Array = []
		for s in p["slots"]:
			if s["visible"]:
				shown.append(s)
		row = clampi(row, 0, maxi(shown.size() - 1, 0))
		for i in shown.size():
			var s: Dictionary = shown[i]
			lines.append(("▶ " if i == row else "   ") + "%s  %s" % [("●" if s["installed"] else "○"), s["name"]])
		if not shown.is_empty():
			lines.append("")
			lines.append(String(shown[row]["desc"]).replace("\n", " "))
	label.text = "\n".join(lines)

func _unhandled_key_input(event: InputEvent) -> void:
	if not visible or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	match event.keycode:
		KEY_LEFT, KEY_A:
			if not pages.is_empty(): page = posmod(page - 1, pages.size())
			row = 0
		KEY_RIGHT, KEY_D:
			if not pages.is_empty(): page = posmod(page + 1, pages.size())
			row = 0
		KEY_UP, KEY_W: row = maxi(row - 1, 0)
		KEY_DOWN, KEY_S: row += 1
		KEY_ESCAPE, KEY_X, KEY_ENTER:
			closed.emit()
			queue_free()
			get_viewport().set_input_as_handled()
			return
		_: return
	_render()
	get_viewport().set_input_as_handled()
