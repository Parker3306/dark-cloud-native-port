class_name WeaponMenuUi
extends Control
# The weapon page (notes/menus.md C.4 and D): the weapons of a character with their stats, and the commands Equip, Customize (attachments), Attribute,
# Repair, Upgrade, Status break, Build-up and Discard. The rules are in dccore/weapon_ops.h; `walker` is the running floor (its record is the one that
# counts) or null in the town.
# Keys: Up/Down choose, Q/E change the character, Enter opens the commands / does the command, Esc goes back, in Customize: Enter puts the chosen
# synthesis item into the chosen hole (Tab changes the list), Delete takes the attachment out again.

signal closed

enum Result { OK, CURSED, LIMITED_ZONE, NOT_IN_PARTY, NOT_ALLOWED, BROKEN_DEFAULT, MAX_LEVEL, NEEDS_POWDER, NOTHING_TO_DO, ATTACH_FULL, HAS_ATTACHMENTS, ZERO_ELEMENT, ELEMENT_REQUIRED, SERPENT_SWORD, DEFAULT_WEAPON, NOT_BUILDABLE }
const COMMANDS := ["Equip", "Customize", "Attribute", "Repair", "Upgrade", "Status break", "Build-up", "Discard"]
const COMMAND_HELP := [104, 105, 106, 107, 108, 109, 110, 164]
const ELEMENTS := ["Fire", "Ice", "Thunder", "Wind", "Holy"]
const SLAYERS := ["Dragon", "Undead", "Marine", "Rock", "Plant", "Beast", "Sky", "Metal", "Mimic", "Mage"]
const REFUSALS := {
	Result.CURSED: 411, Result.LIMITED_ZONE: 412, Result.NOT_IN_PARTY: 163, Result.BROKEN_DEFAULT: 170, Result.MAX_LEVEL: 169, Result.ATTACH_FULL: 162,
	Result.HAS_ATTACHMENTS: 168, Result.ZERO_ELEMENT: 161, Result.ELEMENT_REQUIRED: 171, Result.SERPENT_SWORD: 167, Result.DEFAULT_WEAPON: 166,
}

var models: DcModels
var walker: DcWalker
var ch := 0
var weapons: Array = []
var state := "list"      # list | commands | customize | element | confirm | build
var cmd := 0
var sel := 0             # weapon cursor
var hole_sel := 0
var attach_sel := 0
var on_attach := false
var elem_sel := 0
var build_sel := 0
var targets: Array = []
var confirm_kind := ""
var confirm_yes := false
var note := ""
var stats_label: Label
var list_label: Label
var help_label: Label

func setup(session: DcModels, walker_node: DcWalker = null) -> void:
	models = session
	walker = walker_node
	ch = walker.get_character() if walker != null else int(models.get_village_status().get("character", 0))
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.04, 0.12, 0.95)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	list_label = _label(Vector2(40, 30), 22)
	stats_label = _label(Vector2(520, 30), 20)
	help_label = _label(Vector2(40, 560), 22)
	_reload()
	_render()

func _label(pos: Vector2, size: int) -> Label:
	var l := Label.new()
	l.position = pos
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 4)
	add_child(l)
	return l

func _reload() -> void:
	weapons = models.wm_weapons(ch, walker)
	sel = clampi(sel, 0, maxi(weapons.size() - 1, 0))

func _cur() -> Dictionary:
	return weapons[sel] if sel < weapons.size() else {}

func _refresh_game() -> void:
	if walker != null:
		walker.refresh_weapon()

func _text(id: int) -> String:
	return String(models.menu_text(id)).replace("\n", " ")

func _result(code: int, ok_note: String = "") -> bool:
	_reload()
	if code == Result.OK:
		note = ok_note
		_refresh_game()
		return true
	note = _text(REFUSALS[code]) if REFUSALS.has(code) else ("You need a powder for that." if code == Result.NEEDS_POWDER else "That is not possible.")
	return false

