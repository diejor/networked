class_name BomberGamestate
extends Node

## Manages the bomber game state as a session service.

const DEFAULT_PORT = 10567
const MAX_PEERS = 12

var player_name: String = "The Warrior"
var players := {}

signal player_list_changed()
signal connection_failed()
signal connection_succeeded()
signal game_ended()
signal game_error(what: String)
signal match_started()

@onready var ctx: NetwContext = Netw.ctx(self)

func _enter_tree() -> void:
	NetwServices.register(self)


func _exit_tree() -> void:
	NetwServices.unregister(self)

func _ready() -> void:
	setup_connections()


func _on_player_joined(join_payload: JoinPayload) -> void:
	register_player.rpc_id(join_payload.peer_id, player_name)
	player_list_changed.emit()

func _on_peer_disconnected(id: int) -> void:
	if has_node(^"/root/World"):
		if multiplayer.is_server():
			game_error.emit("Player " + players[id] + " disconnected")
			end_game()
	else:
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
	jp.url = ip
	
	ctx.tree.connect_player(jp)

func host_game(_player_name: String) -> void:
	player_name = _player_name
	var jp := JoinPayload.new()
	jp.username = _player_name
	
	ctx.tree.connect_player(jp)

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


## Starts the match by activating [code]World[/code].
func begin_game() -> void:
	assert(multiplayer.is_server())
	_activate_world_scene()
	match_started.emit()


# Activates the bomber world.
func _activate_world_scene() -> void:
	var sm := ctx.services.get_scene_manager()
	if not sm:
		return
	
	sm.activate_scene(&"World")


func end_game() -> void:
	if has_node(^"/root/World"):
		get_node(^"/root/World").queue_free()

	game_ended.emit()
	players.clear()


func setup_connections() -> void:
	ctx.tree.player_joined.connect(_on_player_joined)
	ctx.tree.peer_disconnected.connect(_on_peer_disconnected)
	ctx.tree.connected_to_server.connect(_on_connected_ok)
	ctx.tree.server_disconnected.connect(_on_server_disconnected)


func get_player_color(p_name: String) -> Color:
	return Color.from_hsv(wrapf(p_name.hash() * 0.001, 0.0, 1.0), 0.6, 1.0)
