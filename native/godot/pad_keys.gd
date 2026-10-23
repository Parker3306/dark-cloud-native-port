extends Node
# The DualShock in the menus: most screens of the port read keyboard events, so the pad's buttons are turned into the keys those screens know.
# (The game itself reads the pad directly, see gdext/src/dc_pad.h: Cross OK, Circle lock-on / cancel, Square use, Triangle, L1 / R1 / L2 / R2, D-pad, sticks, Start, Select.)
#   D-pad -> arrow keys, Cross -> Enter (OK), Circle -> Escape (cancel), L1 / R1 -> Q / E (pages, turning).
# The bridge only acts while a menu control has the focus, so the world keeps the buttons.
# The events carry no physical key code, so the game's own polling of the keyboard (is_physical_key_pressed: the arrows would walk) does not see them. Godot's built-in
# joypad actions for the UI are removed so that one press is one step, not two.

const MAP := {
	JOY_BUTTON_DPAD_UP: KEY_UP, JOY_BUTTON_DPAD_DOWN: KEY_DOWN, JOY_BUTTON_DPAD_LEFT: KEY_LEFT, JOY_BUTTON_DPAD_RIGHT: KEY_RIGHT,
	JOY_BUTTON_A: KEY_ENTER, JOY_BUTTON_B: KEY_ESCAPE, JOY_BUTTON_LEFT_SHOULDER: KEY_Q, JOY_BUTTON_RIGHT_SHOULDER: KEY_E,
}

func _ready() -> void:
	for action in ["ui_up", "ui_down", "ui_left", "ui_right", "ui_accept", "ui_cancel", "ui_focus_next", "ui_focus_prev", "ui_page_up", "ui_page_down", "ui_select"]:
		if not InputMap.has_action(action):
			continue
		for ev in InputMap.action_get_events(action):
			if ev is InputEventJoypadButton or ev is InputEventJoypadMotion:
				InputMap.action_erase_event(action, ev)

func _input(event: InputEvent) -> void:
	# only while a menu control has the focus: in the world the game reads these buttons itself (Circle is lock-on there, not Escape)
	if not DisplayServer.window_is_focused() or get_viewport().gui_get_focus_owner() == null:
		return
	if event is InputEventJoypadButton and MAP.has(event.button_index):
		var k := InputEventKey.new()
		k.keycode = MAP[event.button_index]
		k.pressed = event.pressed
		Input.parse_input_event(k)
