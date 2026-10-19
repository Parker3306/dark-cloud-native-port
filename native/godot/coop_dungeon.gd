class_name CoopDungeon
extends Node
# Co-op inside a dungeon: the same floor for everybody (the host's seed), the other players as puppets, the floor changes that move everybody together, and
# the shared monsters (see notes/coop_native.md):
#  host   simulates the monsters for every player; it takes the attack spheres of the clients into its hit pool, sends the monsters' attack spheres, a snapshot
#         of the monsters and the drops, floating numbers, and the changes it made to a client's record (weapon experience, gold, items picked up)
#  client simulates only its own player; the monsters on its screen are the host's snapshots; it sends its attack spheres and its player record.

var run: DungeonRun
var net: CoopNet
var puppets := {}          # peer id -> {node, character, index, target_pos, target_yaw}
var remote_ids := {}       # host: peer id -> id of the remote player in the host's monster world
var statuses := {}         # host: peer id -> the last player record that peer sent
var _send_timer := 0.0
var _status_timer := 0.0
var _exit_sent := false
var _tick := 0
var floor_data := {}      # the host's seed and record of this floor (a late guest builds the same floor from it)
var _greeted := {}        # host: peers that were in the roster when the floor began or got the floor since
var _ev_last := PackedInt32Array()   # the floor's event state as this peer last knew it (sent or received)

func setup(dungeon_run: DungeonRun, coop: CoopNet) -> void:
	run = dungeon_run
	net = coop
	net.presence_received.connect(_on_presence)
	net.peer_left.connect(_on_peer_left)
	net.event_received.connect(_on_event)
	net.data_received.connect(_on_data)
	net.roster_changed.connect(_on_roster_changed)
	for peer in net.roster:
		_greeted[peer] = true
	for ch in range(6):   # in co-op every character is on the team
		var party: Array = run.models.get_party()
		if not bool(party[ch]["joined"]):
			run.models.join_party(ch)

# the other players on this floor, for the minimap and the life panel
func other_players() -> Array:
	var out := []
	for peer in puppets:
		var st: Dictionary = net.remote.get(peer, {})
		var p: Dictionary = puppets[peer]
		if not is_instance_valid(p["node"]):
			continue
		var pos: Vector3 = p["node"].position
		out.append({"pos": Vector2(pos.x, pos.z), "color": CoopTown._color_of(int(p["index"])), "name": String(net.roster.get(peer, {}).get("name", "P%d" % (int(p["index"]) + 1))), "life": int(st.get("life", 0)), "max_life": int(st.get("max_life", 1))})
	return out

func map_key() -> String:
	return "d%df%d%s" % [run.dungeon, run.floor_no, "b" if run.walker != null and run.walker.is_back_floor() else ""]   # (the back floor is a floor of its own)

# the floor swapped with its back floor (by a script of this player or because another player took everybody over): the event state starts afresh,
# and a swap that began here goes to the others (the host relays it)
var _ura_skip := 0
func on_ura_swap(to_back: bool) -> void:
	_ev_last = run.walker.coop_event_state()
	if _ura_skip > 0:
		_ura_skip -= 1
		return
	if net.is_host:
		net.send_event("ura_swap", {"back": to_back})
	else:
		net.send_event_to_host("ura_swap", {"back": to_back})

# host: somebody joined while the dungeon runs: he builds the same floor and gets the state of its chests and doors
func _on_roster_changed() -> void:
	if not net.is_host or run == null or run.walker == null or floor_data.is_empty():
		return
	for peer in net.roster:
		if peer == 1 or _greeted.has(peer) or not bool(net.roster[peer].get("ready", false)):
			continue
		_greeted[peer] = true
		net.send_event_to(peer, "enter_dungeon", {"dungeon": run.dungeon, "floor": run.floor_no, "seed": floor_data["seed"], "status": floor_data["status"], "back": run.walker.is_back_floor()})

