class_name TownEditUi
extends Control
# The 2D side of the Georama edit mode (notes/georama_edit.md 4.9): the help bar, the name of the part under the cursor, the edit menu, the Atla board and
# the Georama analysis. The rules and the data are in DcTownWalk (georama_*).

const MENU_ENTRIES := ["Config&Assembly", "Move&Remove", "Georama Analysis", "Save", "Options", "Manuals"]
const MENU_HELP := ["Configure&assemble georama parts.", "Move&remove georama parts.", "Analyze current town status.", "Save game progress.", "Change game configurations.", "Instruction on controls."]
const SE_PLACE := 13
const SE_REMOVE := 14
const SE_MENU := 1
const SE_DENIED := 2

var run: TownRun
var help: Label
var hover: Label
var wishes: Label
var panel: PanelContainer
var text: Label
var mode := ""    # "" | "menu" | "board" | "analysis"
var menu_index := 0
var page := 0
var row := 0
var in_tray := false
var tray_cell := 0
var note := ""

func setup(owner_run: TownRun) -> void:
	run = owner_run
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	visible = false
	help = Label.new()
	help.position = Vector2(40, 396)
	help.add_theme_font_size_override("font_size", 24)
	help.add_theme_color_override("font_outline_color", Color.BLACK)
	help.add_theme_constant_override("outline_size", 6)
	add_child(help)
	hover = Label.new()
	hover.add_theme_font_size_override("font_size", 22)
	hover.add_theme_color_override("font_outline_color", Color.BLACK)
	hover.add_theme_constant_override("outline_size", 6)
	add_child(hover)
	wishes = Label.new()
	wishes.position = Vector2(20, 20)
	wishes.add_theme_font_size_override("font_size", 22)
	wishes.add_theme_color_override("font_outline_color", Color.BLACK)
	wishes.add_theme_constant_override("outline_size", 6)
	add_child(wishes)
	panel = PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.08, 0.2, 0.92)
	style.border_color = Color(0.9, 0.85, 0.6, 0.9)
	style.set_border_width_all(2)
	style.set_corner_radius_all(8)
	style.content_margin_left = 18
	style.content_margin_right = 18
	style.content_margin_top = 12
	style.content_margin_bottom = 12
	panel.add_theme_stylebox_override("panel", style)
	panel.visible = false
	text = Label.new()
	text.add_theme_font_size_override("font_size", 20)
	panel.add_child(text)
	add_child(panel)

func _walk() -> DcTownWalk:
	return run.walk

func update(ed: Dictionary) -> void:
	var active: bool = ed.get("active", false)
	visible = active
	if not active:
		mode = ""
		panel.visible = false
		return
	if ed.get("menu", false) and mode == "":
		menu_index = 0
		mode = "board" if ed.get("board_first", false) else "menu"
		page = 0
		row = 0
		in_tray = false
		_render()
	elif not ed.get("menu", false) and mode != "":
		mode = ""
	var status: int = ed.get("status", -1)
	var line := ""
	if status == 0 or status == 1:
		line = "%s   %d/%d      [Enter] Place   [Z] [C] Rotate   [T] Menu" % [ed.get("part_name", ""), ed.get("placed", 0), ed.get("max", 0)]
	elif status == 3:
		line = "[Enter] Pick up   [G] Put away   [T] Menu   [B] Walking mode"
	else:
		line = "[T] Georama Menu      [B] Walking Mode" + ("" if ed.get("can_leave", false) else "  (not here)")
	help.text = line if mode == "" else (note if note != "" else "")
	var vp := get_viewport().get_visible_rect().size
	help.position = Vector2(40, vp.y - 70)
	wishes.text = "Wishes %d / %d" % [ed.get("requests_met", 0), ed.get("requests_total", 0)]
	var over: String = ed.get("over_name", "")
	hover.text = over
	hover.visible = over != "" and ed.has("screen")
	if hover.visible:
		var sp: Vector2 = ed["screen"]
		hover.position = sp + Vector2(20, -60)
	panel.visible = mode != ""
	if mode != "":
		panel.reset_size()
		panel.position = Vector2((vp.x - panel.size.x) * 0.5, maxf((vp.y - panel.size.y) * 0.35, 10.0))

func notice(n: Dictionary) -> void:
	var snd: DcSound = run.sound
	if snd == null:
		return
	match String(n["kind"]):
		"place": snd.play_se(SE_PLACE)
		"remove": snd.play_se(SE_REMOVE)
		"menu": snd.play_se(SE_MENU)
		"denied": snd.play_se(SE_DENIED)

# ---------------------------------------------------------------------------------------------------------------------------------------------- input
func handle_key(event: InputEvent) -> bool:
	if mode == "" or not (event is InputEventKey) or not event.pressed or event.echo:
		return false
	var w := _walk()
	var k: int = event.keycode
	var up := k == KEY_UP or k == KEY_W
	var down := k == KEY_DOWN or k == KEY_S
	var left := k == KEY_LEFT or k == KEY_A
	var right := k == KEY_RIGHT or k == KEY_D
	var ok := k == KEY_ENTER or k == KEY_SPACE or k == KEY_KP_ENTER
	var back := k == KEY_ESCAPE or k == KEY_X or k == KEY_BACKSPACE
	match mode:
		"menu":
			if up: menu_index = (menu_index + MENU_ENTRIES.size() - 1) % MENU_ENTRIES.size()
			elif down: menu_index = (menu_index + 1) % MENU_ENTRIES.size()
			elif back:
				w.edit_menu_done(0)
				mode = ""
			elif ok:
				_menu_choose()
			note = ""
		"board":
			_board_key(w, up, down, left, right, ok, back, k)
		"analysis":
			if ok or back:
				mode = "menu"
	if mode != "":
		_render()
	else:
		panel.visible = false
	return true

