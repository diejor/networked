class_name TPComponent
extends NetwComponent

const AsyncMutex := preload("res://addons/networked/utils/async_mutex.gd")

const AreaReparentGuard := preload("res://addons/networked/utils/area_reparent_guard.gd")

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

## Emitted (client) when [method teleport] actually starts a transfer (not an
## ignored request). [param corr] is an opaque [NetwCorrelation] the
## debugger's [TeleportProbe] uses to link this operation to its server-side
## counterpart. Production code never reads it.
signal teleport_initiated(target_tp: SceneNodePath, corr: NetwCorrelation)

## Emitted (client) when waiting to start a teleport, or waiting on the save
## acknowledgment, takes long enough to indicate a genuine hang rather than
## routine latency. [param reason] is [code]&"mutex_wait"[/code] or
## [code]&"save_ack_timeout"[/code].
signal stalled(reason: StringName)

## Emitted after the local player snaps to the destination and its processing
## resumes, at the start of the transition-in reveal.
##
## A teleport is a position discontinuity, so a view that smooths or interpolates
## its position must drop its history here or it pans across the inter-scene gap
## as the reveal fades in. The player is already processing again by this point,
## so the reset lands during the reveal instead of waiting for it to finish.
## Reset whatever your camera uses: [method Camera2D.reset_smoothing], a
## [Camera3D] rig's own interpolation, or [method Node.reset_physics_interpolation].
## [codeblock]
## func _ready() -> void:
##     %TPComponent.teleport_committed.connect(reset_smoothing)
## [/codeblock]
signal teleport_committed

## Emitted (client) once the full local commit sequence finishes: transition-in
## animation done, physics restored, and the promise resolved.
signal teleport_finished

## Emitted on either side when a step of the active teleport operation fails.
## Production code has already logged the error. A listener only needs
## [param reason] and [param data] to attribute the failure.
signal teleport_failed(reason: StringName, data: Dictionary)

## Emitted (server) when a teleport request RPC is accepted from
## [param sender_id]. [param corr] mirrors the correlation the client attached
## to [signal teleport_initiated].
signal teleport_request_received(
		sender_id: int,
		from_scene_name: String,
		to_scene_path: String,
		corr: NetwCorrelation,
)

## Emitted (server) when the request handler finishes reparenting the player
## and flushing its physics guard.
signal teleport_request_completed

## Fallback scene when [member current_scene_path] is empty on tree entry.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:MultiplayerSpawner")
var starting_scene_path: SceneNodePath

## The scene the player currently resides in. Replicates on change.
@export var current_scene_path: String = "":
	get:
		return ResourceUID.ensure_path(current_scene_path)
	set(value):
		current_scene_path = value

## The root node name of [member current_scene_path]'s scene.
var current_scene_name: String:
	get:
		return _resolve_scene_name(current_scene_path)

## Seconds after a teleport commits during which [method is_settling]
## returns [code]true[/code]. Destination-area [code]body_entered[/code]
## handlers should short-circuit while settling to avoid ping-pong
## when the snap position overlaps another teleporter.
@export var settle_seconds: float = 0.5

## Minimum mutex wait before [signal stalled] fires with
## [code]&"mutex_wait"[/code]. A second teleport request queued behind a
## normal-length first one should not trip this.
const _MUTEX_STALL_MSEC: int = 2000

var _tp_mutex := AsyncMutex.new()
var _tp_guard: AreaReparentGuard # Holds owner's physics state during a TP.
var _settle_until_msec: int = 0
var _dbg: NetwHandle = Netw.dbg.handle(self)


## Per-peer storage bucket for [TPComponent].
## Bridges the old client instance (which stores the promise) to the new instance
## (which resolves it) across the delete+respawn cycle caused by server reparenting.
class Bucket extends RefCounted:
	var pending: Dictionary[int, TeleportPromise] = { }


func _get_bucket() -> Bucket:
	return get_bucket(Bucket) as Bucket


class TeleportPromise extends RefCounted:
	## Returned by [method TPComponent.teleport] to observe the completion of a teleport.
	##
	## Survives the client node's lifetime - safe to await even when the client player
	## is destroyed and respawned during the teleport handshake.
	signal completed
	var is_completed := false


