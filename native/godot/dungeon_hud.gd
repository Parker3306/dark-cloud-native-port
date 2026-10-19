class_name DungeonHud
extends Control
# The dungeon HUD drawn like the game's topStatusInfo / MainDraw (notes/menus.md section A, tools/dc/hud_mock.py): life, weapon durability, water, the three
# quick items, the floor plaque, the ailment tag and the attack gauge. Coordinates are on the game's 640 x 448 screen.

const MAX_FLOORS := [15, 17, 18, 18, 15, 25, 100]
const PAL := {
	"life": [Color8(0x40, 0xc1, 0x82), Color8(0x00, 0x81, 0xff)],
	"weapon": [Color8(0xff, 0x80, 0x00), Color8(0xff, 0x20, 0x00)],
	"30": [Color8(0xff, 0x00, 0xc6), Color8(0x3f, 0x00, 0x31)],
	"15": [Color8(0xff, 0x00, 0x00), Color8(0x3f, 0x00, 0x00)],
}

var models: DcModels
var itempack: Texture2D
var stayframe: Texture2D
var wepready: Texture2D
var status := {}
var floor_no := 0
var dungeon := 0
var back_dungeon := false
var title_back := false   # the floor title shows BACK instead of LIMITED ZONE
var rogo_y := -0x60       # the HUD slides in 2 pixels a frame (rogoY3)
var alarm_counter := 0.0
var alarm := 0x80
var k := Vector2.ONE
var banner_tex: Texture2D
var banner_dungeon := -1
var banner_state := 3      # floor title: 0 fade in, 1 hold, 2 fade out, 3 gone
var banner_alpha := 0
var banner_hold := 0
var walker: DcWalker
var mpd_path := ""
var minimap_tex: Texture2D
var other_players: Callable   # co-op: [{pos: Vector2 (x, z), color, name, life, max_life}] of the other players on this floor
var hits: Array = []          # floating damage numbers: {pos, digits, phase[], alpha, rate, type, noeffect}

func setup(session: DcModels) -> void:
	models = session
	itempack = models.get_pack_texture("commenu/a_usa/itempack.img", "itempack")
	stayframe = models.get_pack_texture("commenu/a_usa/stayframe.img", "stayframe")
	wepready = models.get_pack_texture("dun/pack/teximg.pac", "wepready")
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE

func set_map(session_walker: DcWalker, mpd: String) -> void:
	walker = session_walker
	mpd_path = mpd
	minimap_tex = null
	hits.clear()

func add_hits(list: Array) -> void:
	for h in list:
		var v: int = h["value"]
		var e := {"pos": h["pos"], "type": int(h["type"]), "alpha": 0.0, "rate": 3.0, "noeffect": int(h["type"]) < 0, "np": 0.0, "digits": [], "phase": []}
		if int(h["type"]) >= 0:
			var s := str(maxi(v, 0))
			for i in range(s.length() - 1, -1, -1):   # units digit first
				e["digits"].append(int(s[i]))
				e["phase"].append(-PI)
		hits.append(e)
	if hits.size() > 32:
		hits = hits.slice(hits.size() - 32)

func show_title(back: bool) -> void:
	title_back = back
	banner_state = 0
	banner_alpha = 0

func reset_slide() -> void:
	title_back = false
	rogo_y = -0x60
	banner_state = 0
	banner_alpha = 0

func set_state(st: Dictionary, floor_index: int, dungeon_no: int) -> void:
	status = st
	floor_no = floor_index
	dungeon = dungeon_no

func tick() -> void:
	match banner_state:
		0:
			banner_alpha += 4
			if banner_alpha >= 0x80:
				banner_alpha = 0x80
				banner_hold = 0x78
				banner_state = 1
		1:
			banner_hold -= 1
			if banner_hold <= 0:
				banner_state = 2
		2:
			banner_alpha -= 4
			if banner_alpha <= 0:
				banner_alpha = 0
				banner_state = 3
	if rogo_y < 0x20:
		rogo_y = mini(rogo_y + 2, 0x20)
	alarm_counter += 0.10472
	if alarm_counter > PI:
		alarm_counter -= PI
	alarm = 0xc0 - int(sin(alarm_counter) * 128.0)
	var alive: Array = []
	for e in hits:
		e["alpha"] = clampf(e["alpha"] + e["rate"], 0.0, 128.0)
		if e["noeffect"]:
			e["np"] += 0.15708
			if e["np"] >= PI:
				e["rate"] = -3.0
		else:
			var ph: Array = e["phase"]
			for i in ph.size():
				if ph[i] < PI:
					ph[i] = minf(ph[i] + PI / (5.0 * i + 20.0), PI)
			if ph[ph.size() - 1] >= PI and e["rate"] > 0.0:
				e["rate"] = -3.6
		if e["alpha"] > 0.0 or e["rate"] > 0.0:
			alive.append(e)
	hits = alive

