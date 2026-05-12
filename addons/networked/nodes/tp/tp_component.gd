class_name TPComponent
extends NetwComponent

## Cross-scene teleportation for a player-owned entity.
##
## [method teleport] returns a [TPComponent.TeleportPromise] that survives node
## destruction, so [operator await] is safe across the delete+respawn
## cycle. Requires a [TPLayerAPI] in the destination scene for
## transition animations.
##
## [codeblock]
## var tp := %TPComponent.teleport(target_scene)
## await tp.completed
## [/codeblock]

signal _teleport_committed



## Fallback scene when [member current_scene_path] is empty on tree entry.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:MultiplayerSpawner")
var starting_scene_path: SceneNodePath

## The scene the player currently resides in. Replicates on change.
@export var current_scene_path: String = "":
	get: return ResourceUID.ensure_path(current_scene_path)
	set(value):
		current_scene_path = value

## The root node name of [member current_scene_path]'s scene.
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
	## TODO: move name conventions to NetwComponent
	name = "TPComponent"
	unique_name_in_owner = true


func _notification(what: int) -> void:
	if what != NOTIFICATION_PARENTED or Engine.is_editor_hint():
		return
	
	var entity := Netw.ctx(self).entity
	if not (entity or entity.owner):
		return
	
	var rel := entity.owner.get_path_to(self)
	entity.contribute_spawn_property("%s:current_scene_path" % rel)
	entity.contribute_save_property(
		&"current_scene_path",
		"%s:current_scene_path" % rel,
	)


func _ready() -> void:
	pass


func _step(label: String, data: Dictionary = {}) -> void:
	if _tp_span: _tp_span.step(label, data)


func _begin_tp_span(scene_path: String, promise: TeleportPromise) -> void:
	_tp_span = _dbg.span("tp", {"scene": scene_path})
	promise.span = _tp_span
	_step("initiate")
	_dbg.info("Initiating teleport to %s" % [scene_path])


func _end_tp_span() -> void:
	if _tp_span:
		_tp_span.end()
		_tp_span = null


func _recover_tp_span() -> void:
	if _tp_span:
		return
	var bucket := _get_bucket()
	if bucket:
		_tp_span = bucket.span
		bucket.span = null


func _fail_span(
	span: NetSpan,
	reason: String,
	msg: String,
	args: Array = [],
	data: Dictionary = {},
) -> void:
	_dbg.error(msg, args, func(m): push_error(m))
	if span:
		span.fail(reason, data)


## Copies [member starting_scene_path] into
## [member current_scene_path] when the latter is empty.
## Runs on tree entry; call explicitly for pre-tree-entry setup.
func ensure_current_scene_path() -> void:
	if current_scene_path.is_empty() and starting_scene_path:
		current_scene_path = starting_scene_path.scene_path


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return

	ensure_current_scene_path()


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


## Returns a [TPComponent.TeleportPromise] that resolves when the teleport
## completes. Safe to [operator await] across the delete+respawn cycle.
func teleport(target_tp: SceneNodePath) -> TeleportPromise:
	_flush_player_position(owner)
	var promise := TeleportPromise.new()
	_begin_tp_span(target_tp.scene_path, promise)
	_do_teleport(target_tp, promise)
	return promise


func _do_teleport(target_tp: SceneNodePath, promise: TeleportPromise) -> void:
	_step("awaiting_mutex")
	await _tp_mutex.lock()
	_step("mutex_acquired")
	
	
	var peer_id := multiplayer.get_unique_id()
	var bucket := _get_bucket()
	if bucket:
		bucket.pending[peer_id] = promise
		bucket.span = _tp_span
	
	var from_scene := current_scene_name
	current_scene_path = target_tp.scene_path
	_step("scene_path_set", {"from": from_scene, "to": current_scene_name})

	var save_component: SaveComponent = owner.get_node_or_null("%SaveComponent")
	if save_component:
		save_component.push_to.call_deferred(MultiplayerPeer.TARGET_PEER_SERVER, true)
		_step("save_pushed")
		var timer := get_tree().create_timer(5.0)
		if await Async.timeout(save_component.push_acknowledged, timer):
			_step("save_ack_timeout")
	else:
		_step("save_push_skipped")

	var tp_layer := get_tp_layer()
	if tp_layer:
		var phase := _tp_span.phase("transition_out")
		await tp_layer.teleport_out()
		phase.done()

	# Don't restrict visibility on the server (listen-server case): doing so
	# kills public_visibility on the canonical synchronizers and the player
	# becomes permanently invisible to remote clients after reparent.
	if not multiplayer.is_server():
		SynchronizersCache.sync_only_server(owner)

	# disable physics and input on the player node during transition.
	owner.set_physics_process(false)
	owner.set_process_input(false)
	
	_step("rpc_sent")
	_request_teleport.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		owner.name,
		from_scene,
		target_tp.scene_path,
		target_tp.node_path,
		_tp_span.checkpoint()
	)