func _render() -> void:
	var lines := PackedStringArray()
	var hero: String = models.get_hero_name(ch)
	lines.append("%s's weapons     [Q] [E] character" % hero)
	for i in weapons.size():
		var w: Dictionary = weapons[i]
		var tag := ("E " if w["equipped"] else "  ")
		lines.append(("▶ " if i == sel and state == "list" else "   ") + tag + "%s%s   Lv %d" % [w["name"], "" if int(w["level"]) == 0 else "+%d" % w["level"], w["level"]])
	if state == "commands":
		lines.append("")
		for i in COMMANDS.size():
			lines.append(("▶ " if i == cmd else "   ") + COMMANDS[i])
	list_label.text = "\n".join(lines)
	stats_label.text = _stats_text()
	var help := note
	if help == "":
		if state == "commands":
			help = _text(COMMAND_HELP[cmd])
		elif state == "list":
			help = "[Enter] commands   [Esc] close"
	help_label.text = help

func _stats_text() -> String:
	var w := _cur()
	if w.is_empty():
		return ""
	var t := PackedStringArray()
	t.append("%s   Lv %d" % [w["name"], w["level"]])
	t.append("WHp %d / %d      Abs %d / %d" % [int(w["durability"]), w["max_durability"], w["exp"], w["max_exp"]])
	t.append("Attack %d   Endurance %d   Speed %d   Magic %d" % [w["attack"], w["endurance"], w["speed"], w["magic"]])
	var el := ""
	var elv: Array = w["elements"]
	for i in 5:
		el += ("[%s %d] " if i == int(w["element"]) else "%s %d  ") % [ELEMENTS[i], elv[i]]
	t.append(el)
	var sl := ""
	var slv: Array = w["slayers"]
	for i in 10:
		if int(slv[i]) != 0:
			sl += "%s %d  " % [SLAYERS[i], slv[i]]
	t.append(sl)
	for h in w["holes"]:
		t.append("Hole %d (%s): %s%s" % [int(h["hole"]) + 1, "plate" if int(h["type"]) == 2 else "slot", h["name"] if int(h["id"]) >= 0 else "-", " x2" if h["double"] else ""])
	if state == "customize":
		t.append("")
		var items: Array = models.wm_attach_items(walker)
		t.append("Synthesis items:" + ("   <- list" if on_attach else ""))
		for i in items.size():
			t.append(("▶ " if on_attach and i == attach_sel else "  ") + String(items[i]["name"]))
		t.append("")
		t.append("Holes:" + ("" if on_attach else "   <- list") + "  (selected hole %d)" % (hole_sel + 1))
	elif state == "element":
		t.append("")
		for i in 5:
			t.append(("▶ " if i == elem_sel else "   ") + ELEMENTS[i])
	elif state == "build":
		t.append("")
		t.append("Build up to:")
		for i in targets.size():
			t.append(("▶ " if i == build_sel else "   ") + "%s%s" % [targets[i]["name"], "" if targets[i]["buildable"] else "   (items missing)"])
	elif state == "confirm":
		t.append("")
		t.append("%s?   %s" % [confirm_kind, "[Yes] No" if confirm_yes else "Yes [No]"])
	return "\n".join(t)

func _unhandled_key_input(event: InputEvent) -> void:
	if not visible or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	var k: int = event.keycode
	var up := k == KEY_UP or k == KEY_W
	var down := k == KEY_DOWN or k == KEY_S
	var left := k == KEY_LEFT or k == KEY_A
	var right := k == KEY_RIGHT or k == KEY_D
	var ok := k == KEY_ENTER or k == KEY_SPACE or k == KEY_KP_ENTER
	var back := k == KEY_ESCAPE or k == KEY_X
	note = ""
	if k == KEY_Q or k == KEY_E:
		if state == "list":
			ch = posmod(ch + (1 if k == KEY_E else -1), maxi(int(models.get_party_size()), 1))
			sel = 0
			_reload()
	else:
		match state:
			"list": _key_list(up, down, ok, back)
			"commands": _key_commands(up, down, ok, back)
			"customize": _key_customize(up, down, left, right, ok, back, k)
			"element": _key_element(up, down, ok, back)
			"confirm": _key_confirm(up, down, left, right, ok, back)
			"build": _key_build(up, down, ok, back)
	_render()
	get_viewport().set_input_as_handled()

func _key_list(up: bool, down: bool, ok: bool, back: bool) -> void:
	if up: sel = maxi(sel - 1, 0)
	elif down: sel = mini(sel + 1, weapons.size() - 1)
	elif ok and not weapons.is_empty(): state = "commands"; cmd = 0
	elif back:
		closed.emit()
		queue_free()

