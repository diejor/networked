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
@export var current_scene_path: String = "":
	get: return ResourceUID.ensure_path(current_scene_path)
	set(value):
		current_scene_path = value

## The root node name of the [member current_scene_path], used to look up the 
## active lobby.
var current_scene_name: String:
	get:
		return _resolve_scene_name(current_scene_path)

var _tp_mutex := AsyncMutex.new()
var _tp_span: NetSpan  # Span for the current teleport operation
var _dbg: NetwHandle = Netw.dbg.handle(self)


## Per-peer storage bucket for [TPComponent].
## Bridges the old client instance (which stores the promise) to the new instance
## (which resolves it) across the delete+respawn cycle caused by server reparenting.
class Bucket extends RefCounted:
	var pending: Dictionary[int, TeleportPromise] = {}
	var span: NetSpan # Active span for the local player's teleport

func _get_bucket() -> Bucket:
	return get_bucket(Bucket) as Bucket

class TeleportPromise extends RefCounted:
	## Returned by [method TPComponent.teleport] to observe the completion of a teleport.
	##
	## Survives the client node's lifetime — safe to await even when the client player
	## is destroyed and respawned during the teleport handshake.
	signal completed
	var span: NetSpan # Reference to the initiating span


func _init() -> void:
	## TODO: move name conventions to NetComponent
	name = "TPComponent"
	unique_name_in_owner = true

func _ready() -> void:
	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.delta_synchronized.is_connected(client_synchronized.emit):
			sync.delta_synchronized.connect(client_synchronized.emit)
			sync.synchronized.connect(client_synchronized.emit)


func _ensure_current_scene_path() -> void:
	if current_scene_path.is_empty() and starting_scene_path:
		current_scene_path = starting_scene_path.scene_path


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	_ensure_current_scene_path()


static func _resolve_scene_name(path_or_uid: String) -> String:
	if path_or_uid.is_empty():
		return ""

	var path: String = ResourceUID.ensure_path(path_or_uid)
	if not ResourceLoader.exists(path):
		Netw.dbg.error("Unable to find scene at path '%s'." % [path], func(m): push_error(m))
		return ""

	var scene: PackedScene = load(path)

	if not is_instance_valid(scene):
		Netw.dbg.error("Unable to find scene at path '%s'." % [path], func(m): push_error(m))
		return ""

	var scene_state: SceneState = scene.get_state()
	return scene_state.get_node_name(0)


## Initiates a teleport sequence from the client. Returns a [TPComponent.TeleportPromise] 
##  that resolves once the server confirms the reparent and the visual transition 
## completes. Safe to [operator await] even if this node is destroyed and respawned during 
## the handshake.
func teleport(target_tp: SceneNodePath) -> TeleportPromise:
	_tp_span = _dbg.span("tp", {"scene": target_tp.scene_path})
	var promise := TeleportPromise.new()
	promise.span = _tp_span
	_tp_span.step("initiate")
	_dbg.info("Initiating teleport to %s" % [target_tp.scene_path])
	_do_teleport(target_tp, promise)
	return promise


func _do_teleport(target_tp: SceneNodePath, promise: TeleportPromise) -> void:
	_dbg.trace("_do_teleport called.")
	_tp_span.step("awaiting_mutex")
	await _tp_mutex.lock()
	_tp_span.step("mutex_acquired")

	var peer_id := multiplayer.get_unique_id()
	var bucket := _get_bucket()
	if bucket:
		bucket.pending[peer_id] = promise
		bucket.span = _tp_span
	
	var from_scene := current_scene_name
	current_scene_path = target_tp.scene_path
	_tp_span.step("scene_path_set", {"from": from_scene, "to": current_scene_name})
	_dbg.debug("Teleporting from %s to %s" % [from_scene, current_scene_name])

	var save_component: SaveComponent = owner.get_node_or_null("%SaveComponent")
	if save_component:
		_dbg.debug("Pushing save state before teleport.")
		save_component.push_to(MultiplayerPeer.TARGET_PEER_SERVER)
		_tp_span.step("save_pushed")

	var tp_layer := get_tp_layer()
	if tp_layer:
		_dbg.trace("Playing teleport_out transition.")
		var phase := _tp_span.phase("transition_out")
		await tp_layer.teleport_out()
		phase.done()

	SynchronizersCache.sync_only_server(owner)

	# Clean Lock: disable physics and input on the player node during transition.
	# This does NOT affect components (children), so the TP handshake continues.
	owner.set_physics_process(false)
	owner.set_process_input(false)

	_dbg.trace("Sending request_teleport RPC to server.")
	_tp_span.step("rpc_sent")
	request_teleport.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		owner.name,
		from_scene,
		target_tp.scene_path,
		target_tp.node_path,
		_tp_span.checkpoint()
	)