func _process(_delta: float) -> void:
	queue_redraw()

func _spr(tex: Texture2D, dst: Rect2, src: Rect2, alpha: int = 0x80) -> void:
	if tex == null:
		return
	draw_texture_rect_region(tex, Rect2(dst.position * k, dst.size * k), src, Color(1, 1, 1, clampf(alpha / 128.0, 0.0, 1.0)))

func _r(x: float, y: float, w: float, h: float) -> Rect2:
	return Rect2(x, y, w, h)

func _gradient(rect: Rect2, c: Array) -> void:
	if rect.size.x <= 0:
		return
	var r := Rect2(rect.position * k, rect.size * k)
	var pts := PackedVector2Array([r.position, Vector2(r.end.x, r.position.y), r.end, Vector2(r.position.x, r.end.y)])
	draw_polygon(pts, PackedColorArray([c[0], c[1], c[1], c[0]]))

func _fill(x: float, y: float, w: float, h: float, c: Color) -> void:
	if w > 0:
		draw_rect(Rect2(Vector2(x, y) * k, Vector2(w, h) * k), c)

func _pal(mx: float, cur: float, weapon: bool = false) -> Array:
	if 0.2 * mx < cur:
		return PAL["weapon" if weapon else "life"]
	if 0.3 * mx < cur or cur <= 0.15 * mx:
		return PAL["15"]
	return PAL["30"]

# ValuePrint: at least two digits (zero padded), three from 100
func _digits(x: float, y: float, v: int, row: int = 0) -> int:
	var s := ("%03d" % v) if v >= 100 else ("%02d" % v)
	for i in s.length():
		var d := int(s[i])
		_spr(stayframe, _r(x + 12 * i, y, 12, 12), _r(d * 12, 0xb0 + row * 12, 12, 12))
	return s.length()

func _bar(y: float, seg: int, alpha: int) -> float:
	_spr(itempack, _r(0x30, y, 4, 16), _r(0x68, 0x70, 4, 16), alpha)
	for i in range(seg - 2):
		_spr(itempack, _r(i * 4 + 0x34, y, 4, 16), _r(0x6c, 0x70, 4, 16), alpha)
	var xr := (seg - 1) * 4.0
	_spr(itempack, _r(xr + 0x30, y, 4, 16), _r(0x70, 0x70, 4, 16), alpha)
	return xr

