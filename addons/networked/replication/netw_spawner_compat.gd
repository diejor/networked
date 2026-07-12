## Consumes [MultiplayerSpawner] registrations so an unmodified stock project
## replicates through the Networked spawn pipeline instead of the native
## [code]SceneReplicationInterface[/code], gaining route identity, liveness gating, and the
## P3 RPC interception without knowing Networked exists.
##
## The native replicator is sealed (constructed inside [SceneMultiplayer], never
## exposed), so there is nothing to subclass. This adapter instead intercepts
## the [method MultiplayerAPI.object_configuration_add] a spawner already emits
## and translates it into the same armed-record pipeline the
## [method Netw.replicate] verb uses, addressed by the [constant
## NetwSpawnBook.Recipe.SPAWNER] recipe. A spawner registration is therefore
## consumed, never forwarded to [member NetwMultiplayer.inner], so the native
## replicator never learns the node exists and a double spawn is unrepresentable.
## [codeblock]
## authority                                receiver
## spawner.spawn(data) / auto-spawn
##   object_configuration_add(node, spawner)
##     consume -> arm SPAWNER record
##       SPAWN frame  ─────────────────────▶ resolve spawner anchor
##                                            instantiate via scene index or fn
##                                            spawner.spawned(node)
## [/codeblock]
## The custom [method MultiplayerSpawner.spawn] argument lives in the spawner's
## private tracking and is not script-readable, so [method wrap_spawner]
## replaces [member MultiplayerSpawner.spawn_function] with a recorder that
## captures the argument as the node is built. Receivers invoke the original
## callable held here, so the wrap records only on the authority.
##
## [br][br][b]Conformance and extension[/b]
## [br]Consumption is conformance-first. Scene-list auto-spawn, custom
## argument recovery, the receiver-side half of
## [member MultiplayerSpawner.spawn_limit], spawner signals on receivers, and
## late-join replay through the [NetwSpawnBook] spawned book all match the
## native rows. The pipeline then exceeds the native contract in one place:
## a consumed node's route survives a cross-scene reparent, a state the
## native spawner cannot represent because it untracks on any tree exit and
## re-derives everything on re-entry. See the existence-versus-visibility
## contract on [NetwSpawnPipeline]. Because the consumed
## [NetwSpawnBook.SpawnRecord] outlives moves, [method reanchor_record]
## re-derives its reconstruction recipe from the spawner watching the
## destination parent at each reparent edge, the same derivation native
## performs on re-entry, without the despawn and without losing the captured
## custom argument.
class_name NetwSpawnerCompat
extends RefCounted

# The owning NetwMultiplayer. A weakref because the owner holds this adapter
# strongly through NetwReplicationInterface and both are reference counted.
var _api_ref: WeakRef

# node instance id -> captured custom spawn data, written by the spawn_function
# wrap and consumed by the next registration for that node.
var _custom_args: Dictionary[int, Variant] = { }

# spawner instance id -> its original spawn_function, so receivers instantiate
# through the unwrapped callable and a re-wrap is idempotent.
var _originals: Dictionary[int, Callable] = { }

# spawner instance id -> WeakRef(spawner): every spawner discovered in this
# session's branch. The engine never announces a spawner as a unit (only its
# tracked nodes reach object_configuration_add), so the adapter builds the
# registration roster itself and reanchor_record consults it as the reverse
# index instead of scanning the tree.
var _spawners: Dictionary[int, WeakRef] = { }

# route -> WeakRef(spawner), so a DESPAWN frame carrying only the route can still
# emit despawned on the spawner that produced the node.
var _recv_spawners: Dictionary[int, WeakRef] = { }

# Receiver: a custom spawn whose spawn function was never wrapped, so its
# argument could not be captured on the authority.
var _drops_uncaptured_custom: int = 0


func _init(api: NetwMultiplayer) -> void:
	_api_ref = weakref(api) if api else null
	var mt := api.tree if api else null
	if mt:
		mt.session_entered.connect(_on_session_entered)
		mt.session_ended.connect(_on_session_ended)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