@rpc("any_peer", "call_remote", "reliable")
func request_teleport(username: String, from_scene_name: String, to_scene_path: String, tp_path: String, token: Variant) -> void:
	var sender_id := multiplayer.get_remote_sender_id()
	var span := Netw.dbg.peer_span(self, "tp_server", [sender_id], {}, token as CheckpointToken)
	_dbg.info("Server received teleport request from %s to %s" % [username, to_scene_path])

	var lobby_manager := get_lobby_manager()
	if not lobby_manager:
		_dbg.error("Cannot teleport, lobby manager not found.", func(m): push_error(m))
		span.fail("no_lobby_manager")
		return

	var from_lobby: Lobby = lobby_manager.active_lobbies.get(from_scene_name)
	if not from_lobby:
		_dbg.error("Source lobby '%s' not found." % [from_scene_name], func(m): push_error(m))
		span.fail("source_lobby_not_found", {"lobby": from_scene_name})
		return

	var player: Node = from_lobby.level.get_node_or_null(username)
	if not player:
		_dbg.error("Player '%s' not found in source lobby." % [username], func(m): push_error(m))
		span.fail("player_not_found")
		return

	_flush_player_position(player)
	
	var tp_component: TPComponent = player.get_node("%TPComponent")
	tp_component.current_scene_path = to_scene_path
	
	await _sync_client_state(player, span)

	var to_lobby := await _activate_destination(to_scene_path, span)
	if not to_lobby:
		return

	_reparent_player(player, from_lobby, to_lobby, tp_path)
	span.end()


func _flush_player_position(player: Node) -> void:
	# Fix: Position Flush workaround for Godot issue #14578.
	# Move far away to force the PhysicsServer to cleanly exit any Area2D overlaps
	# This prevents the "!E" condition crash.
	var far_away: Variant = Vector3(99999, 99999, 99999) if player is Node3D else Vector2(99999, 99999)
	player.set("global_position", far_away)


func _sync_client_state(player: Node, span: NetSpan) -> void:
	var tp_component: TPComponent = player.get_node("%TPComponent")
	span.step("awaiting_client_sync")
	var timer := get_tree().create_timer(5.0)
	if await Async.timeout(tp_component.client_synchronized, timer):
		_dbg.error("Client couldn't synchronize while teleporting.", func(m): push_error(m))
		span.fail("client_sync_timeout")
	else:
		span.step("client_synced")


func _activate_destination(to_scene_path: String, span: NetSpan) -> Lobby:
	var lobby_manager := get_lobby_manager()
	var to_scene_name := _resolve_scene_name(to_scene_path)
	span.step("activating_lobby", {"lobby": to_scene_name})
	await lobby_manager.activate_lobby(StringName(to_scene_name))
	var to_lobby: Lobby = lobby_manager.active_lobbies.get(StringName(to_scene_name))
	if not to_lobby:
		_dbg.error("Destination lobby '%s' could not be activated." % [to_scene_name], func(m): push_error(m))
		span.fail("dest_lobby_activation_failed", {"lobby": to_scene_name})
		return null
	return to_lobby


func _reparent_player(player: Node, from_lobby: Lobby, to_lobby: Lobby, tp_path: String) -> void:
	var username := player.name
	var to_lobby_name := to_lobby.level.name
	var tp_component: TPComponent = player.get_node("%TPComponent")
	
	_dbg.info("Reparenting player %s to lobby %s" % [username, to_lobby_name])
	
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
	_dbg.trace("`teleported` callback on server.")
	var teleport_success := func() -> void:
		assert(is_inside_tree(), "TPComponent: `teleported` was called when `is_inside_tree = false`.")
		var snap_pos: Variant = Vector3.ZERO if owner is Node3D else Vector2.ZERO
		if scene:
			var tp_node: Node = scene.get_node_or_null(_tp_path)
			if tp_node:
				snap_pos = tp_node.get("global_position")

		_dbg.debug("Teleport server-side complete. Snapping to %s" % [str(snap_pos)])
		owner.set("global_position", snap_pos)
		_rpc_teleport_committed.rpc_id(owner.get_multiplayer_authority(), snap_pos)

	teleport_success.call_deferred()


@rpc("any_peer", "call_remote", "reliable")
func _rpc_teleport_committed(snap_pos: Variant) -> void:
	var peer_id := multiplayer.get_unique_id()
	var bucket := _get_bucket()

	# Recovery: If we were recreated, pick up the span from the persistent bucket
	if not _tp_span and bucket:
		_tp_span = bucket.span
		bucket.span = null

	_dbg.info("Teleport committed. Snapping local player to %s" % [str(snap_pos)])
	if _tp_span: _tp_span.step("committed", {"snap_pos": str(snap_pos)})
	_teleport_committed.emit()
	_tp_mutex.unlock()
	owner.set("global_position", snap_pos)

	# Unlock the player now that we've arrived and snapped.
	owner.set_physics_process(true)
	owner.set_process_input(true)

	var tp_layer := get_tp_layer()
	if tp_layer:
		_dbg.trace("Playing teleport_in transition.")
		var phase := _tp_span.phase("transition_in") if _tp_span else null
		await tp_layer.teleport_in()
		if phase: phase.done()

	var promise: TeleportPromise = bucket.pending.get(peer_id) if bucket else null
	if promise:
		_dbg.debug("Resolving teleport promise for peer %d" % [peer_id])
		if _tp_span: _tp_span.step("promise_resolved")
		promise.completed.emit()
		bucket.pending.erase(peer_id)
	
	if _tp_span:
		_tp_span.end()
		_tp_span = null


## Registers the entity with the specified lobby manager and spawns it into the active scene level.
func spawn(lobby_mgr: MultiplayerLobbyManager) -> void:
	_dbg.trace("spawn called.")
	_ensure_current_scene_path()
	
	if current_scene_path.is_empty():
		_dbg.error("Does not have a scene to tp into.", func(m): push_error(m))
		return

	var lobby: Lobby = lobby_mgr.active_lobbies.get(current_scene_name)
	if lobby:
		_dbg.info("Spawning player into lobby %s", [current_scene_name])
		lobby.synchronizer.track_player(owner)
		lobby.level.add_child(owner)
		owner.owner = lobby.level
