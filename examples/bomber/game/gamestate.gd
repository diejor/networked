class_name BomberGamestate
extends NetwService
## Manages the bomber game state as a session service.

const DEFAULT_PORT = 10567
const MAX_PEERS = 12

var player_name: String = "The Warrior"
var players := { }

signal player_list_changed()
signal connection_failed()
signal connection_succeeded()
signal game_ended()
signal game_error(what: String)

@onready var ctx: NetwMultiplayer = Netw.of(self)

var world: MultiplayerScene:
	get:
		if not ctx:
			return null
		var sm := ctx.scene_manager
		if not sm:
			return null
		return sm.active_scenes.get(&"World") as MultiplayerScene

var lobby: MultiplayerScene:
	get:
		if not ctx:
			return null
		var sm := ctx.scene_manager
		if not sm:
			return null
		return sm.active_scenes.get(&"Lobby") as MultiplayerScene


func _ready() -> void:
	setup_connections()


func _on_participant_joined(participant: NetwParticipant) -> void:
	players[participant.peer_id] = participant.username
	player_list_changed.emit()
	var lobby_scene := lobby
	if ctx.is_server() and is_instance_valid(lobby_scene):
		lobby_scene.admit(participant)


func _on_peer_disconnected(id: int) -> void:
	# A client leaving never ends the match. The framework despawns its player
	# entity and the survivors play on. Only the host leaving ends the session,
	# which reaches clients as server_disconnected (see _on_server_disconnected).
	unregister_player(id)


func _on_connected_ok() -> void:
	connection_succeeded.emit()


func _on_server_disconnected() -> void:
	game_error.emit("Server disconnected")
	end_game()


func _on_connected_fail() -> void:
	connection_failed.emit()


func join_game(ip: String, _player_name: String) -> void:
	player_name = _player_name
	var jp := JoinPayload.new()
	jp.username = _player_name

	var target := NetwConnectTarget.new()
	target.scheme = ctx.tree.scheme
	target.address = ip
	ctx.join(target, jp)


func host_game(_player_name: String) -> void:
	player_name = _player_name
	var jp := JoinPayload.new()
	jp.username = _player_name

	ctx.host(jp)


@rpc("any_peer", "call_local")
func register_player(new_player_name: String) -> void:
	var id := multiplayer.get_remote_sender_id()
	players[id] = new_player_name
	player_list_changed.emit()


func unregister_player(id: int) -> void:
	players.erase(id)
	player_list_changed.emit()


func get_player_list() -> Array:
	return players.values()


## Starts the match by moving lobby participants into [code]World[/code].
func begin_game() -> void:
	assert(multiplayer.is_server())
	var sm := ctx.scene_manager
	var world_scene := sm.activate_scene(&"World")
	var lobby_scene := lobby
	assert(is_instance_valid(world_scene))
	assert(is_instance_valid(lobby_scene))
	world_scene.move_participants(lobby_scene.participants)


func end_game() -> void:
	# A client reaching here through server_disconnected has a dead peer, so
	# multiplayer.is_server() would query get_unique_id() on it and error. Only
	# an active peer can be the server, so gate the server-side teardown on it.
	var mp := multiplayer.multiplayer_peer
	var peer_active := mp != null \
			and mp.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED
	if peer_active and multiplayer.is_server() and is_instance_valid(world):
		var sm := ctx.scene_manager
		var lobby_scene := lobby
		if is_instance_valid(lobby_scene):
			lobby_scene.move_participants(world.participants)
		sm.retire_scene(&"World")

	game_ended.emit()
	players.clear()


func setup_connections() -> void:
	ctx.participant_joined.connect(_on_participant_joined)
	ctx.peer_disconnected.connect(_on_peer_disconnected)
	ctx.connected_to_server.connect(_on_connected_ok)
	ctx.server_disconnected.connect(_on_server_disconnected)


func get_player_color(p_name: String) -> Color:
	return Color.from_hsv(wrapf(p_name.hash() * 0.001, 0.0, 1.0), 0.6, 1.0)
