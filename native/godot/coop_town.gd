class_name CoopTown
extends Node
# Co-op in a town or a house: the other players walking around as puppets (their character, their colour, a name tag), and the keys that change the
# character you play. Every player runs his own town; only where the players are is exchanged.

var run: TownRun
var net: CoopNet
var puppets := {}          # peer id -> {node, character, label, target_pos, target_yaw, map}
var _send_timer := 0.0
var cycle_enabled := true
var gifts := {}            # id -> {node, item, owner (peer), pos, age}   items lying on the ground for the others
var _gift_serial := 0

func setup(town_run: TownRun, coop: CoopNet) -> void:
	run = town_run
	net = coop
	net.presence_received.connect(_on_presence)
	net.peer_left.connect(_on_peer_left)
	net.roster_changed.connect(_on_roster_changed)
	net.event_received.connect(_on_event)
	net.session_closed.connect(clear_puppets)
	apply_hero()

# the hero of this town / house wears my colour and my character's model
func apply_hero() -> void:
	if run == null or run.walk == null:
		return
	var ch: int = run.models.get_character()
	if ch != 0 or net.active:
		var model: Node3D = run.models.make_player_model(ch, net.my_index)
		if model != null:
			run.walk.set_hero_model(model)
	run.walk.set_hero_color(net.my_index)
	run.walk.set_edit_locked(not net.is_host)   # (only the host builds the Georama everybody walks in)

# the town changed (another map, a house): the puppets belong to the old scene
# co-op gift: one of my items (a bag slot of the record) lies in front of me; the others (and I, after a moment) can pick it up
func drop_item(slot: int) -> bool:
	var item: int = run.models.take_bag_item(slot)
	if item < 0:
		return false
	var pose: Dictionary = run.walk.get_hero_pose()
	var pos: Vector3 = pose["pos"] + Vector3(sin(float(pose["yaw"])), 0, cos(float(pose["yaw"]))) * 22.0
	_gift_serial += 1
	var id := net.my_peer() * 1000 + _gift_serial
	_spawn_gift(id, item, pos, net.my_peer())
	net.send_event("town_drop", {"id": id, "item": item, "pos": pos, "map": run.map_name})
	return true

func _spawn_gift(id: int, item: int, pos: Vector3, owner: int) -> void:
	var s := Sprite3D.new()
	s.texture = run.models.get_item_icon(item)
	s.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	s.pixel_size = 0.5
	s.no_depth_test = true
	s.position = pos + Vector3(0, 9, 0)
	run.walk.add_child(s)
	gifts[id] = {"node": s, "item": item, "owner": owner, "pos": pos, "age": 0.0}

func _remove_gift(id: int) -> void:
	if gifts.has(id):
		if is_instance_valid(gifts[id]["node"]):
			gifts[id]["node"].queue_free()
		gifts.erase(id)

func _on_event(from_peer: int, kind: String, data: Dictionary) -> void:
	if run == null or run.walk == null:
		return
	match kind:
		"town_drop":
			if String(data["map"]) == run.map_name:
				_spawn_gift(int(data["id"]), int(data["item"]), data["pos"], from_peer)
		"town_take":
			_remove_gift(int(data["id"]))

func clear_puppets() -> void:
	for id in gifts.keys():   # (leaving the map: the gifts I left lying there come back to my bag)
		if int(gifts[id]["owner"]) == net.my_peer():
			run.models.give_item(int(gifts[id]["item"]))
	gifts.clear()
	for p in puppets.values():
		if is_instance_valid(p["node"]):
			p["node"].queue_free()
	puppets.clear()

func cycle_character(direction: int) -> void:
	if not cycle_enabled or run == null or run.walk == null:
		return
	var party: Array = run.models.get_party()
	var cur: int = run.models.get_character()
	var ch := cur
	for _i in range(6):
		ch = posmod(ch + direction, 6)
		break   # in co-op every character may be played (set_character lets the character join)
	if ch == cur:
		return
	run.models.set_character(ch)
	net.set_my_character(ch)
	apply_hero()
	print("COOP character ", ch, " of ", party.size())

func _process(delta: float) -> void:
	if run == null or run.walk == null or not net.active:
		return
	_send_timer += delta
	if _send_timer >= 0.05:
		_send_timer = 0.0
		var pose: Dictionary = run.walk.get_hero_pose()
		if not pose.is_empty():
			pose["map"] = run.map_name
			pose["character"] = run.models.get_character()
			net.send_presence(pose)
	var hero_pos := Vector3.INF
	var hp: Dictionary = run.walk.get_hero_pose()
	if not hp.is_empty():
		hero_pos = hp["pos"]
	for gid in gifts.keys():   # pick up a gift: the first to touch it takes it and tells the others
		var g: Dictionary = gifts[gid]
		g["age"] += delta
		if hero_pos != Vector3.INF and hero_pos.distance_to(g["pos"]) < 12.0 and (int(g["owner"]) != net.my_peer() or g["age"] > 2.5):
			if run.models.give_item(int(g["item"])):
				net.send_event("town_take", {"id": gid})
				_remove_gift(gid)
	for id in puppets.keys():
		var p: Dictionary = puppets[id]
		var n: Node3D = p["node"]
		if not is_instance_valid(n):
			continue
		n.position = n.position.lerp(p["target_pos"], clampf(delta * 14.0, 0.0, 1.0))
		n.rotation.y = lerp_angle(n.rotation.y, p["target_yaw"], clampf(delta * 14.0, 0.0, 1.0))