func _key_commands(up: bool, down: bool, ok: bool, back: bool) -> void:
	if up: cmd = posmod(cmd - 1, COMMANDS.size())
	elif down: cmd = posmod(cmd + 1, COMMANDS.size())
	elif back: state = "list"
	elif ok:
		var w := _cur()
		var slot: int = w["slot"]
		match cmd:
			0: _result(models.wm_equip(ch, slot, walker), "Equipped.")
			1:
				state = "customize"
				hole_sel = 0
				attach_sel = 0
				on_attach = true
			2: state = "element"; elem_sel = clampi(int(w["element"]), 0, 4)
			3: _result(models.wm_repair(ch, slot, walker), "Repaired.")
			4: confirm_kind = "Upgrade %s" % w["name"]; confirm_yes = false; state = "confirm"
			5: confirm_kind = "Seal 60% of the ability in a sphere"; confirm_yes = false; state = "confirm"
			6:
				targets = models.wm_build_targets(ch, slot, walker)
				build_sel = 0
				if targets.is_empty():
					note = "Nothing to build up to."
				else:
					state = "build"
			7: confirm_kind = "Discard %s" % w["name"]; confirm_yes = false; state = "confirm"

func _key_customize(up: bool, down: bool, left: bool, right: bool, ok: bool, back: bool, k: int) -> void:
	var w := _cur()
	var items: Array = models.wm_attach_items(walker)
	var holes: Array = w["holes"]
	if back:
		state = "commands"
		return
	if k == KEY_TAB:
		on_attach = not on_attach
	elif on_attach:
		if up: attach_sel = maxi(attach_sel - 1, 0)
		elif down: attach_sel = mini(attach_sel + 1, maxi(items.size() - 1, 0))
		elif ok and not items.is_empty() and not holes.is_empty():
			_result(models.wm_attach(ch, w["slot"], int(holes[hole_sel]["hole"]), int(items[attach_sel]["slot"]), walker))
			attach_sel = clampi(attach_sel, 0, maxi(models.wm_attach_items(walker).size() - 1, 0))
	else:
		if up: hole_sel = maxi(hole_sel - 1, 0)
		elif down: hole_sel = mini(hole_sel + 1, maxi(holes.size() - 1, 0))
	if left: on_attach = false
	if right: on_attach = true
	if k == KEY_DELETE and not holes.is_empty():
		_result(models.wm_detach(ch, w["slot"], int(holes[hole_sel]["hole"]), walker))

func _key_element(up: bool, down: bool, ok: bool, back: bool) -> void:
	if up: elem_sel = posmod(elem_sel - 1, 5)
	elif down: elem_sel = posmod(elem_sel + 1, 5)
	elif back: state = "commands"
	elif ok:
		_result(models.wm_element(ch, _cur()["slot"], elem_sel, walker))
		state = "commands"

func _key_confirm(up: bool, down: bool, left: bool, right: bool, ok: bool, back: bool) -> void:
	if up or down or left or right:
		confirm_yes = not confirm_yes
	elif back:
		state = "commands"
	elif ok:
		if not confirm_yes:
			state = "commands"
			return
		var w := _cur()
		var slot: int = w["slot"]
		if confirm_kind.begins_with("Upgrade"):
			if _result(models.wm_level_up(ch, slot, walker)):
				note = _text(400).replace("{fbfe}", String(w["name"]))
				note = "Level up!"
		elif confirm_kind.begins_with("Seal"):
			if _result(models.wm_status_break(ch, slot, walker)):
				note = "A SynthSphere was made."
		else:
			if _result(models.wm_discard(ch, slot, walker)):
				note = "Discarded."
		state = "commands"
		_reload()

func _key_build(up: bool, down: bool, ok: bool, back: bool) -> void:
	if up: build_sel = maxi(build_sel - 1, 0)
	elif down: build_sel = mini(build_sel + 1, targets.size() - 1)
	elif back: state = "commands"
	elif ok:
		var t: Dictionary = targets[build_sel]
		if not t["buildable"]:
			note = _text(172)
			return
		if _result(models.wm_build_up(ch, _cur()["slot"], int(t["id"]), walker)):
			note = "Built up to %s" % t["name"]
		state = "commands"