func _draw() -> void:
	if status.is_empty() or itempack == null:
		return
	var vp := get_viewport_rect().size
	k = Vector2(vp.x / 640.0, vp.y / 448.0)
	_banner()
	_minimap()
	_overlay()
	_other_players_panel()
	if walker != null:
		var bf: float = walker.get_boss_fade()
		if bf > 0.0:
			draw_rect(Rect2(Vector2.ZERO, vp), Color(0, 0, 0, bf))
	var y: float = rogo_y - 10
	var al := alarm
	# life
	var cur: int = status["life"]
	var mx: int = status["max_life"]
	var seg := maxi(2, (maxi(2, int(0.74 * mx)) >> 2) + 1)
	var alpha := 0x80 if 0.2 * mx < cur else al
	var xr := _bar(y, seg, alpha)
	if cur > 0:
		_gradient(_r(0x34, y + 5, int(0.74 * cur) - 2, 6), _pal(mx, cur))
	var x := xr + 0x38
	x += _digits(x, y + 7, cur) * 10
	_spr(stayframe, _r(x, y + 7, 12, 12), _r(0x78, 0xb0, 12, 12))
	_digits(x + 10, y + 7, mx)
	_spr(itempack, _r(0x20, y, 16, 16), _r(0x40, 0xa0, 16, 16), alpha)
	# weapon durability
	var mxd: float = status["weapon_max_durability"]
	var curd: float = status["weapon_durability"]
	var f := 1.49495 * mxd
	seg = maxi(2, (maxi(2, int(f)) >> 2) + 2)
	alpha = 0x80 if 0.2 * mxd < curd else al
	xr = _bar(y + 0x11, seg, alpha)
	if curd > 0:
		_gradient(_r(0x34, y + 0x16, int(1.49495 * curd) - 2, 6), _pal(mxd, curd, true))
	var fy := float(((int(y) + 0x1f) >> 1) * 2)
	var wlen := int(f) - 1
	_fill(52, fy, wlen, 2, Color8(0x40, 0x40, 0x40))
	var exp_now: int = status.get("weapon_exp", 0)
	var exp_max: int = status.get("weapon_max_exp", 0)
	if exp_now > 0 and exp_max > 0:
		_fill(52, fy, int(exp_now * (int(f) / float(exp_max))) - 1, 2, Color8(0, 0xa2, 0xff))
	x = xr + 0x38
	var c := int(curd) + (1 if curd - int(curd) > 0 else 0)
	x += _digits(x, y + 0x18, c) * 10
	_spr(stayframe, _r(x, y + 0x18, 12, 12), _r(0x78, 0xb0, 12, 12))
	_digits(x + 10, y + 0x18, int(mxd))
	_spr(itempack, _r(0x20, y + 0x10, 16, 16), _r(0x40, 0xb0, 16, 16), alpha)
	# water
	var wmax: float = status["max_water"]
	var wcur: float = status["water"]
	var wal := al if wcur <= 0.15 * wmax else 0x80
	_spr(itempack, _r(0x20, y + 0x22, 18, 20), _r(0x64, 0x84, 18, 20), wal)
	for i in int(wmax) / 10:
		_spr(itempack, _r(i * 18 + 0x32, y + 0x22, 18, 20), _r(0x64, 0x98, 18, 20), wal)
	if int(wmax) % 10 != 0:
		_spr(itempack, _r((int(wmax) / 10) * 18 + 0x32, y + 0x22, 18, 20), _r(0x64, 0xac, 24, 20), wal)
	for i in int(wcur) / 10:
		_spr(itempack, _r(i * 18 + 0x33, y + 0x22, 16, 20), _r(0, 0x48, 16, 20), 0x50)
	if int(wcur) % 10 != 0:
		var part := int((int(wcur) % 10) / 2.5)
		_spr(itempack, _r((int(wcur) / 10) * 18 + 0x33, y + 0x22, 16, 20), _r((3 - part) * 16, 0x48, 16, 20), 0x50)
	# quick items (the icons come straight from the item sheet instead of being copied into the atlas)
	var quick: Array = status.get("quick", [])
	for i in 3:
		var xx := i * 0x28
		_spr(itempack, _r(xx + 0x126, y + 2, 36, 36), _r(0x24, 0x5c, 36, 36))
		if i < quick.size():
			var iid: int = quick[i]["id"]
			var cnt: int = quick[i]["count"]
			if iid > 0:
				var icon: Texture2D = models.get_item_icon(iid)
				if icon != null:
					_spr(icon, _r(xx + 0x128, y + 4, 31, 31), _r(0, 0, 31, 31))
				if cnt > 1:
					if cnt / 10 > 0:
						_spr(stayframe, _r(xx + 0x12f, y + 0x17, 12, 12), _r((cnt / 10) * 12, 0xd4, 12, 12))
					_spr(stayframe, _r(xx + 0x13b, y + 0x17, 12, 12), _r((cnt % 10) * 12, 0xd4, 12, 12))
	var sel: int = status.get("quick_sel", 1)
	var px := sel * 0x28
	for e in [[0xfc, 0, 0x4a, 0x48], [0x113, 0, 0x5a, 0x48], [0xfc, 0x16, 0x4a, 0x58], [0x113, 0x16, 0x5a, 0x58]]:
		_spr(stayframe, _r(px + e[0], y + e[1], 16, 16), _r(e[2], e[3], 16, 16))
	# floor plaque, crystal and map
	_spr(itempack, _r(0x1fc, y, 102, 41), _r(0x9a, 1, 102, 41))
	if back_dungeon:
		_spr(itempack, _r(0x23c, y, 38, 41), _r(0xda, 0x2b, 38, 41))
	if status.get("have_crystal", false):
		_spr(itempack, _r(0x1fe, y + 5, 32, 32), _r(0, 0xa0, 32, 32))
	if status.get("have_map", false):
		_spr(itempack, _r(0x21e, y + 5, 32, 32), _r(0x20, 0xa0, 32, 32))
	var n := floor_no + 1
	if dungeon >= 0 and dungeon < MAX_FLOORS.size() and n == MAX_FLOORS[dungeon]:
		_spr(stayframe, _r(0x23c, y + 0x12, 38, 17), _r(0x78, 0x9e, 38, 18))
	else:
		var base := 0x23f if n < 10 else 0x245
		if n > 9:
			_spr(stayframe, _r(base - 4, y + 0x12, 14, 17), _r((n / 10) * 12, 0x9e, 12, 18))
		_spr(stayframe, _r(base + 9, y + 0x12, 14, 17), _r((n % 10) * 12, 0x9e, 12, 18))
	# attack gauge, bottom left (fades in with the slide)
	var ga := (rogo_y + 0x60) & 0xff
	var g: float = status.get("gauge", 0.0)
	if g >= 100.0:
		_spr(wepready, _r(0x28, 0x18c, 128, 28), _r(0, 28 if status.get("overheat", false) else 0, 128, 28), ga)
	else:
		_spr(wepready, _r(0x28, 0x18c, 128, 28), _r(0, 0x54, 128, 28), ga)
		_spr(wepready, _r(0x42, 0x18c, int(g), 28), _r(0x1a, 0x38, int(g), 28), ga)
	_spr(itempack, _r(0x18, 0x18c, 42, 29), _r(0x78, 0x37, 42, 29), ga)
	_spr(itempack, _r(0x45, 0x188, 43, 16), _r(0xa2, 0x37, 43, 16), ga)
	var wid: int = status.get("weapon_id", 0)
	if wid > 0:
		var wicon: Texture2D = models.get_item_icon(wid)
		if wicon != null:
			_spr(wicon, _r(0x1d, 0x184, 32, 32), _r(0, 0, 32, 32), ga)
	# ailment tag
	var err: int = status.get("status_err", 0)
	for e in [[0x40, 0, 2], [0x20, 1, 2], [0x10, 1, 1], [8, 0, 1], [4, 1, 0]]:
		if err & e[0]:
			_spr(itempack, _r(0x1ae, y, 62, 35), _r(0x84 + e[1] * 62, 0x54 + e[2] * 36, 62, 36))

