class_name CoopNet
extends Node
# Co-op networking on Godot's ENet multiplayer. One host, up to three clients; every player has a number (0 = host) that also picks the colour of the clothes.
# This node must have the same path on every peer: it is always /root/CoopNet (see CoopNet.create()).
#  - presence: every peer sends where it is (map, position, motion ...) about 20 times a second; the others show it as a puppet
#  - events: reliable messages (enter dungeon, drop an item ...) delivered to every peer through the `event_received` signal
#  - the host also relays: a message sent to the host with `to_host` is handled only there

signal roster_changed
signal presence_received(peer: int, state: Dictionary)
signal data_received(from_peer: int, kind: String, payload: Variant)   # fast, unreliable messages (send_data)
signal event_received(from_peer: int, kind: String, data: Dictionary)
signal connected          # a client: the host accepted us (my_index is known)
signal connection_failed
signal peer_left(peer: int)
signal upnp_done(external_address: String)   # try_upnp finished ("" = no router answered)
signal session_closed      # the connection is gone (the host left, or stop() was called)

const MAX_PLAYERS := 4
const DEFAULT_PORT := 8211

var active := false
var is_host := false
var my_index := 0
var my_name := "Player"
var roster := {}      # peer id -> {index, name, character}
var remote := {}      # peer id -> the last presence state
var _since_send := 0.0
var external_address := ""    # the address others outside this network can use (found with UPnP), "" when unknown
var _upnp_thread: Thread
var _upnp_port := 0           # the port this session asked the router to forward (0: none)

static func create(parent: Node) -> CoopNet:
	var existing := parent.get_tree().root.get_node_or_null("CoopNet")
	if existing != null:
		return existing
	var n := CoopNet.new()
	n.name = "CoopNet"
	parent.get_tree().root.add_child.call_deferred(n)   # (the root may still be setting up its children)
	return n

# the node joins the tree on the next frame: wait for it before host_game / join_game
func wait_ready() -> void:
	while not is_inside_tree():
		await Engine.get_main_loop().process_frame

static func find(tree: SceneTree) -> CoopNet:
	return tree.root.get_node_or_null("CoopNet") as CoopNet

func host_game(port := DEFAULT_PORT, player_name := "Player") -> int:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, MAX_PLAYERS - 1)
	if err != OK:
		return err
	multiplayer.multiplayer_peer = peer
	_connect_signals()
	active = true
	is_host = true
	my_index = 0
	my_name = player_name
	roster = {1: {"index": 0, "name": player_name, "character": 0}}
	roster_changed.emit()
	return OK

func join_game(address: String, port := DEFAULT_PORT, player_name := "Player") -> int:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_client(address, port)
	if err != OK:
		return err
	multiplayer.multiplayer_peer = peer
	_connect_signals()
	active = true
	is_host = false
	my_name = player_name
	return OK

# Asks the router (UPnP) to forward the port so friends on the internet can join. This changes the router's settings, so it is only done when the player asks for it
# (a button); the mapping runs out after 6 hours and is removed again when the session ends. Runs in a thread (discovery takes seconds). Result: upnp_done.
func try_upnp(port := DEFAULT_PORT) -> void:
	if _upnp_thread != null:
		return
	_upnp_thread = Thread.new()
	_upnp_thread.start(_upnp_work.bind(port))

func _upnp_work(port: int) -> void:
	var address := ""
	var u := UPNP.new()
	if u.discover(2000) == UPNP.UPNP_RESULT_SUCCESS and u.get_gateway() != null and u.get_gateway().is_valid_gateway():
		if u.add_port_mapping(port, port, "Dark Cloud co-op", "UDP", 6 * 3600) == UPNP.UPNP_RESULT_SUCCESS:
			address = u.query_external_address()
			_upnp_port = port
	_upnp_finished.call_deferred(address)

func _upnp_finished(address: String) -> void:
	if _upnp_thread != null:
		_upnp_thread.wait_to_finish()
		_upnp_thread = null
	external_address = address
	upnp_done.emit(address)

# gives the router its port back (the mapping would run out by itself after 6 hours)
func close_upnp() -> void:
	if _upnp_port == 0:
		return
	var u := UPNP.new()
	if u.discover(1500) == UPNP.UPNP_RESULT_SUCCESS and u.get_gateway() != null and u.get_gateway().is_valid_gateway():
		u.delete_port_mapping(_upnp_port, "UDP")
	_upnp_port = 0
	external_address = ""

func _exit_tree() -> void:
	if _upnp_thread != null:
		_upnp_thread.wait_to_finish()
	close_upnp()

