class_name TPComponent
extends NodeComponent

var owner2d: Node2D:
	get: return owner as Node2D

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
	var previous_scene_name: String = current_scene_name
	current_scene = new_scene
	var tp_path: String = "%" + tp_id + "/Marker2D"
	
	var save_component: SaveComponent = owner.get_node_or_null("%SaveComponent")
	if save_component:
		save_component.push_to(MultiplayerPeer.TARGET_PEER_SERVER)
	
	await transition_player.teleport_out_animation()
	
	request_teleport.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		owner.name,
		previous_scene_name,
		tp_path
	)
	
	state_sync.only_server()

@rpc("any_peer", "call_remote", "reliable")
func request_teleport(
	username: String, 
	from_scene_name: String, 
	tp_path: String) -> void:
	var from_lobby: Lobby = lobby_manager.active_lobbies[from_scene_name]
	
	var player: Node2D = from_lobby.level.get_node(username)
	var tp_component: TPComponent = player.get_node("%TPComponent")
	
	var state: StateSynchronizer = player.get_node_or_null("%StateSynchronizer")
	if state: # TODO: add a timeout for the case the client never synchronizes
		await state.delta_synchronized # Make sure the player has been updated
	multiplayer
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


func spawn(lobby_mgr: MultiplayerLobbyManager) -> void:
	if current_scene.is_empty():
		push_error("`TPComponent` doesnt have a scene to tp into.")
		return
	
	var lobby: Lobby = lobby_mgr.active_lobbies[current_scene_name]
	lobby.synchronizer.track_player(owner)
	lobby.level.add_child(owner)
	owner.owner = lobby.level