# a new floor was built: the old puppets belong to the old walker
func on_floor_started() -> void:
	for p in puppets.values():
		if is_instance_valid(p["node"]):
			p["node"].queue_free()
	puppets.clear()
	remote_ids.clear()
	_exit_sent = false
	_ev_last = PackedInt32Array()
	var w: DcWalker = run.walker
	w.set_player_color(net.my_index)
	w.coop_set_role(1 if net.is_host else 2)
	if net.is_host:
		for peer in statuses:
			remote_ids[peer] = w.coop_add_remote(statuses[peer])
	else:
		net.send_event_to_host("player_status", {"status": w.coop_status_bytes()})

# co-op gift: one of this player's items (a bag slot) lies on the floor in front of him; the others (and he) can pick it up
func drop_item(slot: int) -> bool:
	var item: int = run.walker.drop_bag_item(slot)
	if item < 0:
		return false
	var pose: Dictionary = run.walker.get_hero_pose()
	var pos: Vector3 = pose["pos"]
	var yaw: float = pose["yaw"]
	pos += Vector3(sin(yaw), 0, cos(yaw)) * 22.0
	if net.is_host:
		run.walker.coop_spawn_drop(pos, item)
	else:
		net.send_event_to_host("drop_item", {"item": item, "pos": pos})
	return true

# this player's visit ends: the host tells everybody (they follow), a client tells the host (it stops looking for him)
func on_finish(reason: String) -> void:
	if net.is_host:
		net.send_event("dungeon_end", {"reason": reason, "boss": run.boss_down, "jump": run.models.get_clear_jump()})
	else:
		net.send_event_to_host("dungeon_left", {})

# the walker asked for the stairs: the host decides and everybody goes down together
# the next (or previous) character of this player's party takes over; the player's colour stays
func cycle_character(direction: int) -> void:
	var cur: int = run.walker.get_character()
	for i in range(1, 7):
		var ch: int = posmod(cur + direction * i, 6)
		if run.models.switch_character(run.walker, ch):
			run.models.set_character(ch)
			if run.sound != null:
				run.sound.load_voice(ch)
			print("COOP character ", ch)
			return

func request_floor_exit() -> void:
	if _exit_sent:
		return
	_exit_sent = true
	net.send_event_to_host("floor_exit")

func _on_peer_left(peer: int) -> void:
	_remove_puppet(peer)
	if run != null and run.walker != null:
		run.walker.coop_clear_fx(peer)
	if net.is_host and remote_ids.has(peer) and run != null and run.walker != null:
		run.walker.coop_remove_remote(int(remote_ids[peer]))
	remote_ids.erase(peer)
	statuses.erase(peer)

