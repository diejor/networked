class_name BomberGamestate
extends NetwService

const WORLD_SCENE := "res://examples/bomber/game/world.tscn"
const LOBBY_SCENE := "res://examples/bomber/game/lobby_level.tscn"

var player_name: String = "The Warrior"
var players := { }

signal player_list_changed()
signal game_ended()
signal game_error(what: String)

@onready var session: NetwSessionHandle = Netw.session(self)

var world: NetwSceneHandle:
	get:
		return Netw.scene(self, &"World")

var lobby: NetwSceneHandle:
	get:
		return Netw.scene(self, &"Lobby")


func _init() -> void:
	Netw.configure_spawn(spawn_lobby)


func _ready() -> void:
	setup_connections()


func spawn_lobby() -> Node:
	var packed: PackedScene = load(LOBBY_SCENE)
	return packed.instantiate()


func open_lobby() -> NetwSceneHandle:
	var level: Node = Netw.spawn(spawn_lobby)
	if level == null:
		return null
	get_parent().add_child(level)
	return Netw.scene(level)


func active_scene() -> NetwSceneHandle:
	var running := world
	if running != null:
		return running
	var waiting := lobby
	return waiting if waiting != null else open_lobby()


func on_participant_joined(participant: NetwParticipant) -> void:
	players[participant.peer_id] = participant.username
	player_list_changed.emit()
	if not multiplayer.is_server():
		return
	var target := active_scene()
	if target != null:
		target.admit(participant)


func on_peer_disconnected(id: int) -> void:
	unregister_player(id)


func on_server_disconnected() -> void:
	game_error.emit("Server disconnected")
	end_game()


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


func begin_game() -> NetwPromise:
	assert(multiplayer.is_server())
	return Netw.change_scene_to_file(self, WORLD_SCENE)


func end_game() -> void:
	var mp := multiplayer.multiplayer_peer
	var peer_active := mp != null \
			and mp.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED
	if peer_active and multiplayer.is_server() and world != null:
		Netw.change_scene_to_file(self, LOBBY_SCENE)

	game_ended.emit()
	players.clear()


func setup_connections() -> void:
	session.participant_joined.connect(on_participant_joined)
	multiplayer.peer_disconnected.connect(on_peer_disconnected)
	session.disconnected.connect(on_server_disconnected)
	session.scene_live.connect(on_scene_live)


func on_scene_live(arrived: NetwSceneHandle) -> void:
	if not multiplayer.is_server():
		return
	for participant: NetwParticipant in session.participants:
		if participant.current_scene == null:
			arrived.admit(participant)


func get_player_color(p_name: String) -> Color:
	return Color.from_hsv(wrapf(p_name.hash() * 0.001, 0.0, 1.0), 0.6, 1.0)