func _pipeline() -> NetwSpawnPipeline:
	var api := _api()
	return api.replication._spawn_pipeline if api else null


## Consumes a spawner registration for [param node]. Returns [constant OK] so
## [method NetwMultiplayer._object_configuration_add] treats it as handled and
## never forwards it to [member NetwMultiplayer.inner].
func consume(node: Node, spawner: MultiplayerSpawner) -> Error:
	if not is_instance_valid(node) or not is_instance_valid(spawner):
		return ERR_INVALID_PARAMETER
	var pipeline := _pipeline()
	if not pipeline:
		return ERR_UNCONFIGURED
	# Every registration event names its spawner, so a spawner the discovery
	# hooks missed (or whose spawn_function was assigned only after discovery)
	# still enters the roster and gets its wrap here.
	register_spawner(spawner)
	# G1: a verb-spawned node placed under a watched spawn path triggers a
	# spawner registration for a node already in our books. Ignore it: no second
	# identity, no second frame, no spawner signals.
	if pipeline._is_booked(node):
		return OK

	var nid := node.get_instance_id()
	var scene_index := -1
	var data: Variant = null
	if _custom_args.has(nid):
		data = _custom_args[nid]
		_custom_args.erase(nid)
	else:
		scene_index = _scene_index_for(spawner, node)
		if scene_index < 0:
			# Not a scene-list child and no captured custom argument. The most
			# likely cause is a custom spawn whose spawn function was not wrapped
			# before its first spawn. Count it and drop.
			_drops_uncaptured_custom += 1
			Netw.dbg.warn(
				"NetwSpawnerCompat: '%s' has no matching spawnable scene and no "
				+ "captured spawn argument, dropping the consumed spawn",
				[node.name],
			)
			return OK

	pipeline.arm_consumed_spawn(node, spawner, scene_index, data)
	return OK


## Consumes a spawner deregistration. The despawn edge is already driven by the
## node's own [signal Node.tree_exiting], so this only prevents the native
## replicator from seeing the removal.
func consume_remove(_node: Node, _spawner: MultiplayerSpawner) -> Error:
	return OK


## Registers [param spawner] in the session roster and attempts
## [method wrap_spawner]. Idempotent, and called from every edge that can
## discover a spawner: the session-mount sweep, the tree watcher, and each
## registration [method consume] receives. The roster is what
## [method reanchor_record] searches, so a spawner belongs in it even while its
## [member MultiplayerSpawner.spawn_function] is not yet assigned and the wrap
## must wait.
func register_spawner(spawner: MultiplayerSpawner) -> void:
	if not is_instance_valid(spawner):
		return
	_spawners[spawner.get_instance_id()] = weakref(spawner)
	wrap_spawner(spawner)


# TODO: delete the wrap machinery (_wrapped_spawn, _custom_args, _originals)
# once MultiplayerSpawner.get_spawn_argument is script-bound upstream. The
# argument already sits in the spawner's tracking when consume() runs, and the
# native SceneReplicationInterface reads it there through that accessor, so
# consume() could do the same instead of pre-wrapping every spawn function.
## Replaces [param spawner]'s [member MultiplayerSpawner.spawn_function] with a
## recorder that captures the custom spawn argument, keeping the original for
## receiver-side reconstruction. Idempotent.
func wrap_spawner(spawner: MultiplayerSpawner) -> void:
	if not is_instance_valid(spawner):
		return
	var current := spawner.spawn_function
	if not current.is_valid():
		return
	if current.get_object() == self and current.get_method() == &"_wrapped_spawn":
		return
	var sid := spawner.get_instance_id()
	_originals[sid] = current
	spawner.spawn_function = Callable(self, &"_wrapped_spawn").bind(current, spawner)


# The spawn_function stand-in: builds through the original callable, then records
# the argument keyed by the freshly built node so consume() can read it.
func _wrapped_spawn(
		data: Variant,
		original: Callable,
		_spawner: MultiplayerSpawner,
) -> Node:
	var node := original.call(data) as Node
	if is_instance_valid(node):
		_custom_args[node.get_instance_id()] = _duplicate_arg(data)
	return node


