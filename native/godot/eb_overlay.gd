class_name EbOverlay
extends Control
# The EB duel's 2D layer (ebattle.cpp EBDraw): the lane with the window frame, the button markers that scroll in from the right, the hit popups, the intro CAUTION and the
# result banner. Screen space is the game's 640 x 448. Fed with DcWalker.get_eb_ui() every frame.

var ebat: Texture2D      # the face buttons and the stars
var ebat2: Texture2D     # EXCELLENT / Clear / Miss, OK!, COOL!, CAUTION
var ui: Dictionary = {}
var k := Vector2.ONE

# the cell of each button (pad mask) in the atlas `ebat`
const CELLS := {0x20: Vector2(0, 0), 0x10: Vector2(0x20, 0), 0x80: Vector2(0x40, 0), 0x40: Vector2(0x60, 0),
	0x1000: Vector2(0, 0x20), 0x4000: Vector2(0x20, 0x20), 0x2000: Vector2(0x40, 0x20), 0x8000: Vector2(0x60, 0x20),
	0x3000: Vector2(0, 0x40), 0x9000: Vector2(0x20, 0x40)}

func setup(models: DcModels) -> void:
	ebat = models.get_pack_texture("img/ebat.pak", "ebat")
	ebat2 = models.get_pack_texture("img/ebat.pak", "ebat2")
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_preset(Control.PRESET_FULL_RECT)

func update_from(state: Dictionary) -> void:
	ui = state
	visible = not state.is_empty()
	if visible:
		queue_redraw()

func _r(x: float, y: float, w: float, h: float) -> Rect2:
	return Rect2(Vector2(x, y) * k, Vector2(w, h) * k)

func _draw() -> void:
	if ui.is_empty():
		return
	var vp := get_viewport_rect().size
	k = Vector2(vp.x / 640.0, vp.y / 448.0)
	var shift := 0.0
	var intro: int = ui.get("intro", -1)
	if intro >= 0 and not ui.get("active", false):
		shift = 640.0 - float(intro) * 6.4
	elif intro >= 0:
		shift = maxf(640.0 - float(intro) * 6.4, 0.0)
	draw_rect(_r(shift, 320, 640, 32), Color(0, 40.0 / 255.0, 160.0 / 255.0, 0.5))
	draw_rect(_r(168 + shift, 320, 64, 32), Color(1, 1, 1, 0.25))
	draw_rect(_r(192 + shift, 320, 16, 32), Color(1, 1, 1, 0.25))
	if intro >= 0 and (intro / 4) % 2 == 1 and ebat2 != null:   # CAUTION blinks at the centre
		draw_texture_rect_region(ebat2, _r(320 - 28, 224 - 28, 56, 56), Rect2(256, 0, 56, 56))
	if ui.get("active", false) and ebat != null:
		var s: float = ui["speed"]
		var count: int = ui["count"]
		var hit: int = ui["key_count"]
		var keys: Array = ui["keys"]
		for i in keys.size():
			var key: Dictionary = keys[i]
			if bool(key["done"]) and i < hit + 64 and not (i == int(ui["popup_key"]) and int(ui["popup_timer"]) > 0):
				continue
			var x := 200.0 - float(count - int(key["tick"])) * s + shift
			if x < -32.0 or x > 640.0:
				continue
			var scale := 1.0
			var cell: Vector2 = CELLS.get(int(key["mask"]), Vector2(-1, -1))
			if bool(key["hint"]):
				cell = Vector2(0, 0x60)   # the yellow "?"
			if i == int(ui["popup_key"]) and int(ui["popup_timer"]) > 0:   # a hit marker grows to double size and back
				var t: int = 30 - int(ui["popup_timer"])
				scale = 1.0 + (0.2 * t if t < 5 else (1.0 - 0.2 * (t - 25) if t > 25 else 1.0))
				scale = clampf(scale, 1.0, 2.0)
			if cell.x < 0:
				continue
			var size := 32.0 * scale
			draw_texture_rect_region(ebat, _r(x + 16.0 - size * 0.5, 320 + 16.0 - size * 0.5, size, size), Rect2(cell.x, cell.y, 32, 32))
		if int(ui["popup_timer"]) > 0 and ebat2 != null and (int(ui["popup_timer"]) / 3) % 2 == 1:
			var good: bool = ui["popup_good"]
			var src := Rect2(0, 0xe0, 40, 16) if good else Rect2(0, 0xd0, 26, 16)
			draw_texture_rect_region(ebat2, _r(200 - src.size.x * 0.5, 318 - src.size.y, src.size.x, src.size.y), src)
	var result: int = ui.get("result", 0)
	if int(ui.get("finish", 0)) > 0 and ebat2 != null:   # the banner of the result
		var banner := Rect2(0, 0, 256, 70)
		if result == 1:
			banner = Rect2(0, 0x92, 184, 64)
		elif result < 0:
			banner = Rect2(0, 0x50, 146, 60)
		if result >= 0 or (int(ui["finish"]) >> 2) % 2 == 1:
			draw_texture_rect_region(ebat2, _r(320 - banner.size.x * 0.5, 224 - banner.size.y * 0.5, banner.size.x, banner.size.y), banner)