func _menu_choose() -> void:
	var w := _walk()
	match menu_index:
		0:
			mode = "board"
			page = 0
			row = 0
			in_tray = false
		1:
			w.edit_menu_done(1)
			mode = ""
		2:
			mode = "analysis"
		3:
			run.open_save()
		4:
			run.open_options()
		_:
			note = "(not available yet)"

func _board_key(w: DcTownWalk, up: bool, down: bool, left: bool, right: bool, ok: bool, back: bool, k: int) -> void:
	var pages: Array = w.georama_board()
	if k == KEY_TAB:
		in_tray = not in_tray
		return
	if k == KEY_G:
		w.georama_sort_tray()
		return
	if back:
		if w.georama_hand() >= 0:
			w.georama_cancel_hand()
		else:
			mode = "menu" if not _opened_on_board() else ""
			if mode == "":
				w.edit_menu_done(0)
		return
	if in_tray:
		if left: tray_cell = maxi(tray_cell - 1, 0)
		elif right: tray_cell = mini(tray_cell + 1, 127)
		elif up:
			if tray_cell < 8: in_tray = false
			else: tray_cell -= 8
		elif down: tray_cell = mini(tray_cell + 8, 127)
		elif ok:
			if not w.georama_press_tray(tray_cell):
				pass
		return
	if pages.is_empty():
		return
	page = clampi(page, 0, pages.size() - 1)
	var p: Dictionary = pages[page]
	var slots := _visible_slots(p)
	if left or k == KEY_Q:
		page = (page + pages.size() - 1) % pages.size()
		row = 0
	elif right or k == KEY_E:
		page = (page + 1) % pages.size()
		row = 0
	elif up:
		row = maxi(row - 1, 0)
	elif down:
		if row >= slots.size():
			in_tray = true
		else:
			row += 1
	elif ok:
		if row == 0:
			if w.georama_select_part(int(p["part"])):
				w.edit_menu_done(0)
				mode = ""
			else:
				note = "Cannot place this part now."
		else:
			var s: Dictionary = slots[row - 1]
			if not w.georama_press_slot(int(p["part"]), int(s["k"])):
				note = "That does not fit here."
			else:
				note = ""

func _opened_on_board() -> bool:
	return bool(_walk().get_edit_ui().get("board_first", false))

func _visible_slots(p: Dictionary) -> Array:
	var out: Array = []
	for s in p["slots"]:
		if s["visible"]:
			out.append(s)
	return out

# ---------------------------------------------------------------------------------------------------------------------------------------------- drawing
func _render() -> void:
	var w := _walk()
	var lines := PackedStringArray()
	match mode:
		"menu":
			lines.append("Georama")
			for i in MENU_ENTRIES.size():
				lines.append(("▶ " if i == menu_index else "   ") + MENU_ENTRIES[i])
			lines.append("")
			lines.append(MENU_HELP[menu_index])
		"analysis":
			var a: Array = w.georama_analysis()
			lines.append("Georama Analysis")
			var names := ["Overall", "Complete houses", "Wishes met"]
			for i in a.size():
				var n: int = int(a[i])
				lines.append("%-16s %s %3d%%" % [names[i], "█".repeat(n / 5) + "░".repeat(20 - n / 5), n])
		"board":
			_render_board(w, lines)
	if note != "":
		lines.append("")
		lines.append(note)
	text.text = "\n".join(lines)

func _render_board(w: DcTownWalk, lines: PackedStringArray) -> void:
	var pages: Array = w.georama_board()
	var hand: int = w.georama_hand()
	var hand_name := "-" if hand < 0 else String(w.georama_chip_name(hand, false))
	if pages.is_empty():
		lines.append("Atla board")
		lines.append("No Georama parts yet. Collect Atla in the dungeons.")
	else:
		page = clampi(page, 0, pages.size() - 1)
		var p: Dictionary = pages[page]
		var slots := _visible_slots(p)
		row = clampi(row, 0, slots.size())
		lines.append("Atla board  %d / %d      [A][D] page   [Tab] tray   [G] sort tray   Tip in hand: %s" % [page + 1, pages.size(), hand_name])
		var head := "%s     copies %d / %d%s" % [p["name"], p["placed"], p["max"], "     COMPLETE" if p["complete"] else ""]
		lines.append(("▶ " if row == 0 and not in_tray else "   ") + head)
		for i in slots.size():
			var s: Dictionary = slots[i]
			lines.append(("▶ " if row == i + 1 and not in_tray else "   ") + "   %s  %s" % [("●" if s["installed"] else "○"), s["name"]])
		if row >= 1 and not in_tray:
			lines.append("")
			lines.append(String(slots[row - 1]["desc"]).replace("\n", " "))
	lines.append("")
	lines.append("Tip tray" + ("  ◀" if in_tray else ""))
	var tray: Array = w.georama_tray()
	var shown := 0
	var last := 0
	for i in tray.size():
		if int(tray[i]) >= 0:
			last = i
	var cells := maxi(24, maxi(last / 8 + 1, tray_cell / 8 + 1) * 8)
	for r in cells / 8:
		var l := ""
		for c in 8:
			var idx := r * 8 + c
			var chip: int = int(tray[idx]) if idx < tray.size() else -1
			var nm := "··" if chip < 0 else String(w.georama_chip_name(chip, false)).substr(0, 8)
			var sel := in_tray and idx == tray_cell
			l += ("[%-8s]" if sel else " %-8s ") % nm
		lines.append(l)
		shown += 1
	if in_tray:
		var chip: int = int(tray[tray_cell])
		if chip >= 0:
			lines.append(String(w.georama_chip_name(chip, true)).replace("\n", " "))