func _on_event(from_peer: int, kind: String, data: Dictionary) -> void:
	match kind:
		"floor_exit":   # (host only)
			if net.is_host:
				var seed := run.models.draw_coop_seed()
				var status := run.models.get_status_bytes()
				net.send_event("next_floor", {"seed": seed, "status": status})
				run.coop_next_floor(seed, status)
		"next_floor":
			if not net.is_host:
				run.coop_next_floor(int(data["seed"]), data["status"])
		"player_status":   # (host) a client's record: the damage code runs with it when that client's sphere hits a monster
			if net.is_host:
				if not statuses.has(from_peer) and run.walker != null:   # (the first time: what happened on this floor so far, for a guest who came late)
					var cur := run.walker.coop_event_state()
					var all := PackedInt32Array()
					for i in cur.size():
						all.push_back(i)
						all.push_back(cur[i])
					net.send_event_to(from_peer, "ev_state", {"pairs": all, "map": map_key()})
				statuses[from_peer] = data["status"]
				if run.walker != null:
					if remote_ids.has(from_peer):
						run.walker.coop_set_remote_status(int(remote_ids[from_peer]), data["status"])
					else:
						remote_ids[from_peer] = run.walker.coop_add_remote(data["status"])
		"ura_swap":   # a player went through the entrance of the back floor (or back): everybody follows
			if run.walker != null and run.walker.has_back_floor() and run.walker.is_back_floor() != bool(data["back"]):
				_ura_skip += 1
				run.walker.coop_swap_floor()
			if net.is_host and from_peer != 1:
				net.send_event("ura_swap", data)
		"ev_state":   # a player opened a chest, took a statue, opened a door ...: the same happens here (the item went to him)
			if run.walker != null and String(data.get("map", map_key())) == map_key():
				var pairs: PackedInt32Array = data["pairs"]
				run.walker.coop_apply_event_state(pairs)
				for k in range(0, pairs.size() - 1, 2):   # (what came from the other player is not sent back)
					if k / 2 < pairs.size() / 2 and int(pairs[k]) < _ev_last.size():
						_ev_last[int(pairs[k])] = int(pairs[k + 1])
		"drop_item":   # (host) a player put an item on the floor for the others
			if net.is_host and run.walker != null:
				run.walker.coop_spawn_drop(data["pos"], int(data["item"]))
		"dungeon_end":   # (client) the host's visit is over: so is ours
			if not net.is_host:
				run.coop_end(String(data["reason"]), data["boss"], data["jump"])
		"dungeon_left":   # (host) a client went back to town
			_on_peer_left(from_peer)
		"mimic":   # (host) a client opened a chest that was a monster
			if net.is_host and run.walker != null:
				run.walker.coop_mimic(int(data["slot"]))
		"patch":   # (client) the host changed my record
			if not net.is_host and run.walker != null:
				for p in data["patches"]:
					run.walker.coop_apply_patch(int(p["offset"]), p["bytes"])
		"notice":   # (client) a message of my weapon (it broke, it is almost worn out ...)
			if not net.is_host and run.walker != null:
				run.walker.coop_show_notice(data["notice"])

func _on_data(from_peer: int, kind: String, payload: Variant) -> void:
	if run == null or run.walker == null:
		return
	match kind:
		"spheres":   # (host) the attack spheres of a client
			if net.is_host and remote_ids.has(from_peer):
				run.walker.coop_import_spheres(from_peer, int(remote_ids[from_peer]), payload)
		"monster_spheres":   # (client) what the monsters swing
			if not net.is_host:
				run.walker.coop_import_spheres(1, 0, payload)
		"snapshot":   # (client) the monsters and the drops
			if not net.is_host:
				run.walker.coop_apply_snapshot(payload)
		"sounds":   # (client) what the monsters make
			if not net.is_host:
				run.walker.coop_play_sounds(payload)
		"fx":   # the weapon effect of another player (on this floor)
			if String(net.remote.get(from_peer, {}).get("map", "")) == map_key():
				run.walker.coop_apply_fx(from_peer, payload)
		"hits":   # (client) floating numbers
			if not net.is_host:
				run.walker.coop_show_hits(payload)

func _physics_process(_delta: float) -> void:
	if run == null or run.walker == null or not net.active:
		return
	_tick += 1
	var w: DcWalker = run.walker
	_sync_events(w)
	if _tick % 3 == 0:   # my weapon effect for the others to draw (20 Hz, unreliable)
		var fx := w.coop_export_fx()
		if not fx.is_empty():
			net.send_data("fx", fx)
	if net.is_host:
		for peer in remote_ids:   # where the clients are (their presence), for the monsters' eyes and the pick-ups
			var st: Dictionary = net.remote.get(peer, {})
			if st.has("pos") and String(st.get("map", "")) == map_key():
				w.coop_update_remote(int(remote_ids[peer]), st)
		net.send_data("monster_spheres", w.coop_export_spheres())
		if _tick % 2 == 0:
			net.send_data("snapshot", w.coop_snapshot())
		var snds := w.coop_take_sounds()
		if not snds.is_empty():
			net.send_data("sounds", snds)
		var hits := w.coop_take_hit_values()
		if not hits.is_empty():
			net.send_data("hits", hits)
		var patches := w.coop_take_patches()
		if not patches.is_empty():
			for peer in remote_ids:
				var mine := patches.filter(func(p): return int(p["remote"]) == int(remote_ids[peer]))
				if not mine.is_empty():
					net.send_event_to(peer, "patch", {"patches": mine})
		for n in w.coop_take_remote_notices():
			for peer in remote_ids:
				if int(remote_ids[peer]) == int(n["remote"]):
					net.send_event_to(peer, "notice", {"notice": n})
	else:
		net.send_data_to(1, "spheres", w.coop_export_spheres())
		_status_timer += _delta
		if _status_timer >= 1.0:
			_status_timer = 0.0
			net.send_event_to_host("player_status", {"status": w.coop_status_bytes()})