# Internal RPC called by the client to request a teleport from the server.
@rpc("any_peer", "call_local", "reliable")
func _request_teleport(username: String, 
	from_scene_name: String, 
	to_scene_path: String, 
	tp_path: String, 
	token: Variant
) -> void:
	if not multiplayer.is_server():
		_dbg.warn("_request_teleport received on non-server peer %d", [multiplayer.get_unique_id()])
		return
	var sender_id := multiplayer.get_remote_sender_id()
	var span := Netw.dbg.peer_span(self, "tp_server", [sender_id], {}, token as CheckpointToken)
	_dbg.info("Server received teleport request from %s to %s" % [username, to_scene_path])
	
	var scene_manager := get_scene_manager()
	if not scene_manager:
		_fail_span(span, "no_scene_manager", "Cannot teleport, scene manager not found.")
		return
	
	var player := owner
	var from_scene := MultiplayerTree.scene_for_node(player) as MultiplayerScene
	if not from_scene:
		from_scene = scene_manager.active_scenes.get(from_scene_name)
	if not from_scene:
		_fail_span(span, "source_scene_not_found",
			"Source scene '%s' not found.", [from_scene_name],
			{"scene": from_scene_name})
		return
	
	if not is_instance_valid(player) or not from_scene.level.is_ancestor_of(player):
		_fail_span(span, "player_not_found",
			"Player '%s' not found in source scene.", [username])
		return
	
	var tp_component: TPComponent = player.get_node("%TPComponent")
	tp_component.current_scene_path = to_scene_path
	
	var authority := player.get_multiplayer_authority()
	var ctx := get_context()
	
	if authority == 1 and ctx and ctx.tree.is_listen_server():
		span.step("client_synced")
		await get_tree().physics_frame
		await get_tree().physics_frame
	
	var to_scene_node := await _activate_destination(to_scene_path, span)
	if not to_scene_node:
		return
	
	_reparent_player(player, from_scene, to_scene_node, tp_path)
	span.end()


func _flush_player_position(player: Node) -> void:
	# Fix: Position Flush workaround for Godot issue #14578.
	# Move far away to force the PhysicsServer to cleanly exit any Area2D overlaps
	# This prevents the "!E" condition crash.
	var far_away: Variant = Vector3(99999, 99999, 99999) if player is Node3D else Vector2(99999, 99999)
	player.set("global_position", far_away)


func _activate_destination(to_scene_path: String, span: NetSpan) -> MultiplayerScene:
	var scene_manager := get_scene_manager()
	var to_scene_name := _resolve_scene_name(to_scene_path)
	span.step("activating_scene", {"scene": to_scene_name})
	await scene_manager.activate_scene(StringName(to_scene_name))
	var to_scene: MultiplayerScene = scene_manager.active_scenes.get(StringName(to_scene_name))
	if not to_scene:
		_fail_span(span, "dest_scene_activation_failed",
			"Destination scene '%s' could not be activated.",
			[to_scene_name], {"scene": to_scene_name})
		return null
	return to_scene


func _reparent_player(player: Node, from_scene: MultiplayerScene, to_scene: MultiplayerScene, tp_path: String) -> void:
	var username := player.name
	var to_scene_name := to_scene.level.name
	var tp_component: TPComponent = player.get_node("%TPComponent")

	_dbg.info("Reparenting player %s to scene %s" % [username, to_scene_name])

	var flip := func(event: Signal, from: Callable, to: Callable) -> void:
		event.disconnect(from)
		var bound := to.bind(player)
		if not event.is_connected(bound):
			event.connect(bound)
		if event == player.tree_exiting:
			# request_ready does NOT cascade to children, so child components
			# whose _exit_tree unregistered them (e.g. TickInterpolator) would
			# never re-init. Reset the ready flag for the whole subtree.
			_request_ready_recursive(player)
			to_scene.register_player(player)
		tp_component._teleported(to_scene.level, tp_path)

	var from_spawn := from_scene.synchronizer._on_spawned
	var to_spawn := to_scene.synchronizer._on_spawned
	var from_despawn := from_scene.synchronizer._on_despawned
	var to_despawn := to_scene.synchronizer._on_despawned

	flip.call(player.tree_entered, from_spawn, to_spawn)
	player.tree_entered.connect(flip.bind(player.tree_exiting, from_despawn, to_despawn))

	player.reparent(to_scene.level)
	player.tree_entered.disconnect(flip)


