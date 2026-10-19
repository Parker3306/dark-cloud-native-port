class_name ShopUi
extends Control
# The item shop and the storage ("check-in") counter (notes/menus_shops.md). The rules are in dccore/shop.h; this is the screen: the shop's (or the storage's)
# things on the left, the player's on the right, three tabs (items, weapons, synthesis items), the keeper's lines at the bottom.
# Keys: Up/Down choose, Left/Right change the side, Q/E change the tab, Enter buys / sells / stores / takes, Esc leaves.

signal closed

enum Result { OK, NO_MONEY, NO_SPACE, CANNOT, EQUIPPED, TOO_MUCH_GOLD, STORAGE_FULL, NOTHING }
const TAB_NAMES := ["Items", "Weapons", "Synthesis"]

var models: DcModels
var shop_no := 0
var charge := false
var tab := 0
var side := 0                       # 0 the shop / storage, 1 the player
var left: ItemList
var right: ItemList
var talk: Label
var gold_label: Label
var title: Label
var desc: Label
var tab_label: Label
var left_rows: Array = []
var right_rows: Array = []
var asking := ""                    # "", "leave" or the pending action
var pending := {}
var idle_ticks := 0

func setup(session: DcModels, shop: int, is_charge: bool) -> void:
	models = session
	shop_no = shop
	charge = is_charge
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.04, 0.12, 0.9)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	title = _label(Vector2(40, 18), 30)
	tab_label = _label(Vector2(40, 62), 22)
	gold_label = _label(Vector2(760, 18), 26)
	left = _list(Vector2(40, 100))
	right = _list(Vector2(590, 100))
	desc = _label(Vector2(40, 470), 22)
	talk = _label(Vector2(40, 540), 24)
	talk.add_theme_color_override("font_color", Color(1.0, 0.95, 0.7))
	left.item_activated.connect(func(_i): _activate(0))
	right.item_activated.connect(func(_i): _activate(1))
	left.item_selected.connect(func(_i): _describe())
	right.item_selected.connect(func(_i): _describe())
	title.text = ("Check-in counter" if charge else "Shop")
	_say(_text_base() + 0)
	_refresh()
	_focus_side(0)

func _label(pos: Vector2, size: int) -> Label:
	var l := Label.new()
	l.position = pos
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 5)
	add_child(l)
	return l

func _list(pos: Vector2) -> ItemList:
	var l := ItemList.new()
	l.position = pos
	l.size = Vector2(530, 350)
	l.fixed_icon_size = Vector2i(32, 32)
	l.add_theme_font_size_override("font_size", 22)
	l.max_columns = 1
	l.auto_height = false
	add_child(l)
	return l

func _text_base() -> int:
	return (2000 + 100 * shop_no) if charge else (4000 + 100 * shop_no)

func _say(id: int) -> void:
	var t: String = models.shop_text(id)
	if t == "":
		t = ""
	talk.text = t.replace("\n", " ")

func _focus_side(s: int) -> void:
	side = s
	var l := left if s == 0 else right
	l.grab_focus()
	if l.item_count > 0 and l.get_selected_items().is_empty():
		l.select(0)
	_describe()
	_say(_text_base() + (20 if s == 0 else 30))

func _kind_of_tab() -> int:
	return tab

func _refresh() -> void:
	var gold: int = models.get_village_status()["gold"]
	gold_label.text = "%d G" % gold
	tab_label.text = "[Q] %s [E]   " % TAB_NAMES[tab]
	left.clear()
	right.clear()
	left_rows.clear()
	right_rows.clear()
	if charge:
		for r in models.storage_list(tab):
			r["kind"] = tab
			left_rows.append(r)
			left.add_item(String(r["name"]), models.get_item_icon(int(r["id"])))
		var cap: Dictionary = models.storage_capacity(tab)
		title.text = "Check-in counter     stored %d / %d" % [cap["used"], cap["max"]]
	else:
		var n := 0
		for r in models.shop_stock(shop_no):
			if int(r["kind"]) != tab:
				continue
			left_rows.append(r)
			left.add_item("%s     %d G" % [r["name"], r["price"]], models.get_item_icon(int(r["id"])))
			n += 1
	for r in models.shop_inventory(not charge):
		if int(r["kind"]) != tab:
			continue
		right_rows.append(r)
		var label := String(r["name"])
		if not charge:
			label += "     %s" % (("%d G" % r["price"]) if int(r["price"]) >= 0 else "-")
		if r["equipped"]:
			label += "  (equipped)"
		right.add_item(label, models.get_item_icon(int(r["id"])))
	if left.item_count > 0:
		left.select(0)
	if right.item_count > 0:
		right.select(0)
	_describe()