func _init() -> void:
	## TODO: move name conventions to NetwComponent
	name = "TPComponent"
	unique_name_in_owner = true
	# _do_teleport pauses owner via PROCESS_MODE_DISABLED. Exempt
	# this node so the commit RPC handler and span ticks survive
	# the pause window.
	process_mode = Node.PROCESS_MODE_ALWAYS


func _notification(what: int) -> void:
	if what != NOTIFICATION_PARENTED or Engine.is_editor_hint():
		return

	var entity := NetwEntity.resolve(self)
	if not entity or not entity.owner:
		return

	# current_scene_path is server-owned: a forged client value is a teleport
	# exploit, so it rides no lane (authority-written, no sync axis) and reaches
	# clients only through the spawn frame. It persists into the entity's row when
	# its archetype declares Netw.configure_persistence.
	Netw.configure_property(self, &"current_scene_path").on_spawn().persisted()


func _ready() -> void:
	pass


# Logs the failure and emits it as a domain signal. No span/manifest touched
# here - the debugger's TeleportProbe (if listening) owns turning this into a
# span failure.
func _fail(
		reason: StringName,
		msg: String,
		args: Array = [],
		data: Dictionary = { },
) -> void:
	_dbg.error(msg, args, func(m): push_error(m))
	teleport_failed.emit(reason, data)


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
##
## If a teleport is already active or still settling, the request is ignored
## and the promise resolves on the next frame. A camera or interpolated view
## resets its smoothing on [signal teleport_committed] so the reveal shows the
## destination instead of panning from the origin.
func teleport(target_tp: SceneNodePath) -> TeleportPromise:
	var promise := TeleportPromise.new()
	if _tp_mutex.is_locked() or is_settling():
		_complete_ignored_promise.call_deferred(promise)
		return promise
	var corr := Netw.dbg.correlate(self)
	teleport_initiated.emit(target_tp, corr)
	_dbg.info("Initiating teleport to %s" % [target_tp.scene_path])
	_do_teleport(target_tp, promise, corr)
	return promise


# Resolves an ignored teleport after callers can connect to the signal.
func _complete_ignored_promise(promise: TeleportPromise) -> void:
	promise.is_completed = true
	promise.completed.emit()


func _do_teleport(
		target_tp: SceneNodePath,
		promise: TeleportPromise,
		corr: NetwCorrelation,
) -> void:
	var wait_start_msec := Time.get_ticks_msec()
	await _tp_mutex.lock()
	if Time.get_ticks_msec() - wait_start_msec > _MUTEX_STALL_MSEC:
		stalled.emit(&"mutex_wait")

	var peer_id := multiplayer.get_unique_id()
	var bucket := _get_bucket()
	if bucket:
		bucket.pending[peer_id] = promise

	var from_scene := current_scene_name
	current_scene_path = target_tp.scene_path

	# Disable processing and mask the body off the PhysicsServer for the
	# transition-out and reparent. Suppresses phantom Area2D/3D enter/exit
	# signals during reparent (godot#14578) and freezes input/physics/animations
	# until the destination snaps in _rpc_teleport_committed, which resumes
	# processing for the reveal and keeps only collision masked until it ends.
	# The TPLayer is a sibling CanvasLayer, so its AnimationPlayer keeps ticking.
	_tp_guard = AreaReparentGuard.new(owner)

	var tp_layer := get_tp_layer()
	if tp_layer:
		await tp_layer._teleport_out()

	_request_teleport.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		owner.name,
		from_scene,
		target_tp.scene_path,
		target_tp.node_path,
		corr.to_dict(),
	)