# the floor title: "LIMITED ZONE" row, the dungeon's name plate and the floor number (MainDraw / FloorTitleOn)
func _banner() -> void:
	if banner_state == 3 or dungeon < 0 or dungeon > 6:
		return
	if banner_dungeon != dungeon:
		banner_dungeon = dungeon
		banner_tex = models.get_pack_texture("dun/img/us/dname%02d.img" % dungeon, "floor%02d" % dungeon)
	if banner_tex == null:
		return
	var n := floor_no + 1
	var a := banner_alpha
	if title_back:
		_spr(banner_tex, _r(0x110, 0xe6, 0x60, 0x38), _r(0x110, 0x78, 0x60, 0x38), a)
	else:
		_spr(banner_tex, _r(0xd4, 0xe6, 0xf0, 0x30), _r(0, 0x78, 0xf0, 0x30), a)
	var digits := str(n)
	var xs: Array = {1: [0x15a], 2: [0x136, 0x15a], 3: [0x112, 0x136, 0x15a]}[digits.length()]
	for i in digits.length():
		_spr(banner_tex, _r(xs[i], 0xaa, 0x26, 0x32), _r(int(digits[i]) * 0x26, 0x32, 0x26, 0x32), a)
	var s1 := -0x24 if n >= 10 else 0
	match dungeon:
		0: _spr(banner_tex, _r(s1 + 0x122, 0xaa, 0x2d, 0x32), _r(0, 0, 0x2d, 0x32), a)
		1: _spr(banner_tex, _r(0xa0, 0xaa, 0xda, 0x32), _r(0, 0, 0xda, 0x32), a)
		2: _spr(banner_tex, _r(0xc8, 0xaa, 0x91, 0x32), _r(0, 0, 0x91, 0x32), a)
		3: _spr(banner_tex, _r(s1 + 0xdc, 0xaa, 0x8e, 0x32), _r(0, 0, 0x8e, 0x32), a)
		4:
			_spr(banner_tex, _r(s1 + 0xc3, 0xaa, 0x4b, 0x32), _r(0, 0, 0x4b, 0x32), a)
			_spr(banner_tex, _r(0x13b, 0xaa, 0xc0, 0x32), _r(0x60, 0, 0xc0, 0x32), a)
		5: _spr(banner_tex, _r(0x108, 0xaa, 0xe1, 0x32), _r(0, 0, 0xe1, 0x32), a)
		6: _spr(banner_tex, _r(s1 + 0xdc, 0xaa, 0x8e, 0x32), _r(0, 0, 0x8e, 0x32), a)