func _describe() -> void:
	var l := left if side == 0 else right
	var rows := left_rows if side == 0 else right_rows
	var sel := l.get_selected_items()
	if sel.is_empty() or sel[0] >= rows.size():
		desc.text = ""
		return
	desc.text = String(models.item_description(int(rows[sel[0]]["id"]))).replace("\n", " ")

func _activate(s: int) -> void:
	if asking != "":
		return
	var l := left if s == 0 else right
	var rows := left_rows if s == 0 else right_rows
	var sel := l.get_selected_items()
	if sel.is_empty() or sel[0] >= rows.size():
		return
	var row: Dictionary = rows[sel[0]]
	pending = {"side": s, "row": row}
	if charge:
		_say(2000 + 100 * shop_no + (0 if s == 1 else 0))
		asking = "do"
		talk.text = ("Check in %s?" if s == 1 else "Take %s?") % row["name"] + "   [Enter] Yes   [Esc] No"
	elif s == 0:
		asking = "do"
		talk.text = "Buy %s for %d G?   [Enter] Yes   [Esc] No" % [row["name"], row["price"]]
	else:
		if int(row["price"]) < 0:
			_report(Result.CANNOT if int(row["why"]) == Result.CANNOT else int(row["why"]))
			return
		asking = "do"
		talk.text = "Sell %s for %d G?   [Enter] Yes   [Esc] No" % [row["name"], row["price"]]

func _report(code: int) -> void:
	var base := _text_base()
	var id := base + 55
	if charge:
		match code:
			Result.CANNOT, Result.EQUIPPED: id = base + 50
			Result.NO_SPACE: id = base + 55
			Result.STORAGE_FULL: id = base + 58
	else:
		match code:
			Result.NO_MONEY: id = base + 52
			Result.NO_SPACE: id = base + 55
			Result.CANNOT: id = base + 54
			Result.EQUIPPED: id = base + 53
			Result.TOO_MUCH_GOLD: id = base + 56
	_say(id)

func _do_pending() -> void:
	var row: Dictionary = pending["row"]
	var s: int = pending["side"]
	var code := 0
	if charge:
		if s == 1:
			code = models.storage_store(int(row["kind"]), int(row["owner"]), int(row["slot"]))
		else:
			code = models.storage_take(tab, int(row["slot"]))
	elif s == 0:
		var idx := 0
		var rows := models.shop_stock(shop_no)
		for i in rows.size():
			if int(rows[i]["id"]) == int(row["id"]):
				idx = i
				break
		code = models.shop_buy(shop_no, idx)
	else:
		code = models.shop_sell(int(row["kind"]), int(row["owner"]), int(row["slot"]))
	asking = ""
	if code == Result.OK:
		_say(_text_base() + (75 if not charge else 0))
		var keep_l := left.get_selected_items()
		var keep_r := right.get_selected_items()
		_refresh()
		if not keep_l.is_empty() and left.item_count > 0:
			left.select(mini(keep_l[0], left.item_count - 1))
		if not keep_r.is_empty() and right.item_count > 0:
			right.select(mini(keep_r[0], right.item_count - 1))
		_describe()
	else:
		_report(code)

func _unhandled_key_input(event: InputEvent) -> void:
	if not visible or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	var k: int = event.keycode
	if asking != "":
		if k == KEY_ENTER or k == KEY_SPACE or k == KEY_KP_ENTER:
			if asking == "leave":
				_close()
			else:
				_do_pending()
		elif k == KEY_ESCAPE or k == KEY_X:
			asking = ""
			_say(_text_base() + 20)
		get_viewport().set_input_as_handled()
		return
	if k == KEY_LEFT:
		_focus_side(0)
	elif k == KEY_RIGHT:
		_focus_side(1)
	elif k == KEY_Q:
		tab = (tab + 2) % 3
		_refresh()
	elif k == KEY_E:
		tab = (tab + 1) % 3
		_refresh()
	elif k == KEY_ESCAPE or k == KEY_X:
		asking = "leave"
		talk.text = "Are you leaving?   [Enter] Leave   [Esc] Stay"
	else:
		return
	get_viewport().set_input_as_handled()

func _close() -> void:
	_say(_text_base() + 91)
	closed.emit()
	queue_free()