# Internal RPC called by the client to request a teleport from the server.
@rpc("any_peer", "call_local", "reliable")
func _request_teleport(
		username: String,
		from_scene_name: String,
		to_scene_path: String,
		tp_path: String,
		corr_dict: Dictionary,
) -> void:
	if not multiplayer.is_server():
		_dbg.warn("_request_teleport received on non-server peer %d", [multiplayer.get_unique_id()])
		return
	var sender_id := multiplayer.get_remote_sender_id()
	# Reject a peer moving an entity it does not own (RPCs can target any node).
	if sender_id != owner.get_multiplayer_authority():
		_dbg.warn(
			"_request_teleport rejected: peer %d is not authority %d for '%s'.",
			[sender_id, owner.get_multiplayer_authority(), owner.name],
		)
		return
	teleport_request_received.emit(
		sender_id,
		from_scene_name,
		to_scene_path,
		NetwCorrelation.from_dict(corr_dict),
	)
	_dbg.info("Server received teleport request from %s to %s" % [username, to_scene_path])

	var scenes := get_scenes()
	if not scenes:
		_fail(&"no_scene_manager", "Cannot teleport, scene interface not found.")
		return

	var player := owner
	var api := Netw.of(self)
	var from_scene := NetwEntity.of(player).scene
	if not from_scene.is_declared:
		from_scene = api.scene_handle(api.scene_find(from_scene_name))
	if not from_scene or not from_scene.is_declared:
		_fail(
			&"source_scene_not_found",
			"Source scene '%s' not found.",
			[from_scene_name],
			{ "scene": from_scene_name },
		)
		return

	if not is_instance_valid(player) \
			or not from_scene.level.is_ancestor_of(player):
		_fail(
			&"player_not_found",
			"Player '%s' not found in source scene.",
			[username],
		)
		return

	var tp_component: TPComponent = player.get_node("%TPComponent")
	tp_component.current_scene_path = to_scene_path

	var to_scene_node := await _activate_destination(to_scene_path)
	if not to_scene_node:
		return

	# On listen-server the initiator already constructed a guard in
	# _do_teleport on this same TPComponent; reuse it. On a dedicated
	# server the server-side body is independent and needs its own.
	var server_guard := tp_component._tp_guard
	var owns_guard := false
	if not server_guard:
		server_guard = AreaReparentGuard.new(player)
		tp_component._tp_guard = server_guard
		owns_guard = true
	# Flush physics state so the source area evicts the body from its
	# body_map before reparent. Without this, godot#14578 fires a stale
	# body_entered from cache when tree_entered re-fires in the new
	# scene. Two physics_frames is what KoBeWi confirmed works.
	await server_guard.flush()
	_reparent_player(player, from_scene, to_scene_node, tp_path)
	await server_guard.flush()
	if owns_guard:
		server_guard.release()
		tp_component._tp_guard = null
	teleport_request_completed.emit()


func _activate_destination(to_scene_path: String) -> NetwSceneHandle:
	var scenes := get_scenes()
	var api := Netw.of(self)
	var to_scene_name := _resolve_scene_name(to_scene_path)
	await scenes.activate_scene(StringName(to_scene_name))
	var to_scene := api.scene_handle(api.scene_find(StringName(to_scene_name)))
	if not to_scene or not to_scene.is_declared:
		_fail(
			&"dest_scene_activation_failed",
			"Destination scene '%s' could not be activated.",
			[to_scene_name],
			{ "scene": to_scene_name },
		)
		return null
	return to_scene


func _reparent_player(
		player: Node,
		_from_scene: NetwSceneHandle,
		to_scene: NetwSceneHandle,
		tp_path: String,
) -> void:
	var username := player.name
	var to_scene_name := to_scene.level.name
	var tp_component: TPComponent = player.get_node("%TPComponent")
	var entity := NetwEntity.of(player)

	_dbg.info("Reparenting player %s to scene %s" % [username, to_scene_name])

	if not entity:
		_dbg.error(
			"Cannot reparent player %s. NetwEntity is missing.",
			[username],
			func(m): push_error(m),
		)
		return

	var opts := NetwReparentOpts.new()
	opts.reason = &"teleport"
	opts.target_global_position = tp_component._resolve_snap_pos(to_scene.level, tp_path)
	entity.reparent_to(to_scene.level, opts)
	tp_component._teleported(to_scene.level, tp_path)


