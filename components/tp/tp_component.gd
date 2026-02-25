class_name TPComponent
extends NodeComponent

signal teleport_committed

@export_file var starting_scene_path: String

@export_custom(PROPERTY_HINT_NONE, "replicated") 
var current_scene: String = "":
	get: return ResourceUID.ensure_path(current_scene)
	set(value):
		current_scene = value
		pass
	
var current_scene_name: String:
	get: 
		return get_scene_name(current_scene)

var owner2d: Node2D:
	get: return owner as Node2D


var _tp_mutex := AsyncMutex.new()


static func get_scene_name(path_or_uid: String) -> String:
	if path_or_uid.is_empty():
		return ""
	var path: String = ResourceUID.ensure_path(path_or_uid)
	var scene: PackedScene = load(path)
	if not is_instance_valid(scene):
		push_error("Unable to find scene.")
		return ""
	var scene_state: SceneState = scene.get_state()
	return scene_state.get_node_name(0)


func _enter_tree() -> void:
	if current_scene.is_empty():
		current_scene = starting_scene_path


func teleport(tp_id: String, new_scene: String) -> void:
	await _tp_mutex.lock()

	var from_scene := current_scene_name
	current_scene = new_scene
	var tp_path := "%" + tp_id + "/Marker2D"

	var save_component: SaveComponent = owner.get_node_or_null("%SaveComponent")
	if save_component:
		save_component.push_to(MultiplayerPeer.TARGET_PEER_SERVER)

	await tp_layer.teleport_out()

	request_teleport.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		owner.name,
		from_scene,
		tp_path
	)

	var timer := get_tree().create_timer(5.0)
	if await Async.timeout(teleport_committed, timer):
		await tp_layer.teleport_in_animation()
		_tp_mutex.unlock()
		push_error("Teleport commit timed out.")
		return

	_tp_mutex.unlock()


@rpc("any_peer", "call_remote", "reliable")
func request_teleport(
	username: String, 
	from_scene_name: String, 
	tp_path: String) -> void:
	var from_lobby: Lobby = lobby_manager.active_lobbies[from_scene_name]
	
	var player: Node2D = from_lobby.level.get_node(username)
	var tp_component: TPComponent = player.get_node("%TPComponent")
	
	var state: StateSynchronizer = player.get_node_or_null("%StateSynchronizer")
	if state:
		var timer := get_tree().create_timer(3.0)
		if await Async.timeout(state.delta_synchronized, timer):
			push_error("Client couldn't synchronize while teleporting.")
	
	var to_lobby: Lobby = lobby_manager.active_lobbies[tp_component.current_scene_name]
	
	var flip := func(event: Signal, from: Callable, to: Callable) -> void:
		event.disconnect(from)
		event.connect(to.bind(player))
		if event == player.tree_exiting:
			player.request_ready()
			tp_component.teleported(to_lobby.level, tp_path)
			
	
	var from_spawn := from_lobby.synchronizer._on_spawned
	var to_spawn := to_lobby.synchronizer._on_spawned
	var from_despawn := from_lobby.synchronizer._on_despawned
	var to_despawn := to_lobby.synchronizer._on_despawned
	
	flip.call(player.tree_entered, from_spawn, to_spawn)
	
	player.tree_entered.connect(flip.bind(player.tree_exiting, from_despawn, to_despawn))
	player.reparent(to_lobby.level)
	player.tree_entered.disconnect(flip)


func teleported(scene: Node, _tp_path: String) -> void:
	if scene:
		var tp_node: Marker2D = scene.get_node_or_null(_tp_path)
		if tp_node:
			owner2d.global_position = tp_node.global_position
	
	var teleport_success := func() -> void:
		assert(is_inside_tree(), "`teleported` was called when `is_inside_tree = false`.")
		_rpc_teleport_committed.rpc_id(owner.get_multiplayer_authority())
	
	teleport_success.call_deferred()

@rpc("any_peer", "call_remote", "reliable")
func _rpc_teleport_committed() -> void:
	teleport_committed.emit()


func spawn(lobby_mgr: MultiplayerLobbyManager) -> void:
	if current_scene.is_empty():
		push_error("`TPComponent` doesnt have a scene to tp into.")
		return
	
	var lobby: Lobby = lobby_mgr.active_lobbies[current_scene_name]
	lobby.synchronizer.track_player(owner)
	lobby.level.add_child(owner)
	owner.owner = lobby.level
