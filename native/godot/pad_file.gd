extends RefCounted
# portkit replay, Godot side (copy this file into the game project). A recording is a text file, one line per game tick: `lx ly rx ry buttons menu` (integers: the raw stick bytes 0..255 with 128
# in the middle, the PS2 pad word, and an extra word for menu bits; `#` lines are comments). Recording is done by the game's own input code (the environment variable named in portkit.toml
# [replays] env_var holds the file to write); this script only reads a recording for the replay: ticks = preload("res://pad_file.gd").load_ticks(path)
# and hands the result to whatever feeds scripted input to the game (an array of [lx, ly, rx, ry, buttons, menu] per tick).

static func load_ticks(path: String) -> Array:
	var ticks: Array = []
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_error("pad file not found: " + path)
		return ticks
	while not f.eof_reached():
		var line := f.get_line().strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue
		var p := line.split(" ", false)
		if p.size() < 5:
			continue
		ticks.append([int(p[0]), int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]) if p.size() > 5 else 0])
	return ticks