# Deep-copies container arguments to match MultiplayerSpawner._track's own
# duplicate(true) semantics. Primitives pass through unchanged.
func _duplicate_arg(data: Variant) -> Variant:
	if data is Array or data is Dictionary:
		return data.duplicate(true)
	return data


## Reconstructs a node on a receiver, either by instantiating the spawner's
## scene at [param scene_index] or by invoking its original spawn function with
## [param data]. Returns [code]null[/code] when the recipe cannot be honored.
func instantiate(
		spawner: MultiplayerSpawner,
		scene_index: int,
		data: Variant,
) -> Node:
	if not is_instance_valid(spawner):
		return null
	if spawner.spawn_limit > 0 \
			and _recv_count_for(spawner) >= spawner.spawn_limit:
		Netw.dbg.warn(
			"NetwSpawnerCompat: spawn limit reached for '%s'",
			[spawner.name],
		)
		return null
	if scene_index >= 0:
		if scene_index >= spawner.get_spawnable_scene_count():
			return null
		var packed := load(spawner.get_spawnable_scene(scene_index)) as PackedScene
		return packed.instantiate() if packed else null
	var original: Callable = _originals.get(
			spawner.get_instance_id(), spawner.spawn_function
	)
	if not original.is_valid():
		return null
	return original.call(data) as Node


## Re-anchors a [constant NetwSpawnBook.Recipe.SPAWNER] record onto the
## [MultiplayerSpawner] watching [param node]'s current parent.
##
## A record's reconstruction recipe is captured at consumption time, but a
## route-stable reparent can move the node into a scene whose observers were
## never admitted to the origin scene, leaving them a spawner anchor they can
## never resolve. Re-anchoring at the reparent edge keeps the recipe
## reconstructible from the destination, mirroring how the native
## despawn-plus-respawn re-derived it through the destination spawner.
## [codeblock]
## consume under Level1        reparent into Level2
##   recipe -> Level1 spawner    recipe -> Level2 spawner
##                               scene_index recomputed for its scene list
## [/codeblock]
## The candidates are the spawners this session registered through
## [method register_spawner], resolved live against their
## [member MultiplayerSpawner.spawn_path] because a path assignment or a move
## of either endpoint has no observable edge. A destination without a matching
## spawner keeps the old anchor, the same nodes-outside-any-spawn-path posture
## native takes.
## [br][br][b]Server Only.[/b]
func reanchor_record(record: NetwSpawnBook.SpawnRecord, node: Node) -> void:
	if record.recipe != NetwSpawnBook.Recipe.SPAWNER:
		return
	if not is_instance_valid(node) or not node.is_inside_tree():
		return
	var parent := node.get_parent()
	var current := record.spawner()
	if current and current.is_inside_tree() \
			and current.get_node_or_null(current.spawn_path) == parent:
		return
	var mt := _api().tree if _api() else null
	if not is_instance_valid(mt):
		return
	for sid: int in _spawners.keys():
		var spawner := _spawners[sid].get_ref() as MultiplayerSpawner
		if not is_instance_valid(spawner):
			_spawners.erase(sid)
			continue
		if not spawner.is_inside_tree() or not mt.is_ancestor_of(spawner):
			continue
		if spawner.get_node_or_null(spawner.spawn_path) != parent:
			continue
		if record.scene_index >= 0:
			var index := _scene_index_for(spawner, node)
			if index < 0:
				continue
			record.scene_index = index
		elif not spawner.spawn_function.is_valid():
			continue
		record.spawner_ref = weakref(spawner)
		return
	Netw.dbg.trace(
		"NetwSpawnerCompat: route %d reparented under '%s' with no matching "
		+ "spawner, keeping its origin anchor",
		[record.route, parent.name],
	)


## Records that [param route] was produced by [param spawner] on this receiver,
## so a later [constant NetwFrameEnvelope.Channel.DESPAWN] can emit
## [signal MultiplayerSpawner.despawned] on the right spawner.
func note_recv(route: int, spawner: MultiplayerSpawner) -> void:
	if is_instance_valid(spawner):
		_recv_spawners[route] = weakref(spawner)