func _on_presence(peer: int, state: Dictionary) -> void:
	if run == null or run.walk == null:
		return
	var same_map: bool = String(state.get("map", "")) == run.map_name and bool(state.get("visible", true))
	if not same_map:
		_remove_puppet(peer)
		return
	var mode_key := int(state.get("mode", 0)) * 2 + (1 if bool(state.get("fish", {}).get("out", false)) else 0)
	if mode_key != int(puppets.get(peer, {}).get("last_mode", -1)):   # (a trace line for the co-op tests)
		print("PUPPET mode ", state.get("mode", 0), " motion ", state.get("motion", 0), " fish ", state.get("fish", {}))
		if puppets.has(peer):
			puppets[peer]["last_mode"] = mode_key
	var ch := int(state.get("character", 0))
	var idx := int(state.get("index", net.index_of(peer)))
	var fishing := int(state.get("mode", 0)) == 5   # (the town's Toan with his rod; the motions of the pose are that pack's)
	var p: Dictionary = puppets.get(peer, {})
	if p.is_empty() or int(p["character"]) != ch or int(p["index"]) != idx or bool(p.get("fishing", false)) != fishing:
		_remove_puppet(peer)
		var node: Node3D = null
		if fishing:
			node = run.models.make_town_model("chara/c01d_turi.chr", "c01d_turi.cfg", idx)
		if node == null:
			fishing = false
			node = run.models.make_player_model(ch, idx)
		if node == null:
			return
		node.position = state["pos"]
		run.walk.add_child(node)
		var label := Label3D.new()
		label.text = String(net.roster.get(peer, {}).get("name", "P%d" % (idx + 1)))
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.no_depth_test = true
		label.fixed_size = false
		label.pixel_size = 0.35
		label.font_size = 40
		label.outline_size = 12
		label.outline_modulate = Color(0, 0, 0, 1)
		label.modulate = _color_of(idx)
		label.position = Vector3(0, 58, 0)
		node.add_child(label)
		p = {"node": node, "character": ch, "index": idx, "fishing": fishing, "label": label, "target_pos": state["pos"], "target_yaw": float(state.get("yaw", 0.0))}
		puppets[peer] = p
	p["target_pos"] = state["pos"]
	p["target_yaw"] = float(state.get("yaw", 0.0))
	var node2: DcCharacter = p["node"] as DcCharacter
	var motion := int(state.get("motion", 0))
	var pmode := int(state.get("mode", 0))
	if pmode == 1 or pmode == 2 or pmode == 3 or pmode == 4 or (pmode == 5 and not bool(p.get("fishing", false))):   # in an event, talking, editing, at a shop counter: the town hero's own motions are not this model's, he stands
		motion = 0
	_update_line(p, state if pmode == 5 and bool(p.get("fishing", false)) else {})
	if node2.get_motion() != motion:
		node2.play(motion, true)
	node2.set_playing(true)
	if motion == 1:
		node2.set_speed_scale(1.0)
	else:
		node2.set_speed_scale(0.05 if motion == 0 else 1.0)

static func _color_of(idx: int) -> Color:
	return [Color(1, 0.35, 0.35), Color(0.4, 0.6, 1), Color(0.45, 0.9, 0.45), Color(1, 0.9, 0.35)][clampi(idx, 0, 3)]

# a fishing puppet's float and line (the presence carries the float, the rod tip and whether the line is out)
func _update_line(p: Dictionary, state: Dictionary) -> void:
	var fish: Dictionary = state.get("fish", {})
	if fish.is_empty() or not bool(fish.get("out", false)):
		if p.has("float") and is_instance_valid(p["float"]):
			p["float"].visible = false
			p["line"].visible = false
		return
	if not p.has("float") or not is_instance_valid(p["float"]):
		var fl := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = 1.4
		sm.height = 2.8
		fl.mesh = sm
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color(1.0, 0.25, 0.2)
		mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		fl.material_override = mat
		run.walk.add_child(fl)
		var ln := MeshInstance3D.new()
		ln.mesh = ImmediateMesh.new()
		var lm := StandardMaterial3D.new()
		lm.albedo_color = Color(0.95, 0.95, 0.85)
		lm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		ln.material_override = lm
		run.walk.add_child(ln)
		p["float"] = fl
		p["line"] = ln
	var flt: Vector3 = fish["float"]
	var tip: Vector3 = fish["tip"]
	var slack := float(fish.get("slack", 1.0))
	p["float"].visible = true
	p["float"].position = flt
	p["line"].visible = true
	var im: ImmediateMesh = p["line"].mesh
	im.clear_surfaces()
	im.surface_begin(Mesh.PRIMITIVE_LINE_STRIP)
	for i in range(25):
		var u := float(i) / 24.0
		var q := tip.lerp(flt, u)
		q.y -= 6.0 * sin(u * PI) * slack
		im.surface_add_vertex(q)
	im.surface_end()

func _remove_puppet(peer: int) -> void:
	if puppets.has(peer):
		if is_instance_valid(puppets[peer]["node"]):
			puppets[peer]["node"].queue_free()
		for k in ["float", "line"]:
			if puppets[peer].has(k) and is_instance_valid(puppets[peer][k]):
				puppets[peer][k].queue_free()
		puppets.erase(peer)

func _on_peer_left(peer: int) -> void:
	_remove_puppet(peer)

func _on_roster_changed() -> void:
	if run != null and run.walk != null:
		run.walk.set_hero_color(net.my_index)