static func _request_ready_recursive(node: Node) -> void:
	node.request_ready()
	for child in node.get_children():
		_request_ready_recursive(child)


# Server-side callback invoked after the entity safely enters the destination scene.
# Sets position on the server and forwards the snap coordinates to the client.
func _teleported(scene: Node, _tp_path: String) -> void:
	_dbg.trace("`_teleported` callback on server.")

	# Snap synchronously: child _ready re-runs (triggered by the recursive
	# request_ready in _reparent_player) fire AFTER this lambda returns but
	# BEFORE any deferred call. Camera2D.reset_smoothing in particular reads
	# owner.global_position; if the snap is deferred, smoothing baselines on
	# the (99999, 99999) flush position from _flush_player_position.
	var snap_pos: Variant = Vector3.ZERO if owner is Node3D else Vector2.ZERO
	if scene:
		var tp_node: Node = scene.get_node_or_null(_tp_path)
		if tp_node:
			snap_pos = tp_node.get("global_position")
	_dbg.debug("Teleport server-side complete. Snapping to %s" % [str(snap_pos)])
	owner.set("global_position", snap_pos)

	# Defer only the client notification — the original assert wanted to
	# guarantee the player is fully in tree, which is now true synchronously.
	var notify_client := func() -> void:
		assert(is_inside_tree(), "TPComponent: `_teleported` was called when `is_inside_tree = false`.")
		var authority := owner.get_multiplayer_authority()
		_rpc_teleport_committed.rpc_id(authority, snap_pos)

	notify_client.call_deferred()


# Relays a push acknowledgment from the server back to the client's SaveComponent.
# Safe to send through TPComponent (unlike SaveComponent which has visibility
# restrictions that block server -> client RPCs).
@rpc("any_peer", "call_remote", "reliable")
func _rpc_push_ack() -> void:
	var save: SaveComponent = owner.get_node_or_null("%SaveComponent")
	if save:
		save.push_acknowledged.emit()


@rpc("any_peer", "call_local", "reliable")
func _rpc_teleport_committed(snap_pos: Variant) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1:
		_dbg.warn("_rpc_teleport_committed received from non-server peer %d", [sender])
		return
	var peer_id := multiplayer.get_unique_id()

	_recover_tp_span()

	_dbg.info("Teleport committed. Snapping local player to %s" % [str(snap_pos)])
	_step("committed", {"snap_pos": str(snap_pos)})
	_teleport_committed.emit()
	_tp_mutex.unlock()
	owner.set("global_position", snap_pos)

	# Unlock the player now that we've arrived and snapped.
	owner.set_physics_process(true)
	owner.set_process_input(true)

	var tp_layer := get_tp_layer()
	if tp_layer:
		var phase := _tp_span.phase("transition_in") if _tp_span else null
		await tp_layer.teleport_in()
		if phase: phase.done()

	var bucket := _get_bucket()
	var promise: TeleportPromise = bucket.pending.get(peer_id) if bucket else null
	if promise:
		_step("promise_resolved")
		promise.completed.emit()
		bucket.pending.erase(peer_id)
	
	_end_tp_span()


## Adds [member owner] to the active scene in [param scene_mgr].
func spawn(scene_mgr: MultiplayerSceneManager) -> void:
	_dbg.trace("spawn called.")
	ensure_current_scene_path()

	if current_scene_path.is_empty():
		_dbg.error("Does not have a scene to tp into.", func(m): push_error(m))
		return

	var scene: MultiplayerScene = scene_mgr.active_scenes.get(current_scene_name)
	if scene:
		_dbg.info("Spawning player into scene %s", [current_scene_name])
		scene.synchronizer.track_node(owner)
		scene.level.add_child(owner)
		owner.owner = scene.level