func _minimap() -> void:
	if walker == null:
		return
	var mode: int = status.get("map_mode", 0)
	if mode < 0 or mode > 2:
		return
	var m := walker.get_minimap()
	if m.is_empty() or not m.has("tiles"):
		return
	if minimap_tex == null and String(m["img"]) != "":
		minimap_tex = models.get_pack_texture(mpd_path, String(m["img"]).get_basename())
	if minimap_tex == null:
		return
	var explored: float = [0x60, 0x80, 0xa2][mode]
	var unexplored: float = [0x20, 0x40, 0x60][mode]
	var alpha_levels := [0x40, 0x60, 0xa2]
	var map_known: bool = status.get("have_map", false)
	var crystal: bool = status.get("have_crystal", false)
	var tiles: PackedInt32Array = m["tiles"]
	var vis: PackedInt32Array = m["visited"]
	for i in 400:
		if tiles[i] < 0:
			continue
		var seen := vis[i] == 1
		if not seen and not map_known:
			continue
		var c: float = explored if seen else unexplored
		var t := tiles[i]
		var col := i % 20
		var row := i / 20
		var tint := Color(c / 128.0, c / 128.0, c / 128.0, 1.0)
		_spr_tint(minimap_tex, _r(0x17c + 16 * col, 0x40 + 16 * row, 16, 16), _r((t & 7) * 16, (t >> 3) * 16, 16, 16), tint, alpha_levels[mode])
	# markers
	var shown := func(p: Vector2) -> bool:
		var cx := int(floor((p.x + 80.0) / 160.0))
		var cz := int(floor((p.y + 80.0) / 160.0))
		if cx < 0 or cz < 0 or cx >= 20 or cz >= 20:
			return false
		return crystal or vis[cz * 20 + cx] == 1
	for p in m.get("items", []):
		if shown.call(p):
			_spr(itempack, _r(p.x * 0.1 + 0x180, p.y * 0.1 + 0x44, 8, 8), _r(0x48, 0x68, 8, 8))
	for p in m.get("monsters", []):
		if crystal and shown.call(p):
			_spr(itempack, _r(p.x * 0.1 + 0x180, p.y * 0.1 + 0x44, 8, 8), _r(0x50, 0x60, 8, 8))
	if other_players.is_valid():
		for o in other_players.call():
			var op: Vector2 = o["pos"]
			var c2: Color = o["color"]
			draw_rect(Rect2(Vector2(op.x * 0.1 + 0x17c + 8 - 3, op.y * 0.1 + 0x40 + 8 - 3) * k, Vector2(7, 7) * k), c2)
			draw_rect(Rect2(Vector2(op.x * 0.1 + 0x17c + 8 - 3, op.y * 0.1 + 0x40 + 8 - 3) * k, Vector2(7, 7) * k), Color.BLACK, false, 1.0)
	if m.has("player"):
		var pp: Vector2 = m["player"]
		var dst := Rect2((Vector2(pp.x * 0.1 + 380 + 8, pp.y * 0.1 + 64 + 8)) * k, Vector2(12, 12) * k)
		draw_set_transform(dst.position, -float(m["angle"]) + PI, Vector2.ONE)
		draw_texture_rect_region(itempack, Rect2(-dst.size * 0.5, dst.size), _r(0x58, 0x60, 12, 12))
		draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)

