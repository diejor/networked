class_name BomberGamestate
extends NetwService
## Manages the bomber game state as a session service.

const WORLD_SCENE := "res://examples/bomber/game/world.tscn"
const LOBBY_SCENE := "res://examples/bomber/game/lobby_level.tscn"

var player_name: String = "The Warrior"
var players := { }

signal player_list_changed()
signal game_ended()
signal game_error(what: String)

@onready var ctx: NetwMultiplayer = Netw.of(self)

var world: MultiplayerScene:
	get:
		return ctx.scenes.scene(&"World") if ctx else null

var lobby: MultiplayerScene:
	get:
		return ctx.scenes.scene(&"Lobby") if ctx else null


func _ready() -> void:
	setup_connections()


func _on_participant_joined(participant: NetwParticipant) -> void:
	players[participant.peer_id] = participant.username
	player_list_changed.emit()
	var target := _active_scene()
	if ctx.is_server() and is_instance_valid(target):
		target.admit(participant)


# The single active scene a fresh participant enters: the lobby between
# matches, the world while one runs.
func _active_scene() -> MultiplayerScene:
	var lobby_scene := lobby
	return lobby_scene if is_instance_valid(lobby_scene) else world


func _on_peer_disconnected(id: int) -> void:
	# A client leaving never ends the match. The framework despawns its player
	# entity and the survivors play on. Only the host leaving ends the session,
	# which reaches clients as server_disconnected (see _on_server_disconnected).
	unregister_player(id)


func _on_server_disconnected() -> void:
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


## Starts the match through [method Netw.change_scene_to_file]. The session is
## [constant NetwSceneConfig.Concurrency.SINGLE], so the change replaces the
## lobby and carries every participant into [code]World[/code].
func begin_game() -> void:
	assert(multiplayer.is_server())
	Netw.change_scene_to_file(self, WORLD_SCENE)


func end_game() -> void:
	# A client reaching here through server_disconnected has a dead peer, so
	# multiplayer.is_server() would query get_unique_id() on it and error. Only
	# an active peer can be the server, so gate the server-side teardown on it.
	var mp := multiplayer.multiplayer_peer
	var peer_active := mp != null \
			and mp.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED
	if peer_active and multiplayer.is_server() and is_instance_valid(world):
		Netw.change_scene_to_file(self, LOBBY_SCENE)

	game_ended.emit()
	players.clear()


func setup_connections() -> void:
	ctx.participant_joined.connect(_on_participant_joined)
	ctx.peer_disconnected.connect(_on_peer_disconnected)
	ctx.server_disconnected.connect(_on_server_disconnected)
	ctx.scenes.scene_spawned.connect(_on_scene_spawned)


# The listen host is accepted before the startup scene spawns, so its
# participant_joined admission finds no scene. Admission re-runs when the scene
# arrives, keeping join order and scene order decoupled.
func _on_scene_spawned(scene: MultiplayerScene) -> void:
	if not ctx.is_server():
		return
	for participant: NetwParticipant in ctx.participants:
		if participant.current_scene == null:
			scene.admit(participant)


func get_player_color(p_name: String) -> Color:
	return Color.from_hsv(wrapf(p_name.hash() * 0.001, 0.0, 1.0), 0.6, 1.0)
