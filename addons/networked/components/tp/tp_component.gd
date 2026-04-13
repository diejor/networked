@tool
class_name TPComponent
extends NetComponent

## Manages cross-lobby teleportation for a player in a multiplayer session.
##
## Coordinates a multi-step handshake: the client requests a teleport via 
## [method teleport], the server reparents the player to the destination lobby, 
## and then confirms with [signal TeleportPromise.completed]. Requires a 
## [TPLayerAPI] in the scene for visual transition animations.
## 
## [codeblock]
## # From a client-owned node:
## var tp := %TPComponent.teleport(target_node_path)
## await tp.completed
## print("Teleport finished!")
## [/codeblock]

signal _teleport_committed

## Emitted each time a client-owned [MultiplayerSynchronizer] delivers a delta update.
##
## TODO: move to NetComponent
signal client_synchronized

## The default scene path assigned when the component enters the tree if no scene 
## is currently set.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:MultiplayerSpawner")
var starting_scene_path: SceneNodePath

## The UID or file path of the scene the player currently resides in.
##
## Automatically resolves to a valid path via [method ResourceUID.ensure_path].
## Replicated on change so clients can track which lobby their player  is in.
@export_custom(PROPERTY_HINT_NONE, "replicated:on_change")
var current_scene_path: String = "":
	get: return ResourceUID.ensure_path(current_scene_path)
	set(value):
		current_scene_path = value

## The root node name of the [member current_scene_path], used to look up the 
## active lobby.
var current_scene_name: String:
	get:
		return _resolve_scene_name(current_scene_path)

var _tp_mutex := AsyncMutex.new()
var _tp_cid: StringName = &""  # correlation ID for the current teleport operation


## Per-peer storage bucket for [TPComponent].
## Bridges the old client instance (which stores the promise) to the new instance
## (which resolves it) across the delete+respawn cycle caused by server reparenting.
class Bucket extends RefCounted:
	var pending: Dictionary[int, TeleportPromise] = {}

func _get_bucket() -> Bucket:
	return get_bucket(Bucket) as Bucket

class TeleportPromise extends RefCounted:
	## Returned by [method TPComponent.teleport] to observe the completion of a teleport.
	##
	## Survives the client node's lifetime — safe to await even when the client player
	## is destroyed and respawned during the teleport handshake.
	signal completed


func _get_configuration_warnings() -> PackedStringArray:
	return ReplicationValidator.get_configuration_warnings(self)


func _validate_editor() -> void:
	ReplicationValidator.verify_and_configure(self)


func _init() -> void:
	## TODO: move name conventions to NetComponent
	name = "TPComponent"
	unique_name_in_owner = true


func _ready() -> void:
	if EditorTooling.validate_and_halt(self, _validate_editor):
		return

	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.delta_synchronized.is_connected(client_synchronized.emit):
			sync.delta_synchronized.connect(client_synchronized.emit)
			sync.synchronized.connect(client_synchronized.emit)


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	if current_scene_path.is_empty():
		current_scene_path = starting_scene_path.scene_path


static func _resolve_scene_name(path_or_uid: String) -> String:
	if path_or_uid.is_empty():
		return ""

	var path: String = ResourceUID.ensure_path(path_or_uid)
	if not ResourceLoader.exists(path):
		push_error("Unable to find scene at path %s." % path)
		return ""

	var scene: PackedScene = load(path)

	if not is_instance_valid(scene):
		push_error("Unable to find scene at path %s." % path)
		return ""

	var scene_state: SceneState = scene.get_state()
	return scene_state.get_node_name(0)


## Initiates a teleport sequence from the client. Returns a [TeleportPromise] 
##  that resolves once the server confirms the reparent and the visual transition 
## completes. Safe to [operator await] even if this node is destroyed and respawned during 
## the handshake.
func teleport(target_tp: SceneNodePath) -> TeleportPromise:
	_tp_cid = StringName("tp_%d" % Time.get_ticks_usec())
	_emit_debug_event(&"tp.initiate", {"scene": target_tp.scene_path}, _tp_cid)
	log_info("Initiating teleport to %s" % target_tp.scene_path)
	var promise := TeleportPromise.new()
	_do_teleport(target_tp, promise)
	return promise


func _do_teleport(target_tp: SceneNodePath, promise: TeleportPromise) -> void:
	log_trace("TPComponent: _do_teleport called.")
	_emit_debug_event(&"tp.awaiting_mutex", {}, _tp_cid)
	await _tp_mutex.lock()
	_emit_debug_event(&"tp.mutex_acquired", {}, _tp_cid)

	var peer_id := multiplayer.get_unique_id()
	_get_bucket().pending[peer_id] = promise

	var from_scene := current_scene_name
	current_scene_path = target_tp.scene_path
	_emit_debug_event(&"tp.scene_path_set", {"from": from_scene, "to": current_scene_name}, _tp_cid)
	log_debug("Teleporting from %s to %s (Peer: %d)" % [from_scene, current_scene_name, peer_id])

	var save_component: SaveComponent = owner.get_node_or_null("%SaveComponent")
	if save_component:
		log_debug("Pushing save state before teleport.")
		save_component.push_to(MultiplayerPeer.TARGET_PEER_SERVER)
		_emit_debug_event(&"tp.save_pushed", {}, _tp_cid)

	var tp_layer := get_tp_layer()
	if tp_layer:
		log_trace("Playing teleport_out transition.")
		_emit_debug_event(&"tp.transition_out_begin", {}, _tp_cid)
		await tp_layer.teleport_out()
		_emit_debug_event(&"tp.transition_out_end", {}, _tp_cid)

	SynchronizersCache.sync_only_server(owner)

	log_trace("Sending request_teleport RPC to server.")
	_emit_debug_event(&"tp.rpc_sent", {}, _tp_cid)
	request_teleport.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		owner.name,
		from_scene,
		target_tp.node_path
	)


