@tool
class_name TPComponent
extends Node

## Manages entity teleportation across multiplayer lobbies and scene transitions.
##
## Coordinates the complex handoff between the client and server, handling state saving,
## visual transitions, lobby reparenting, and synchronizer rebinding.

## Emitted locally on the client when the server confirms the entity has successfully reparented and repositioned.
signal teleport_committed
signal client_synchronized

## The default scene path assigned when the component enters the tree if no scene is currently set.
@export_file var starting_scene_path: String

## The UID or file path of the scene the entity currently resides in. Automatically resolves to a valid path.
@export_custom(PROPERTY_HINT_NONE, "replicated:on_change") 
var current_scene_path: String = "":
	get: return ResourceUID.ensure_path(current_scene_path)
	set(value):
		current_scene_path = value
	
## The root node name of the [member current_scene_path], used to look up the active lobby.
var current_scene_name: String:
	get: 
		return _get_scene_name(current_scene_path)

## Strongly typed reference to the owner.
var owner2d: Node2D:
	get: return owner as Node2D

var _tp_mutex := AsyncMutex.new()


func _get_configuration_warnings() -> PackedStringArray:
	return ReplicationValidator.get_configuration_warnings(self)

func _validate_editor() -> void:
	ReplicationValidator.verify_and_configure(self)

func _ready() -> void:
	if EditorTooling.validate_and_halt(self, _validate_editor):
		return
	
	# Wire up the synchronization signal for the client
	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.delta_synchronized.is_connected(client_synchronized.emit):
			sync.delta_synchronized.connect(client_synchronized.emit)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	
	if current_scene_path.is_empty():
		current_scene_path = starting_scene_path


static func _get_scene_name(path_or_uid: String) -> String:
	if path_or_uid.is_empty():
		return ""
		
	var path: String = ResourceUID.ensure_path(path_or_uid)
	var scene: PackedScene = load(path)
	
	if not is_instance_valid(scene):
		push_error("TPComponent: Unable to find scene at path %s." % path)
		return ""
		
	var scene_state: SceneState = scene.get_state()
	return scene_state.get_node_name(0)


## Initiates a secure teleport sequence from the client.
## Locks the component, syncs state with the server, handles visual layers, and awaits server confirmation.
func teleport(target_tp: SceneNodePath) -> void:
	await _tp_mutex.lock()

	var from_scene := current_scene_name
	current_scene_path = target_tp.scene_path

	var save_component: SaveComponent = owner.get_node_or_null("%SaveComponent")
	if save_component:
		save_component.push_to(MultiplayerPeer.TARGET_PEER_SERVER)
	
	var tp_layer := NetworkedAPI.get_tp_layer(self)
	if tp_layer:
		await tp_layer.teleport_out()
	
	SynchronizersCache.sync_only_server(owner)
	
	request_teleport.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		owner.name,
		from_scene,
		target_tp.node_path
	)

	var timer := get_tree().create_timer(5.0)
	if await Async.timeout(teleport_committed, timer):
		if tp_layer:
			await tp_layer.teleport_in()
		_tp_mutex.unlock()
		push_error("TPComponent: Teleport commit timed out.")
		return

	_tp_mutex.unlock()


@rpc("any_peer", "call_remote", "reliable")
func request_teleport(username: String, from_scene_name: String, tp_path: String) -> void:
	var lobby_manager := NetworkedAPI.get_lobby_manager(self)
	if not lobby_manager:
		push_error("TPComponent: Cannot teleport, lobby manager not found.")
		return
		
	var from_lobby: Lobby = lobby_manager.active_lobbies.get(from_scene_name)
	var player: Node2D = from_lobby.level.get_node(username)
	var tp_component: TPComponent = player.get_node("%TPComponent")
	
	var timer := get_tree().create_timer(5.0)
	if await Async.timeout(tp_component.client_synchronized, timer):
		push_error("TPComponent: Client couldn't synchronize while teleporting.")
	
	var to_lobby: Lobby = lobby_manager.active_lobbies.get(tp_component.current_scene_name)
	
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


## Server-side callback invoked after the entity safely enters the destination lobby.
## Snaps the entity to the target coordinates and signals the client to finalize the visual transition.
func teleported(scene: Node, _tp_path: String) -> void:
	if scene:
		var tp_node: Marker2D = scene.get_node_or_null(_tp_path)
		if tp_node:
			owner2d.global_position = tp_node.global_position
	
	var teleport_success := func() -> void:
		assert(is_inside_tree(), "TPComponent: `teleported` was called when `is_inside_tree = false`.")
		_rpc_teleport_committed.rpc_id(owner.get_multiplayer_authority())
	
	teleport_success.call_deferred()


@rpc("any_peer", "call_remote", "reliable")
func _rpc_teleport_committed() -> void:
	teleport_committed.emit()


## Registers the entity with the specified lobby manager and spawns it into the active scene level.
func spawn(lobby_mgr: MultiplayerLobbyManager) -> void:
	if current_scene_path.is_empty():
		push_error("TPComponent: Does not have a scene to tp into.")
		return
	
	var lobby: Lobby = lobby_mgr.active_lobbies.get(current_scene_name)
	if lobby:
		lobby.synchronizer.track_player(owner)
		lobby.level.add_child(owner)
		owner.owner = lobby.level