func _spr_tint(tex: Texture2D, dst: Rect2, src: Rect2, tint: Color, alpha: int) -> void:
	tint.a = clampf(alpha / 128.0, 0.0, 1.0)
	draw_texture_rect_region(tex, Rect2(dst.position * k, dst.size * k), src, tint)

# floating damage numbers and the life bar of the monster in the cursor
func _screen(cam: Camera3D, p: Vector3) -> Vector2:
	if cam.is_position_behind(p):
		return Vector2(-1000, -1000)
	return cam.unproject_position(p) / k

func _overlay() -> void:
	if walker == null:
		return
	var cam := walker.get_camera()
	if cam == null:
		return
	var row_y := {0: 0x9e, 1: 0x8c, 2: 0x7c}
	var hide_enemy_dmg := models.get_option(8) == 1
	var hide_pc_dmg := models.get_option(9) == 1
	for e in hits:
		if (int(e["type"]) <= 0 and hide_enemy_dmg and int(e["type"]) != 2) or (int(e["type"]) == 2 and hide_pc_dmg):
			continue
		var sp := _screen(cam, e["pos"])
		var a := int(e["alpha"])
		if e["noeffect"]:
			_spr(itempack, _r(sp.x - 36, sp.y - 0x30 - sin(e["np"]) * 24.0, 72, 24), _r(0, 0x80, 72, 24), a)
			continue
		var digits: Array = e["digits"]
		var ph: Array = e["phase"]
		for i in digits.size():
			var dy: float = sp.y - int(sin(ph[i]) * 48.0) - 0x30
			_spr(stayframe, _r(sp.x - 12 * i, dy, 12, 18), _r(digits[i] * 12, row_y.get(e["type"], 0x9e), 12, 18), a)
	var tg: Dictionary = status.get("target", {})
	if not tg.is_empty() and models.get_option(10) == 0:
		var sp := _screen(cam, tg["pos"])
		var cx := int(sp.x)
		var cy := int(sp.y) & ~1
		if cx - 0x20 >= 0 and cx - 0x20 <= 0x240 and cy >= 0 and cy <= 0x1e0:
			_fill(cx - 0x21, cy - 1, 66, 6, Color8(0x80, 0x9b, 0xff))
			_fill(cx - 0x20, cy, 64, 4, Color8(0, 0, 0x20))
			var mxhp := maxf(float(tg["hp_max"]), 1.0)
			_gradient(_r(cx - 0x20, cy, int(64.0 / mxhp * float(tg["hp"])), 4), [Color8(0xff, 0, 0), Color8(0xff, 0xb4, 0)])
		if tg.get("locked", false):   # the corner marks of the lock-on cursor
			var col := Color(1, 1, 1, 0.9)
			for sx in [-1, 1]:
				for sy in [-1, 1]:
					var o := Vector2(sp.x + sx * 22.0, sp.y + sy * 22.0)
					draw_line(o * k, (o - Vector2(sx * 8.0, 0)) * k, col, 2.0)
					draw_line(o * k, (o - Vector2(0, sy * 8.0)) * k, col, 2.0)

# co-op: the other players' names and life under the HUD
func _other_players_panel() -> void:
	if not other_players.is_valid():
		return
	var font := ThemeDB.fallback_font
	var y := 150.0
	for o in other_players.call():
		var c2: Color = o["color"]
		draw_rect(Rect2(Vector2(16, y - 12) * k, Vector2(10, 10) * k), c2)
		draw_string(font, Vector2(32, y) * k, String(o["name"]), HORIZONTAL_ALIGNMENT_LEFT, -1, int(13 * k.y), Color.WHITE)
		var life: int = o["life"]
		var mx: int = maxi(o["max_life"], 1)
		draw_rect(Rect2(Vector2(32, y + 3) * k, Vector2(70, 5) * k), Color(0, 0, 0, 0.6))
		draw_rect(Rect2(Vector2(32, y + 3) * k, Vector2(70.0 * float(life) / float(mx), 5) * k), Color(0.3, 0.9, 0.4) if life * 3 > mx else Color(0.95, 0.3, 0.2))
		y += 22.0