@rpc("any_peer", "call_remote", "reliable")
func request_teleport(username: String, from_scene_name: String, tp_path: String) -> void:
	var sender_id := multiplayer.get_remote_sender_id()
	var srv_cid := StringName("tp_srv_%d" % Time.get_ticks_usec())
	log_info("Server received teleport request from %s (Peer: %d)" % [username, sender_id])

	var lobby_manager := get_lobby_manager()
	if not lobby_manager:
		log_error("TPComponent: Cannot teleport, lobby manager not found.")
		return

	var from_lobby: Lobby = lobby_manager.active_lobbies.get(from_scene_name)
	if not from_lobby:
		log_error("TPComponent: Source lobby '%s' not found." % from_scene_name)
		return

	var player: Node = from_lobby.level.get_node(username)
	var tp_component: TPComponent = player.get_node("%TPComponent")
	tp_component._emit_debug_event(&"tp.server_received",
		{"from_scene": from_scene_name, "sender": sender_id}, srv_cid)

	log_trace("Waiting for client synchronization...")
	tp_component._emit_debug_event(&"tp.awaiting_client_sync", {}, srv_cid)
	var timer := get_tree().create_timer(5.0)
	if await Async.timeout(tp_component.client_synchronized, timer):
		log_error("TPComponent: Client couldn't synchronize while teleporting.")
		tp_component._emit_debug_event(&"tp.sync_timeout", {}, srv_cid)
	else:
		tp_component._emit_debug_event(&"tp.client_synced", {}, srv_cid)

	var to_lobby_name := tp_component.current_scene_name
	tp_component._emit_debug_event(&"tp.activating_lobby", {"lobby": to_lobby_name}, srv_cid)
	await lobby_manager.activate_lobby(StringName(to_lobby_name))
	var to_lobby: Lobby = lobby_manager.active_lobbies.get(StringName(to_lobby_name))
	if not to_lobby:
		log_error("TPComponent: Destination lobby '%s' could not be activated." % to_lobby_name)
		return

	log_info("Reparenting player %s to lobby %s" % [username, to_lobby_name])
	tp_component._emit_debug_event(&"tp.reparenting", {"to_lobby": to_lobby_name}, srv_cid)

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
## Sets position on the server and forwards the snap coordinates to the client.
func teleported(scene: Node, _tp_path: String) -> void:
	log_trace("TPComponent: teleported callback on server.")
	var teleport_success := func() -> void:
		assert(is_inside_tree(), "TPComponent: `teleported` was called when `is_inside_tree = false`.")
		var snap_pos: Variant = Vector3.ZERO if owner is Node3D else Vector2.ZERO
		if scene:
			var tp_node: Node = scene.get_node_or_null(_tp_path)
			if tp_node:
				snap_pos = tp_node.get("global_position")

		log_debug("Teleport server-side complete. Snapping to %s" % str(snap_pos))
		owner.set("global_position", snap_pos)
		_rpc_teleport_committed.rpc_id(owner.get_multiplayer_authority(), snap_pos)

	teleport_success.call_deferred()


@rpc("any_peer", "call_remote", "reliable")
func _rpc_teleport_committed(snap_pos: Variant) -> void:
	log_info("Teleport committed. Snapping local player to %s" % str(snap_pos))
	_emit_debug_event(&"tp.committed", {"snap_pos": str(snap_pos)}, _tp_cid)
	_teleport_committed.emit()
	_tp_mutex.unlock()
	owner.set("global_position", snap_pos)

	var tp_layer := get_tp_layer()
	if tp_layer:
		log_trace("Playing teleport_in transition.")
		_emit_debug_event(&"tp.transition_in_begin", {}, _tp_cid)
		await tp_layer.teleport_in()
		_emit_debug_event(&"tp.transition_in_end", {}, _tp_cid)

	var peer_id := multiplayer.get_unique_id()
	var bucket := _get_bucket()
	var promise: TeleportPromise = bucket.pending.get(peer_id) if bucket else null
	if promise:
		log_debug("Resolving teleport promise for peer %d" % peer_id)
		_emit_debug_event(&"tp.promise_resolved", {}, _tp_cid)
		promise.completed.emit()
		bucket.pending.erase(peer_id)


## Registers the entity with the specified lobby manager and spawns it into the active scene level.
func spawn(lobby_mgr: MultiplayerLobbyManager) -> void:
	log_trace("TPComponent: spawn called.")
	if current_scene_path.is_empty():
		log_error("TPComponent: Does not have a scene to tp into.")
		return

	var lobby: Lobby = lobby_mgr.active_lobbies.get(current_scene_name)
	if lobby:
		log_info("Spawning player into lobby %s" % current_scene_name)
		lobby.synchronizer.track_player(owner)
		lobby.level.add_child(owner)
		owner.owner = lobby.level
