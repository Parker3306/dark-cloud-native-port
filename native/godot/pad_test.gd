extends Node
# Harness: prints the DualShock as the game gets it (PS2 pad word bits, sticks) whenever it changes. --seconds N (default 20)

const NAMES := {0x1: "L2", 0x2: "R2", 0x4: "L1", 0x8: "R1", 0x10: "Triangle", 0x20: "Circle", 0x40: "Cross", 0x80: "Square", 0x100: "Select", 0x200: "L3", 0x400: "R3", 0x800: "Start",
	0x1000: "Up", 0x2000: "Right", 0x4000: "Down", 0x8000: "Left"}
var models := DcModels.new()
var last := ""
var t := 0.0

func _process(delta: float) -> void:
	t += delta
	var s: Dictionary = models.pad_state()
	var names: PackedStringArray = []
	for b in NAMES:
		if int(s["bits"]) & b:
			names.append(NAMES[b])
	var line := "%s | L(%.2f,%.2f) R(%.2f,%.2f)" % [" ".join(names), snappedf(s["lx"], 0.05), snappedf(s["ly"], 0.05), snappedf(s["rx"], 0.05), snappedf(s["ry"], 0.05)]
	if line != last:
		print("PAD ", line)
		last = line
	var secs := 20.0
	var a := OS.get_cmdline_user_args()
	if a.find("--seconds") >= 0:
		secs = float(a[a.find("--seconds") + 1])
	if t > secs:
		get_tree().quit()