# Server-side callback invoked after the entity safely enters the destination scene.
# Flushes save state and forwards the snap coordinates to the client. The server
# owner is already positioned by reparent_to (before reparented fired), so no
# server-side re-snap or smoothing reset happens here.
func _teleported(scene: Node, tp_path: String) -> void:
	_dbg.trace("`_teleported` callback on server.")

	var snap_pos := _resolve_snap_pos(scene, tp_path)
	_dbg.debug("Teleport server-side complete. Snapped to %s" % [str(snap_pos)])
	var entity := NetwEntity.of(owner)
	var engine := entity.persistence if entity else null
	if engine:
		engine.flush()

	# Defer only the client notification - the assert guarantees the player is
	# fully in tree, which is true synchronously after reparent_to.
	var notify_client := func() -> void:
		assert(is_inside_tree(), "TPComponent: `_teleported` was called when `is_inside_tree = false`.")
		var authority := owner.get_multiplayer_authority()
		_rpc_teleport_committed.rpc_id(authority, snap_pos)

	notify_client.call_deferred()


# Resolves the destination world position from the target node at tp_path.
func _resolve_snap_pos(scene: Node, tp_path: String) -> Variant:
	var snap_pos: Variant = Vector3.ZERO if owner is Node3D else Vector2.ZERO
	if scene:
		var tp_node: Node = scene.get_node_or_null(tp_path)
		if tp_node:
			snap_pos = tp_node.get("global_position")
	return snap_pos


@rpc("any_peer", "call_local", "reliable")
func _rpc_teleport_committed(snap_pos: Variant) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1:
		_dbg.warn("_rpc_teleport_committed received from non-server peer %d", [sender])
		return
	var peer_id := multiplayer.get_unique_id()

	_dbg.info("Teleport committed. Snapping local player to %s" % [str(snap_pos)])
	owner.set("global_position", snap_pos)
	# Lift the processing freeze at the destination pose but keep collision
	# suppressed, so the player and its camera are live through the reveal while
	# the body still cannot trip the destination teleporter. The mutex and the
	# settle window remain the double-teleport guards. Emit after resuming so a
	# view that resets on this signal is already processing when it re-baselines.
	if _tp_guard:
		_tp_guard.resume_processing()
	teleport_committed.emit()

	var tp_layer := get_tp_layer()
	if tp_layer:
		await tp_layer._teleport_in()

	# Open the settle window before restoring collision so the first
	# body_entered the destination area fires inside the window.
	_settle_until_msec = Time.get_ticks_msec() + int(settle_seconds * 1000.0)

	# Restore collision only after the reveal completes, so the arrival cannot
	# trip an overlapping teleporter until the settle window covers it.
	if _tp_guard:
		_tp_guard.release()
		_tp_guard = null

	var bucket := _get_bucket()
	var promise: TeleportPromise = bucket.pending.get(peer_id) if bucket else null
	if promise:
		promise.is_completed = true
		promise.completed.emit()
		bucket.pending.erase(peer_id)
	else:
		_dbg.warn("Teleport commit had no pending promise for peer %d", [peer_id])

	# Unlock the mutex last so a second teleport cannot start mid-
	# reveal and race against the in-flight commit.
	_tp_mutex.unlock()
	teleport_finished.emit()


## [code]true[/code] for [member settle_seconds] after the last
## teleport commit. Use in destination-area [code]body_entered[/code]
## handlers to avoid ping-pong when the snap position lands on top of
## another teleporter:
## [codeblock]
##     func _on_body_entered(body: Node) -> void:
##         if not is_inside_tree() or not body.is_inside_tree():
##             return
##         var tp := body.get_node_or_null("%TPComponent") as TPComponent
##         if tp and tp.is_settling():
##             return
##         tp.teleport(target)
## [/codeblock]
func is_settling() -> bool:
	return Time.get_ticks_msec() < _settle_until_msec


## Adds [member owner] to the active scene named by this component in
## [param scenes].
func spawn(scenes: SceneCore) -> void:
	_dbg.trace("spawn called.")
	ensure_current_scene_path()

	if current_scene_path.is_empty():
		_dbg.error("Does not have a scene to tp into.", func(m): push_error(m))
		return

	var scene := scenes.scene(current_scene_name)
	if scene and scene.is_declared:
		_dbg.info("Spawning player into scene %s", [current_scene_name])
		scene.add_player(NetwEntity.of(owner))
