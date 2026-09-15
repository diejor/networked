class_name BomberGamestate
extends NetwService

const WORLD_SCENE := "res://examples/bomber/game/world.tscn"
const LOBBY_SCENE := "res://examples/bomber/game/lobby_level.tscn"

signal game_ended()
signal game_error(what: String)

@onready var session: NetwSessionHandle = Netw.session(self)

var roster: Array[NetwParticipant] = []
var spawner: BomberPlayerSpawner

var world: NetwSceneHandle:
	get:
		return Netw.scene(self, &"World")

var lobby: NetwSceneHandle:
	get:
		return Netw.scene(self, &"Lobby")


func _init() -> void:
	Netw.configure_spawn(spawn_lobby)
	Netw.configure_join(self, place_player)


func _ready() -> void:
	session.participant_left.connect(leave_game)
	session.disconnected.connect(on_server_disconnected)
	session.scene_live.connect(on_scene_live)


func spawn_lobby() -> Node:
	var packed: PackedScene = load(LOBBY_SCENE)
	return packed.instantiate()


func open_lobby() -> NetwSceneHandle:
	var waiting := lobby
	if waiting != null:
		return waiting
	var level: Node = Netw.spawn(spawn_lobby)
	get_parent().add_child(level)
	return Netw.scene(level)


func place_player(participant: NetwParticipant) -> void:
	roster.append(participant)
	var running := world
	if running == null:
		open_lobby().watch(participant)
		return
	running.watch(participant)
	spawner.spawn_participant(participant, roster.size() - 1)


func leave_game(participant: NetwParticipant) -> void:
	roster.erase(participant)


func on_server_disconnected() -> void:
	game_error.emit("Server disconnected")
	end_game()


func begin_game() -> NetwPromise:
	assert(multiplayer.is_server())
	return Netw.change_scene_to_file(self, WORLD_SCENE)


func end_game() -> void:
	var mp := multiplayer.multiplayer_peer
	var peer_active := mp != null \
			and mp.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED
	if peer_active and multiplayer.is_server() and world != null:
		release_players()
		spawner = null
		Netw.change_scene_to_file(self, LOBBY_SCENE)

	game_ended.emit()


func release_players() -> void:
	for player: NetwEntity in world.players:
		player.owner.queue_free()


func on_scene_live(arrived: NetwSceneHandle) -> void:
	session.present(arrived)
	if arrived.label == &"Lobby":
		var in_lobby: InLobby = arrived.root.find_child("InLobby", true, false)
		in_lobby.start_pressed.connect(begin_game)
		return
	if arrived.label != &"World" or not multiplayer.is_server():
		return
	open_match(arrived)


func open_match(world_scene: NetwSceneHandle) -> void:
	if not world_scene.root.is_node_ready():
		world_scene.root.ready.connect(
			open_match.bind(world_scene),
			CONNECT_ONE_SHOT,
		)
		return
	spawner = world_scene.root.get_node(^"PlayerSpawner")
	for slot in roster.size():
		spawner.spawn_participant(roster[slot], slot)


func get_player_color(p_name: String) -> Color:
	return Color.from_hsv(wrapf(p_name.hash() * 0.001, 0.0, 1.0), 0.6, 1.0)