## Emits [signal MultiplayerSpawner.spawned] for [param node], matching the
## native receive path.
func emit_spawned(spawner: MultiplayerSpawner, node: Node) -> void:
	if is_instance_valid(spawner) and is_instance_valid(node):
		spawner.emit_signal(&"spawned", node)


## Emits [signal MultiplayerSpawner.despawned] for [param node] on the spawner
## that produced [param route], if any. A no-op for non-spawner routes.
func emit_despawned(route: int, node: Node) -> void:
	var ref: WeakRef = _recv_spawners.get(route)
	_recv_spawners.erase(route)
	var spawner := ref.get_ref() as MultiplayerSpawner if ref else null
	if is_instance_valid(spawner) and is_instance_valid(node):
		spawner.emit_signal(&"despawned", node)


# Counts routes this peer materialized through [param spawner], the
# receiver-side half of the native spawn_limit check.
func _recv_count_for(spawner: MultiplayerSpawner) -> int:
	var count := 0
	for route: int in _recv_spawners:
		if _recv_spawners[route].get_ref() == spawner:
			count += 1
	return count


# Reimplements MultiplayerSpawner.find_spawnable_scene_index_from_path
# script-side, resolving both sides through ResourceUID so a uid and a res path
# for the same scene compare equal.
func _scene_index_for(spawner: MultiplayerSpawner, node: Node) -> int:
	var target := ResourceUID.ensure_path(node.scene_file_path)
	if target.is_empty():
		return -1
	for i in spawner.get_spawnable_scene_count():
		if ResourceUID.ensure_path(spawner.get_spawnable_scene(i)) == target:
			return i
	return -1


# Registers every spawner already in the tree branch at session mount, then
# watches node_added so spawners inside later-spawned levels register before
# their first custom spawn can run its argument through an unwrapped function.
# TODO: delete this sweep and the node_added watcher once MultiplayerSpawner
# announces itself through object_configuration_add at tree entry the way
# MultiplayerSynchronizer does; register_spawner inside consume() would then
# be the sole discovery edge.
func _on_session_entered() -> void:
	var mt := _api().tree if _api() else null
	if not is_instance_valid(mt):
		return
	for spawner: Node in mt.find_children("*", "MultiplayerSpawner", true, false):
		register_spawner(spawner as MultiplayerSpawner)
	var scene_tree := mt.get_tree()
	if scene_tree and not scene_tree.node_added.is_connected(_on_node_added):
		scene_tree.node_added.connect(_on_node_added)


# The node_added filter: only spawners inside this session's branch register,
# so multi-tree hosts never cross-wrap another session's spawners.
func _on_node_added(node: Node) -> void:
	if not (node is MultiplayerSpawner):
		return
	var mt := _api().tree if _api() else null
	if is_instance_valid(mt) and mt.is_ancestor_of(node):
		register_spawner(node as MultiplayerSpawner)


func _on_session_ended() -> void:
	var mt := _api().tree if _api() else null
	var scene_tree := mt.get_tree() if is_instance_valid(mt) else null
	if scene_tree and scene_tree.node_added.is_connected(_on_node_added):
		scene_tree.node_added.disconnect(_on_node_added)
	_clear_session_state.call_deferred()


func _clear_session_state() -> void:
	# Restore the original spawn functions so a re-hosted session's fresh
	# adapter wraps the real callable instead of chaining through this one.
	for sid: int in _originals:
		var spawner := instance_from_id(sid) as MultiplayerSpawner
		if is_instance_valid(spawner):
			spawner.spawn_function = _originals[sid]
	_custom_args.clear()
	_originals.clear()
	_recv_spawners.clear()
	_spawners.clear()


## Returns this adapter's contribution to
## [method NetwReplicationInterface.counters].
func counters() -> Dictionary:
	return {
		&"drops_uncaptured_custom": _drops_uncaptured_custom,
	}