# what this player did to the floor's events since the last look goes to the others
func _sync_events(w: DcWalker) -> void:
	var cur := w.coop_event_state()
	if cur.is_empty():
		return
	if _ev_last.size() != cur.size():
		_ev_last = cur
		return
	var pairs := PackedInt32Array()
	for i in cur.size():
		if cur[i] != _ev_last[i]:
			pairs.push_back(i)
			pairs.push_back(cur[i])
	if not pairs.is_empty():
		_ev_last = cur
		net.send_event("ev_state", {"pairs": pairs, "map": map_key()})

func _process(delta: float) -> void:
	if run == null or run.walker == null or not net.active:
		return
	_send_timer += delta
	if _send_timer >= 0.05:
		_send_timer = 0.0
		var pose: Dictionary = run.walker.get_hero_pose()
		if not pose.is_empty():
			pose["map"] = map_key()
			pose["character"] = run.walker.get_character()
			net.send_presence(pose)
	for id in puppets.keys():
		var p: Dictionary = puppets[id]
		var n: DcCharacter = p["node"]
		if not is_instance_valid(n):
			continue
		n.position = n.position.lerp(p["target_pos"], clampf(delta * 18.0, 0.0, 1.0))
		n.rotation.y = lerp_angle(n.rotation.y, p["target_yaw"], clampf(delta * 18.0, 0.0, 1.0))

func _on_presence(peer: int, state: Dictionary) -> void:
	if run == null or run.walker == null:
		return
	if String(state.get("map", "")) != map_key() or not bool(state.get("visible", true)):
		_remove_puppet(peer)
		return
	var ch := int(state.get("character", 0))
	var idx := int(state.get("index", net.index_of(peer)))
	var p: Dictionary = puppets.get(peer, {})
	var weapon := int(state.get("weapon", -1))
	if p.is_empty() or int(p["character"]) != ch or int(p["index"]) != idx or int(p.get("weapon", -1)) != weapon:
		_remove_puppet(peer)
		var node: Node3D = run.models.make_player_model(ch, idx, weapon)
		if node == null:
			return
		node.position = state["pos"]
		run.walker.add_child(node)
		var label := Label3D.new()
		label.text = String(net.roster.get(peer, {}).get("name", "P%d" % (idx + 1)))
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.no_depth_test = true
		label.pixel_size = 0.35
		label.font_size = 40
		label.outline_size = 12
		label.modulate = CoopTown._color_of(idx)
		label.position = Vector3(0, 58, 0)
		node.add_child(label)
		p = {"node": node, "character": ch, "index": idx, "weapon": weapon, "target_pos": state["pos"], "target_yaw": float(state.get("yaw", 0.0))}
		puppets[peer] = p
	p["target_pos"] = state["pos"]
	p["target_yaw"] = float(state.get("yaw", 0.0))
	var n2: DcCharacter = p["node"] as DcCharacter
	var motion := int(state.get("motion", 0))
	if n2.get_motion() != motion:
		n2.play(motion, true)
	n2.set_playing(false)
	n2.set_time(float(state.get("time", 0.0)))

func _remove_puppet(peer: int) -> void:
	if puppets.has(peer):
		if is_instance_valid(puppets[peer]["node"]):
			puppets[peer]["node"].queue_free()
		puppets.erase(peer)
