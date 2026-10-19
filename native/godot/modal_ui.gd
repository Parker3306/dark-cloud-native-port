class_name ModalUi
extends Control
# The small full-screen questions the event scripts ask: the name of a character (_NAME_REGISTRY), the place to travel to (_WORLD_MAP) and the item to hand over
# (_GOTO_USE_ITEM), and the fishing screens: the bait to put on the hook, the exchange of fishing points and the record list.

signal done(value: int)

const TOWN_TEXTS := [10, 12, 14, 15, 17]   # _systeme.bin: Nolun, Matataki, Brownboo, Queens, Muska Lacka (maps 1..5)

static var auto_choose := false   # tests: answer at once with the first entry
var models: DcModels
var kind := 0
var args: Array = []
var title: Label
var edit: LineEdit
var list: ItemList
var note: Label
var chara := 0
var rows: Array = []

func setup(session: DcModels, k: int, a: Array) -> bool:
	models = session
	kind = k
	args = a
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.04, 0.12, 0.92)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	title = _label(Vector2(120, 120), 34)
	note = _label(Vector2(120, 400), 22)
	match kind:
		6: _name_entry()
		7: _world_map()
		5: _item_choice()
		8: _fish_exchange()
		9: _bait_choice()
		10: _fish_records()
		_: return false
	if auto_choose:
		call_deferred("_auto_answer")
	return true

func _auto_answer() -> void:
	if list != null and list.item_count > 0 and kind != 8 and kind != 10:
		list.item_activated.emit(0)
	else:
		_finish(0)

func _label(pos: Vector2, size: int) -> Label:
	var l := Label.new()
	l.position = pos
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 5)
	add_child(l)
	return l

func _name_entry() -> void:
	chara = args[0] if args.size() > 0 else 0
	title.text = "REGISTER NAME"
	edit = LineEdit.new()
	edit.position = Vector2(120, 200)
	edit.size = Vector2(500, 56)
	edit.max_length = 10
	edit.add_theme_font_size_override("font_size", 32)
	edit.text = models.get_hero_name(chara)
	edit.select_all()
	add_child(edit)
	edit.text_submitted.connect(func(t): _accept_name(t))
	edit.grab_focus()
	note.text = "Up to 10 characters.   [Enter] Accept   (an empty name keeps the default)"

func _accept_name(t: String) -> void:
	var name := t.strip_edges()
	if name == "":
		name = ["Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"][chara]
	for k in chara:   # an earlier character already has that name
		if String(models.get_hero_name(k)) == name:
			edit.text = ""
			note.text = "That name is taken by another character."
			return
	models.set_hero_name(chara, name)
	done.emit(0)
	queue_free()

func _world_map() -> void:
	title.text = "Where to?"
	list = ItemList.new()
	list.position = Vector2(120, 200)
	list.size = Vector2(500, 260)
	list.add_theme_font_size_override("font_size", 28)
	add_child(list)
	for i in TOWN_TEXTS.size():
		var map_no := i + 1
		if models.has_file("gedit/e%02d/mapinfo.cfg" % map_no):
			rows.append(map_no)
			list.add_item(models.get_system_text(TOWN_TEXTS[i]))
	if list.item_count > 0:
		list.select(0)
	list.grab_focus()
	list.item_activated.connect(func(i): _finish(rows[i]))
	note.text = "[Enter] Go   [Esc] Stay"

func _item_choice() -> void:
	title.text = "Which item?"
	list = ItemList.new()
	list.position = Vector2(120, 200)
	list.size = Vector2(500, 300)
	list.fixed_icon_size = Vector2i(32, 32)
	list.add_theme_font_size_override("font_size", 24)
	add_child(list)
	var allowed := {}
	for a in args:
		if int(a) >= 0:
			allowed[int(a)] = true
	for r in models.shop_inventory(false):
		if int(r["kind"]) == 0 and (allowed.is_empty() or allowed.has(int(r["id"]))):
			rows.append(int(r["id"]))
			list.add_item(String(r["name"]), models.get_item_icon(int(r["id"])))
	if list.item_count > 0:
		list.select(0)
	list.grab_focus()
	list.item_activated.connect(func(i): _finish(rows[i]))
	note.text = "[Enter] Give   [Esc] Cancel"

func _bait_choice() -> void:
	title.text = "Which bait?"
	list = ItemList.new()
	list.position = Vector2(120, 200)
	list.size = Vector2(500, 300)
	list.fixed_icon_size = Vector2i(32, 32)
	list.add_theme_font_size_override("font_size", 24)
	add_child(list)
	for b in models.fish_baits():
		rows.append(int(b["id"]))
		list.add_item("%s x%d" % [String(b["name"]), int(b["count"])], models.get_item_icon(int(b["id"])))
	if list.item_count == 0:
		list.add_item("(no bait in the bag)")
	else:
		list.select(0)
	list.grab_focus()
	list.item_activated.connect(func(i):
		if i < rows.size() and models.fish_take_bait(int(rows[i])):
			_finish(int(rows[i])))
	note.text = "[Enter] Use   [Esc] Cancel"

func _fish_exchange() -> void:
	title.text = "Fishing Points: %d pt." % models.fish_points()
	list = ItemList.new()
	list.position = Vector2(120, 200)
	list.size = Vector2(560, 300)
	list.fixed_icon_size = Vector2i(32, 32)
	list.add_theme_font_size_override("font_size", 24)
	add_child(list)
	_exchange_fill()
	list.grab_focus()
	list.item_activated.connect(_exchange_buy)
	note.text = "[Enter] Exchange   [Esc] Leave"

func _exchange_buy(i: int) -> void:
	if i >= rows.size():
		return
	var r := models.fish_exchange(int(rows[i]))
	if r == 0:
		note.text = "Exchanged."
	elif r == 1:
		note.text = "Not enough Fishing Points."
	elif r == 2:
		note.text = "Too many items."
	else:
		note.text = ""
	title.text = "Fishing Points: %d pt." % models.fish_points()
	_exchange_fill()

func _exchange_fill() -> void:
	var keep := list.get_selected_items()[0] if list.get_selected_items().size() > 0 else 0
	list.clear()
	rows.clear()
	for r in models.fish_exchange_rows():
		rows.append(int(r["row"]))
		list.add_item("%s   %d pt." % [String(r["name"]), int(r["cost"])], models.get_item_icon(int(r["id"])))
	if list.item_count > 0:
		list.select(mini(keep, list.item_count - 1))

func _fish_records() -> void:
	title.text = "Fish Record"
	list = ItemList.new()
	list.position = Vector2(120, 200)
	list.size = Vector2(500, 300)
	list.add_theme_font_size_override("font_size", 26)
	add_child(list)
	for r in models.fish_records():
		list.add_item("%2d.  %s   %.1f cm" % [int(r["rank"]), String(r["name"]), float(r["size"]) / 10.0])
	if list.item_count == 0:
		list.add_item("(nothing caught yet)")
	list.select(0)
	list.grab_focus()
	list.item_activated.connect(func(_i): _finish(0))
	note.text = "[Esc] Close"

func _finish(value: int) -> void:
	done.emit(value)
	queue_free()

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and (event.keycode == KEY_ESCAPE) and kind != 6:
		get_viewport().set_input_as_handled()
		_finish(0 if (kind == 7 or kind == 9 or kind == 8 or kind == 10) else -1)
