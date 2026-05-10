class_name BomberGamestate
extends Node

## Manages the bomber game state as a session service.

const DEFAULT_PORT = 10567
const MAX_PEERS = 12
const PLAYER_SCENE := preload("res://examples/bomber/player.tscn")

var player_name: String = "The Warrior"
var players := {}

signal player_list_changed()
signal connection_failed()
signal connection_succeeded()
signal game_ended()
signal game_error(what: String)

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


## Starts the match by activating [code]World[/code] and spawning players.
func begin_game() -> void:
	assert(multiplayer.is_server())
	
	var scene := await _activate_world_scene()
	if not scene:
		return
	
	_spawn_joined_players(scene)


# Activates the bomber world and returns its managed scene wrapper.
func _activate_world_scene() -> MultiplayerScene:
	var sm := ctx.services.get_scene_manager()
	if not sm:
		return null
	
	sm.activate_scene(&"World")
	var scene := sm.active_scenes.get(&"World") as MultiplayerScene
	while not scene:
		var spawned_scene: MultiplayerScene = await sm.scene_spawned
		if spawned_scene.level.name == &"World":
			scene = spawned_scene
	
	return scene


# Spawns one Networked player entity for each accepted join payload.
func _spawn_joined_players(scene: MultiplayerScene) -> void:
	var joined_players := ctx.tree.get_joined_players()
	joined_players.sort_custom(
		func(a: JoinPayload, b: JoinPayload) -> bool:
			return a.peer_id < b.peer_id
	)
	
	for index in joined_players.size():
		_register_score_player(scene.level, joined_players[index])
		_spawn_player(scene, joined_players[index], index)


# Registers the joined player in the local world score table.
func _register_score_player(world: Node, join_payload: JoinPayload) -> void:
	var score := world.get_node_or_null("Score")
	if score and score.has_method("add_player"):
		score.add_player(join_payload.peer_id, str(join_payload.username))


# Spawns a player through SpawnerPlayerComponent lifecycle hooks.
func _spawn_player(
	scene: MultiplayerScene,
	join_payload: JoinPayload,
	spawn_index: int
) -> void:
	var world := scene.level
	var players_root := world.get_node_or_null("Players")
	if not players_root:
		return
	
	var node_name := SpawnerComponent.format_name(
		str(join_payload.username),
		join_payload.peer_id
	)
	if players_root.get_node_or_null(node_name):
		return
	
	var template := PLAYER_SCENE.instantiate()
	var spawn_position := _get_spawn_position(world, spawn_index)
	var player := SpawnerComponent.instantiate_from(
		template,
		func(spawner: SpawnerComponent) -> void:
			var player_spawner := spawner as SpawnerPlayerComponent
			if player_spawner:
				player_spawner.username = str(join_payload.username)
				player_spawner.player_peer_id = join_payload.peer_id
				player_spawner.authority_mode = (
					SpawnerComponent.AuthorityMode.SERVER
				)
			
			spawner.add_spawn_property(NodePath(".:synced_position"))
			spawner.add_spawn_property(NodePath("label:text"))
			
			spawner.owner.name = node_name
			spawner.owner.set("position", spawn_position)
			spawner.owner.set("synced_position", spawn_position)
			
			var label := spawner.owner.get_node_or_null("label") as Label
			if label:
				label.text = str(join_payload.username)
	)
	template.free()
	
	players_root.add_child(player)
	player.owner = world


# Returns a deterministic spawn point for [param spawn_index].
func _get_spawn_position(world: Node, spawn_index: int) -> Vector2:
	var spawn_points := world.get_node_or_null("SpawnPoints")
	if not spawn_points or spawn_points.get_child_count() == 0:
		return Vector2.ZERO
	
	var point_index := spawn_index % spawn_points.get_child_count()
	var marker := spawn_points.get_child(point_index) as Node2D
	return marker.position if marker else Vector2.ZERO


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