func stop() -> void:
	close_upnp()
	if multiplayer.multiplayer_peer != null:
		multiplayer.multiplayer_peer.close()
		multiplayer.multiplayer_peer = null
	active = false
	is_host = false
	roster.clear()
	remote.clear()
	roster_changed.emit()
	session_closed.emit()

var _signals_connected := false
func _connect_signals() -> void:
	if _signals_connected:
		return
	_signals_connected = true
	multiplayer.peer_connected.connect(_on_peer_connected)
	multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	multiplayer.connected_to_server.connect(_on_connected_to_server)
	multiplayer.connection_failed.connect(func(): active = false; connection_failed.emit())
	multiplayer.server_disconnected.connect(func(): stop())

func my_peer() -> int:
	return multiplayer.get_unique_id() if active else 1

func player_count() -> int:
	return roster.size() if active else 1

# the player number of a peer (-1 when unknown)
func index_of(peer: int) -> int:
	return int(roster[peer]["index"]) if roster.has(peer) else -1

func _on_peer_connected(id: int) -> void:
	if not is_host:
		return
	var used := {}
	for p in roster:
		used[int(roster[p]["index"])] = true
	var idx := 0
	while used.has(idx):
		idx += 1
	roster[id] = {"index": idx, "name": "Player %d" % (idx + 1), "character": 0}
	_rpc_roster.rpc(roster)
	roster_changed.emit()

func _on_peer_disconnected(id: int) -> void:
	remote.erase(id)
	if is_host:
		roster.erase(id)
		_rpc_roster.rpc(roster)
	peer_left.emit(id)
	roster_changed.emit()

func _on_connected_to_server() -> void:
	_rpc_hello.rpc_id(1, my_name)

@rpc("any_peer", "call_remote", "reliable")
func _rpc_hello(player_name: String) -> void:
	if not is_host:
		return
	var id := multiplayer.get_remote_sender_id()
	if roster.has(id):
		roster[id]["name"] = player_name
		roster[id]["ready"] = true   # (it can hear us: what was sent before may have got lost)
		_rpc_roster.rpc(roster)
		roster_changed.emit()

@rpc("authority", "call_remote", "reliable")
func _rpc_roster(r: Dictionary) -> void:
	var first := roster.is_empty()
	roster = r
	var me := multiplayer.get_unique_id()
	if roster.has(me):
		my_index = int(roster[me]["index"])
		if first:
			connected.emit()
	roster_changed.emit()

# ------------------------------------------------------------------------------------------------------------------------------ presence and events
# `state` is any small dictionary; it replaces what the others know about this peer. Called at most every 50 ms.
func send_presence(state: Dictionary) -> void:
	if not active:
		return
	state["index"] = my_index
	_rpc_presence.rpc(state)

@rpc("any_peer", "call_remote", "unreliable_ordered")
func _rpc_presence(state: Dictionary) -> void:
	var id := multiplayer.get_remote_sender_id()
	remote[id] = state
	presence_received.emit(id, state)

# fast messages that may be lost or arrive late (the newest wins): snapshots, attack spheres ...
func send_data(kind: String, payload: Variant) -> void:
	if active:
		_rpc_data.rpc(kind, payload)

func send_data_to(peer: int, kind: String, payload: Variant) -> void:
	if active:
		_rpc_data.rpc_id(peer, kind, payload)

@rpc("any_peer", "call_remote", "unreliable_ordered")
func _rpc_data(kind: String, payload: Variant) -> void:
	data_received.emit(multiplayer.get_remote_sender_id(), kind, payload)

# to every other peer (reliable)
func send_event(kind: String, data: Dictionary = {}) -> void:
	if not active:
		return
	_rpc_event.rpc(kind, data)

# to one peer
func send_event_to(peer: int, kind: String, data: Dictionary = {}) -> void:
	if not active:
		return
	_rpc_event.rpc_id(peer, kind, data)

# to the host only
func send_event_to_host(kind: String, data: Dictionary = {}) -> void:
	if not active:
		return
	if is_host:
		event_received.emit(1, kind, data)
	else:
		_rpc_event.rpc_id(1, kind, data)

@rpc("any_peer", "call_remote", "reliable")
func _rpc_event(kind: String, data: Dictionary) -> void:
	event_received.emit(multiplayer.get_remote_sender_id(), kind, data)

# every peer tells the others which character it plays (the roster of the host is only the list of who is there)
func set_my_character(ch: int) -> void:
	if roster.has(my_peer()):
		roster[my_peer()]["character"] = ch
