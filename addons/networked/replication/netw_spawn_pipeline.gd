## The spawn half of the replication core owned by [ReplicationCore]:
## the [method Netw.replicate] and [method Netw.spawn] verb tails, the
## end-of-frame flush that snapshots a
## [constant NetwFrameEnvelope.Channel.SPAWN] frame, the implicit
## [constant NetwFrameEnvelope.Channel.DESPAWN] and
## [constant NetwFrameEnvelope.Channel.REPARENT] edges, the frame codecs, and
## the receive pipeline that reconstructs a node while it is still orphaned.
##
## Identity is stamped on every peer before the node's first
## [method Node._enter_tree], so a spawned node observes a valid [NetwEntity]
## and its applied spawn state from its earliest lifecycle hook. Every other
## rule in this pipeline exists to keep that ordering true under packet races,
## interest flapping, and cross-scene moves.
## [codeblock]
## SPAWN frame arrives
##   resolve dependencies   parent route, fn host route, node-ref args
##     any unresolved   ->  park until live, then retry
##   construct              scene.instantiate() or host.fn(args)
##   stamp identity         route, entity_id, peer_id, controller
##   apply spawn state       .on_spawn() values, while orphaned
##   add_child              root PARENTED, _enter_tree, _ready
## [/codeblock]
##
## [b]The spawn contract[/b]
## [br]Receivers hold these invariants for every frame on every peer:
## [br]- Identity and [method Netw.configure_property] spawn state apply while
## the node is still orphaned, before [method Node.add_child] places it.
## [br]- Parents materialize before their children and children despawn before
## their parents, because [NetwSpawnBook] insertion order is the replay and
## sweep order and the despawn cascade walks it child-first through
## [NetwSpawnBook.SpawnRecord] parent routes.
## [br]- A duplicate [constant NetwFrameEnvelope.Channel.SPAWN] and a
## [constant NetwFrameEnvelope.Channel.DESPAWN] for a route this peer never
## materialized are idempotent drops, counted by [method counters], never
## errors.
## [br]- An unmet dependency is a bounded deferral. The frame parks on
## [method LivenessShell.when_live] for [member park_timeout_seconds],
## a [constant NetwFrameEnvelope.Channel.DESPAWN] arriving during the park
## cancels it with net result zero, and an expired park unparks the route so
## no stale flag outlives the wait.
##
## [br][br][b]Existence versus visibility[/b]
## [br]A route names one entity instance for its whole life.
## [constant NetwFrameEnvelope.Channel.SPAWN] and
## [constant NetwFrameEnvelope.Channel.DESPAWN] frames are per-peer visibility
## edges at the boundary of what [InterestCore] admits, not lifecycle
## events of the entity itself.
## [method schedule_visibility_sweep] reconciles the
## [NetwSpawnBook.SpawnRecord] recipient set against the current admissions,
## and [NetwSyncCompat] clamps consumed synchronizer traffic to that same
## book, so a peer is never sent state for a node it was not sent.
##
## [br][br]
## The consequence is that a cross-scene move is a route-stable reparent,
## never a despawn plus respawn. Peers that keep visibility move their
## existing instance, with its local state, in place. Only peers actually
## crossing the visibility boundary see a spawn edge.
## [codeblock]
## peer visibility across a reparent     frame it receives
##   kept origin and destination    ─▶  REPARENT   same instance moves
##   lost, had origin only          ─▶  DESPAWN    instance freed
##   gained, destination only       ─▶  SPAWN      fresh instance, recipe
##                                                 re-anchored on the
##                                                 destination spawner
##   never admitted to either       ─▶  nothing
## [/codeblock]
## The gained row is why [method NetwSpawnerCompat.reanchor_record] runs at
## the reparent edge: a consumed record's reconstruction recipe must stay
## resolvable from the destination for observers that never saw the origin.
##
## [br][br][b]Revival[/b]
## [br]A [constant NetwMultiplayer.EntityState.DEAD] route is tombstoned, yet
## [constant NetwFrameEnvelope.Channel.SPAWN] and
## [constant NetwFrameEnvelope.Channel.DESPAWN] share one reliable ordered
## carrier stream, so a spawn edge arriving after the despawn that killed the
## route is an interest re-admission reviving the entity, never a stale
## packet. Dependencies in [constant NetwMultiplayer.EntityState.DEAD]
## therefore park exactly like [constant NetwMultiplayer.EntityState.UNKNOWN]
## ones, because the reviving [constant NetwFrameEnvelope.Channel.SPAWN]
## travels the same stream and can only be moments behind.
class_name NetwSpawnPipeline
extends RefCounted

# The owning NetwMultiplayer. A weakref because the owner holds this pipeline
# strongly through ReplicationCore and both are reference counted.
var _api_ref: WeakRef

# Spawn ledger for the Netw.replicate / Netw.spawn verbs. See NetwSpawnBook.
var _spawn_book := NetwSpawnBook.new()

# Receiver: SPAWN/DESPAWN frame from a sender other than the server.
var _drops_spawn_bad_sender: int = 0
# Receiver: SPAWN for a route already known on this peer. Idempotent by design.
var _drops_spawn_duplicate: int = 0
# Receiver: SPAWN whose parent, fn host, or recipe could not be resolved.
var _drops_spawn_unresolved: int = 0
# Receiver: DESPAWN for a route this peer never materialized. Idempotent.
var _drops_despawn_unknown: int = 0
# Receiver: SPAWN frames parked on an unmet dependency via when_live.
var _spawn_deferrals: int = 0
# Receiver: parked SPAWN frames cancelled by a DESPAWN before applying.
var _spawn_parked_cancelled: int = 0
# Receiver: parked SPAWN frames whose dependency never bound before the park
# window closed. The frame is dropped and the route unparked, so a later
# DESPAWN or re-admission SPAWN sees clean state instead of a stale park.
var _spawn_park_expired: int = 0

## How long a parked frame waits on [method LivenessShell.when_live]
## for its dependency before expiring and unparking its route.
##
## A bounded deferral must survive an interest flap under real latency, where
## the reviving dependency [constant NetwFrameEnvelope.Channel.SPAWN] can
## trail by several round trips, so this is
## deliberately much wider than the one-second [method LivenessShell.when_live]
## default.
var park_timeout_seconds: float = 5.0

# Routes whose SPAWN frame is parked on a dependency, so a DESPAWN arriving
# during the park cancels the spawn with net result zero.
var _parked_spawn_routes: Dictionary[int, bool] = { }

# Receiver: spawner-consumed SPAWN frames parked because their consumed
# spawner's scene subtree was not present when the frame arrived, keyed by route
# to a { "payload", "deadline" } record. A path-anchored spawner has no route to
# wait on through when_live, and a late-join replay can deliver a player before
# the scene that holds its spawner, so these retry when a scene spawns rather
# than dropping the player permanently.
var _spawner_parked: Dictionary[int, Dictionary] = { }

# True only while this pipeline synchronously places a replicated node, so a
# marked scene's tree_entered hook tells a framework spawn from a native
# change_scene. Read through ReplicationCore.is_applying_remote_frame.
var _applying_remote_frame := false

# Coalesces visibility-change signals into one end-of-frame sweep.
var _visibility_sweep_scheduled := false

# Coalesces spawn-edge sends into one same-frame carrier flush.
var _carrier_flush_scheduled := false

# Host-less spawn constructors keyed by a stable id, registered by the API
# subsystem that owns them. A Recipe.FN_REGISTRY spawn resolves its function
# here instead of from a node anchor, so a manager-less session still spawns.
var _spawn_constructors: Dictionary[StringName, Callable] = { }

# Route -> a suspended action-spawn owner, hidden until its display tick arrives.
var _action_gates: Dictionary[int, _ActionGate] = { }


func _init(api: NetwMultiplayer) -> void:
	_api_ref = weakref(api) if api else null
	if api:
		api._connect_once(api.peer_connected, _on_peer_connected)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# Late-join hydration: a peer that connects after spawns already exist is
# replayed the whole authority spawn book in insertion order, so it receives
# parents before their children (the book is an ordered Dictionary for exactly
# this reason). Mirrors the native SceneReplicationInterface peer-change replay.
func _on_peer_connected(peer_id: int) -> void:
	if not _is_server_authority():
		return
	var api := _api()
	if api:
		api._sink_verdict(_replay_spawn_book(peer_id), 0)


# Re-encodes each issued spawn from its record plus the node's live state at
# send time, so a late joiner sees the current spawn-state values before
# steady-state sync takes over, and records the peer as a recipient so a later
# despawn reaches it. Filtered per record by the same visibility verdict that
# picks initial recipients.
func _replay_spawn_book(peer_id: int) -> Error:
	var api := _api()
	if not api or not api.inner.multiplayer_peer:
		return ERR_UNAVAILABLE
	for route in _spawn_book.ancestry_order():
		var record: NetwSpawnBook.SpawnRecord = _spawn_book.spawned[route]
		var node := record.node()
		if not node or not node.is_inside_tree():
			continue
		if peer_id in record.recipients:
			continue
		if not _spawn_visible_to(peer_id, record, node):
			continue
		var payload := _encode_spawn_frame(record, node)
		if payload.is_empty():
			api._sink_verdict(ERR_INVALID_DATA, route)
			continue
		record.recipients.append(peer_id)
		api._replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.SPAWN,
			payload,
			true,
			0,
			"",
			true,
		)
	_schedule_carrier_flush()
	return OK

#region Interest-driven visibility

## Schedules an end-of-frame visibility sweep over the spawned book, the
## counterpart of the native per-peer spawn visibility update. Fired by
## synchronizer [signal MultiplayerSynchronizer.visibility_changed] relays and
## safe to call redundantly.
## [br][br][b]Server Only.[/b]
func schedule_visibility_sweep() -> void:
	if _visibility_sweep_scheduled:
		return
	if not _is_server_authority():
		return
	_visibility_sweep_scheduled = true
	_run_visibility_sweep.call_deferred()


# Sends SPAWN to peers that gained visibility (ancestry order, parents before
# children) and DESPAWN to peers that lost it (reverse order, children before
# parents), maintaining record.recipients as the per-peer book.
func _run_visibility_sweep() -> void:
	_visibility_sweep_scheduled = false
	var api := _api()
	if not api or not api.inner.multiplayer_peer:
		return
	if not api.is_online:
		return
	var peers := api.inner.get_peers()

	# A reparent refreshes a record's parent and spawner anchors in
	# _send_reparent, which is deferred and can trail this sweep on the same
	# frame. Encoding a gained peer's SPAWN or judging a kept peer's visibility
	# from the stale anchors then either parks that peer's SPAWN on a spawner in
	# the scene it never joined, or despawns the mover from its own just-moved
	# entity. Refresh every in-tree record here so the sweep always reads the
	# live topology.
	for route: int in _spawn_book.spawned.keys():
		var record: NetwSpawnBook.SpawnRecord = _spawn_book.spawned[route]
		var node := record.node()
		if node and node.is_inside_tree():
			_refresh_record_anchors(record, node)

	# Ordered after the refresh, because the refresh is what re-parents a mover's
	# record and so decides where it sits in ancestry.
	var routes := _spawn_book.ancestry_order()
	var rows: Array[Dictionary] = []
	for route: int in routes:
		var record: NetwSpawnBook.SpawnRecord = _spawn_book.spawned[route]
		var node := record.node()
		if not node or not node.is_inside_tree():
			continue
		var local_desired: Dictionary = { }
		var leave: Dictionary = { }
		var entity := NetwEntity.of(node)
		for peer_id in peers:
			var desired := _spawn_locally_desired_to(peer_id, node)
			local_desired[peer_id] = desired
			if entity and peer_id in record.recipients and not desired:
				leave[peer_id] = api._interest._resolve_leave_decision(
					entity,
					peer_id,
				)
		rows.append(
			{
				&"route": route,
				&"parent_route": record.parent_route,
				&"recipients": record.recipients.duplicate(),
				&"local_desired": local_desired,
				&"leave": leave,
			},
		)

	var peer_values := PackedInt32Array(peers)
	api._spawn_reconcile_rows = rows
	api._spawn_reconcile_peers = peer_values
	api._spawn_reconcile_plan = null
	var reconcile_verdict := api._spawn_reconcile()
	var plan := api._spawn_reconcile_plan
	api._spawn_reconcile_rows = []
	api._spawn_reconcile_peers = PackedInt32Array()
	api._spawn_reconcile_plan = null
	if reconcile_verdict != OK or plan == null:
		api._sink_verdict(
			reconcile_verdict if reconcile_verdict != OK \
			else ERR_UNCONFIGURED,
			0,
		)
		api._interest._finish_leave_sweep()
		return
	var spawn_payloads: Dictionary[int, PackedByteArray] = { }
	var despawn_payloads: Dictionary[int, PackedByteArray] = { }
	for operation: Dictionary in plan.operations:
		var route := int(operation[&"route"])
		var peer_id := int(operation[&"peer"])
		var record: NetwSpawnBook.SpawnRecord = _spawn_book.spawned.get(route)
		if not record:
			continue
		var node := record.node()
		if not node or not node.is_inside_tree():
			continue
		match operation[&"action"]:
			&"spawn":
				var payload: PackedByteArray = spawn_payloads.get(
					route,
					PackedByteArray(),
				)
				if payload.is_empty():
					payload = _encode_spawn_frame(record, node)
					spawn_payloads[route] = payload
				if payload.is_empty():
					continue
				record.recipients.append(peer_id)
				api._replication.send_to(
					peer_id,
					0,
					NetwFrameEnvelope.Channel.SPAWN,
					payload,
					true,
					0,
					"",
					true,
				)
			&"retain", &"despawn":
				var entity := NetwEntity.of(node)
				if entity:
					api._interest._commit_leave_decision(
						entity,
						peer_id,
						operation[&"decision"],
						bool(operation[&"forced"]),
					)
				if operation[&"action"] == &"retain":
					continue
				var payload: PackedByteArray = despawn_payloads.get(
					route,
					PackedByteArray(),
				)
				if payload.is_empty():
					var writer := NetwBitBufferWriter.new()
					NetwCodec.put_varint(writer, route)
					payload = writer.to_bytes()
					despawn_payloads[route] = payload
				record.recipients.erase(peer_id)
				api._replication.send_to(
					peer_id,
					0,
					NetwFrameEnvelope.Channel.DESPAWN,
					payload,
					true,
					0,
					"",
					true,
				)
	api._interest._finish_leave_sweep()
	_schedule_carrier_flush()


# The per-peer spawn verdict: the book-derived ancestor clamp, the interest
# admission for entities interest manages, and the native OR-composition over
# the root's authority-held synchronizers.
func _spawn_visible_to(
		peer_id: int,
		record: NetwSpawnBook.SpawnRecord,
		node: Node,
) -> bool:
	if record.parent_route > 0:
		var parent: NetwSpawnBook.SpawnRecord = \
				_spawn_book.spawned.get(record.parent_route)
		if parent and peer_id not in parent.recipients:
			return false
	return _spawn_locally_desired_to(peer_id, node)


# Evaluates the desired ancestor chain without consulting materialized rows.
# Revokes run child-first, while parent recipient rows still describe the old
# materialized state. Using those rows would strand descendants as phantom
# recipients after the parent despawns them through its subtree cascade.
func _spawn_desired_to(
		peer_id: int,
		record: NetwSpawnBook.SpawnRecord,
		node: Node,
) -> bool:
	if record.parent_route > 0:
		var parent: NetwSpawnBook.SpawnRecord = \
				_spawn_book.spawned.get(record.parent_route)
		if parent:
			var parent_node := parent.node()
			if parent_node and not _spawn_desired_to(
				peer_id,
				parent,
				parent_node,
			):
				return false
	return _spawn_locally_desired_to(peer_id, node)


# Computes this record's own admission terms without its ancestor clamp.
func _spawn_locally_desired_to(peer_id: int, node: Node) -> bool:
	var api := _api()
	if not api:
		return false
	var entity := NetwEntity.of(node)
	if entity:
		if api._interest.has_committed_intent(entity):
			return api._interest.wire_admits(peer_id, entity)
		if api.interest_is_filtered(api.rid_of(node)) \
				and not api._interest.wire_admits(peer_id, entity):
			return false
	return api._replication._sync_compat.synchronizer_verdict(peer_id, node)


# Spawn-edge frames are batched, but they must not wait for the next tick's
# aggregation flush: native traffic (sync path confirms, native RPCs) leaves
# immediately at poll and would overtake a buffered SPAWN on the same ordered
# stream. One coalesced flush at the end of the frame's deferred queue keeps
# intra-frame batching while restoring send order.
func _schedule_carrier_flush() -> void:
	if _carrier_flush_scheduled:
		return
	_carrier_flush_scheduled = true
	_run_carrier_flush.call_deferred()


func _run_carrier_flush() -> void:
	_carrier_flush_scheduled = false
	var api := _api()
	if api:
		api._replication.flush_all_buffers()

#endregion

## Replicates the orphan [param node] to every connected peer, reconstructing
## it from its [member Node.scene_file_path]. The api-scoped form of
## [method Netw.replicate].
##
## Identity is stamped synchronously before this method returns, so
## [method Node._enter_tree] and [method Node._ready] observe a valid
## [NetwEntity] on every peer. The [constant NetwFrameEnvelope.Channel.SPAWN]
## frame is snapshotted at end-of-frame of tree entry, so the window between
## this call and [method Node.add_child] is where async hydration belongs.
## [codeblock]
## var player := PlayerScene.instantiate()
## var entity := api._replication.replicate(player, participant)
## await save.hydrate(entity)
## arena.add_child(player)
## [/codeblock]
## [b]Server Only.[/b]
func replicate(node: Node, owner: NetwParticipant = null) -> NetwEntity:
	assert(
		_is_server_authority(),
		"Netw.replicate is server-only; player-proposed spawns are the "
		+ "(future) Netw.request_replicate verb",
	)
	if not is_instance_valid(node):
		Netw.dbg.error("Netw.replicate: node is null or freed")
		return null
	assert(
		not node.is_inside_tree(),
		"Netw.replicate: identity must precede tree entry: call before "
		+ "add_child; pre-placed nodes are the (future) Netw.adopt boundary",
	)

	var existing := NetwEntity.of(node)
	if existing and existing.route > 0:
		if _spawn_book.is_recv(existing.route):
			Netw.dbg.error(
				"Netw.replicate: '%s' was materialized from the server; "
				+ "this peer cannot take authority of a remote-owned instance",
				[node.name],
			)
			return null
		Netw.dbg.warn(
			"Netw.replicate: '%s' is already replicated (route %d)",
			[node.name, existing.route],
		)
		return existing

	assert(
		not node.scene_file_path.is_empty(),
		"Netw.replicate: node has no reconstruction recipe. Instantiate it "
		+ "from a scene or use Netw.spawn with a configured spawn function.",
	)

	var record := NetwSpawnBook.SpawnRecord.new()
	record.recipe = NetwSpawnBook.Recipe.SCENE
	record.scene_path = node.scene_file_path
	return _arm_authoritative_spawn(node, record, owner)


## Constructs a node by running the spawn function [param fn] with
## [param args] locally, replicating the same construction to every peer. The
## api-scoped form of [method Netw.spawn].
##
## [param fn] must be registered with [method Netw.configure_spawn] on a host
## object that exists on every peer, must build from its arguments only, and
## must return an orphan [Node]. The returned node is stamped and armed like
## [method replicate]. The caller places it.
## [br][br][b]Server Only.[/b]
func spawn(fn: Callable, args: Array = [], owner: NetwParticipant = null) -> Node:
	assert(
		_is_server_authority(),
		"Netw.spawn is server-only; player-proposed spawns are the "
		+ "(future) Netw.request_spawn verb",
	)
	var host := fn.get_object() as Node
	assert(
		host != null,
		"Netw.spawn: the spawn function must be a method on a Node host "
		+ "that exists on every peer",
	)
	var script := host.get_script() as Script
	var method := fn.get_method()
	assert(
		NetwScriptModel.get_spawn_config(script, method) != null,
		"Netw.spawn: spawn function '%s' is not registered; call "
		% method
		+ "Netw.configure_spawn in the host's _init()",
	)
	assert(
		NetwScriptModel.validate_argument_count(script, method, args.size()),
		"Netw.spawn: argument count mismatch for spawn function '%s'"
		% method,
	)

	var node := fn.callv(args) as Node
	assert(
		node != null,
		"Netw.spawn: spawn function '%s' did not return a Node. A spawn "
		% method
		+ "function must not be a coroutine and must return an orphan node.",
	)
	assert(
		not node.is_inside_tree(),
		"Netw.spawn: the spawn function must return an orphan node",
	)
	var existing := NetwEntity.of(node)
	assert(
		existing == null or existing.route == 0,
		"Netw.spawn: the spawn function returned an already replicated node",
	)

	var record := NetwSpawnBook.SpawnRecord.new()
	record.recipe = NetwSpawnBook.Recipe.FN
	record.fn_host_ref = weakref(host)
	record.fn_method = method
	record.fn_args = args
	_arm_authoritative_spawn(node, record, owner)
	return node


## Registers [param fn] as a host-less spawn constructor under [param id]. A
## [method spawn_registered] call and its receivers resolve the function from
## [param id] alone, so a session with no host node still reconstructs it.
## [param fn]'s method must also be registered with
## [method Netw.configure_spawn] so its arguments encode.
func register_spawn_constructor(id: StringName, fn: Callable) -> void:
	_spawn_constructors[id] = fn


func _spawn_constructor(id: StringName) -> Callable:
	return _spawn_constructors.get(id, Callable())


## Constructs a node on every peer by running the constructor registered under
## [param id] with [param args], returning the local node for the caller to
## place. Mirrors [method spawn] but resolves the function from the registry
## instead of a host node, so a manager-less session can spawn.
## [br][br][b]Server Only.[/b]
func spawn_registered(
		id: StringName,
		args: Array = [],
		owner: NetwParticipant = null,
) -> Node:
	assert(
		_is_server_authority(),
		"spawn_registered is server-only",
	)
	var fn := _spawn_constructor(id)
	assert(
		fn.is_valid(),
		"spawn_registered: no constructor registered under id '%s'" % id,
	)
	var node := fn.callv(args) as Node
	assert(
		node != null and not node.is_inside_tree(),
		"spawn_registered: constructor '%s' must return an orphan node" % id,
	)
	var record := NetwSpawnBook.SpawnRecord.new()
	record.recipe = NetwSpawnBook.Recipe.FN_REGISTRY
	record.fn_registry_id = id
	record.fn_args = args
	_arm_authoritative_spawn(node, record, owner)
	return node


# Shared verb tail: allocates the route, stamps identity while the node is
# orphaned, and arms the record for the end-of-frame flush on tree entry. A
# consumed spawner node may already be in the tree at arm time (native
# auto-spawn fires on child_entered_tree), so the flush is deferred directly
# instead of waiting for a tree_entered that will not fire again.
func _arm_authoritative_spawn(
		node: Node,
		record: NetwSpawnBook.SpawnRecord,
		owner: NetwParticipant,
) -> NetwEntity:
	var api := _api()
	if not api:
		return null
	var liveness := api._liveness

	var entity := NetwEntity.ensure(node)
	var previous_route := entity.route
	var previous_id := entity.entity_id
	var previous_peer := entity.peer_id
	var previous_controller := entity.controller
	# G2 adoption: a consumed spawner node may already carry a route stamped by
	# its spawn envelope before the binding exists, so read the entity record
	# directly rather than the liveness binding.
	var route := liveness.route_of(entity)
	if route <= 0:
		route = entity.route
	if route <= 0:
		route = liveness.reserve_route()
	entity.route = route
	if entity.entity_id == &"":
		entity.entity_id = StringName("%s@%d" % [_recipe_base(record, node), route])
	if owner:
		entity.peer_id = owner.peer_id
		entity.controller = owner.peer_id
	record.route = route
	record.node_ref = weakref(node)
	record.entity_id = entity.entity_id
	record.peer_id = entity.peer_id
	record.controller = entity.controller
	var declare_verdict := api._spawn_declare(
		api.rid_of(node),
		{
			&"recipe": record.recipe,
			&"route": route,
			&"entity_id": record.entity_id,
			&"peer": record.peer_id,
			&"controller": record.controller,
		},
	)
	if declare_verdict != OK:
		api._sink_verdict(declare_verdict, route)
		entity.route = previous_route
		entity.entity_id = previous_id
		entity.peer_id = previous_peer
		entity.controller = previous_controller
		return null
	_spawn_book.arm(record)
	# Settle authority and seal the record on the orphan, before add_child, so
	# is_multiplayer_authority() is correct in every child's tree entry. The
	# armer holds the session, so hand it over rather than making the record
	# re-discover it once in-tree.
	if entity.stage == NetwEntity.Stage.UNBOUND:
		entity.arm(api)
	if node.is_inside_tree():
		# The node entered the tree before this arm stamped identity, so its
		# first tree entry classified it inert. Drive its live path now.
		entity._go_live_if_armed()
		_flush_armed_spawn.call_deferred(route)
	else:
		api._connect_once(
			node.tree_entered,
			_on_armed_tree_entered.bind(route),
			CONNECT_ONE_SHOT,
		)
	return entity


# Writes a spawn function's arguments using the quantizers and types declared
# for its (script, method) through Netw.configure_spawn. Shared by the FN and
# FN_REGISTRY recipes, which differ only in how they address the function.
func _encode_fn_args(
		w: NetwBitBufferWriter,
		script: Script,
		method: StringName,
		fn_args: Array,
) -> void:
	var api := _api()
	var cfg := NetwScriptModel.get_spawn_config(script, method)
	var encoded_args: Array = api._rpc_core._encode_args(api._liveness, fn_args)
	NetwScriptModel.write_values(
		w,
		encoded_args,
		cfg.quantizers if cfg else [],
		NetwScriptModel.get_method_arg_types(script, method),
	)


# Reads a spawn function's encoded arguments for its (script, method) config.
# Returns null when the function is not registered through Netw.configure_spawn.
func _read_fn_args(r: NetwBitBufferReader, script: Script, method: StringName) -> Variant:
	var cfg := NetwScriptModel.get_spawn_config(script, method)
	if not cfg:
		return null
	return NetwScriptModel.read_values(
		r,
		cfg.quantizers,
		NetwScriptModel.get_method_arg_types(script, method),
	)


# Materializes a spawn function's decoded arguments, resolving each NetwNodeRef
# to its live node. Returns null and parks the spawn when a referenced route is
# not yet live. Shared by the FN and FN_REGISTRY recipes.
func _resolve_spawn_args(
		encoded_args: Array,
		payload: PackedByteArray,
		route: int,
		liveness: LivenessShell,
) -> Variant:
	var api := _api()
	var args: Array = []
	for encoded in encoded_args:
		if encoded is NetwNodeRef:
			var ref: NetwNodeRef = encoded
			if _anchor_parks(liveness.route_state(ref.route)):
				_park_spawn(payload, ref.route, route)
				return null
			var arg_entity := liveness.entity_of(ref.route)
			var arg_node: Node = null
			if arg_entity:
				arg_node = api._replication.resolve_comp_node(
					arg_entity,
					ref.comp,
					ref.path,
				)
			args.append(arg_node)
		else:
			args.append(encoded)
	return args


# The entity_id stem for an auto-assigned id: the scene basename, the spawn
# function name, or the spawner node's scene or name.
func _recipe_base(record: NetwSpawnBook.SpawnRecord, node: Node) -> String:
	match record.recipe:
		NetwSpawnBook.Recipe.SCENE:
			return record.scene_path.get_file().get_basename()
		NetwSpawnBook.Recipe.FN:
			return String(record.fn_method)
		NetwSpawnBook.Recipe.FN_REGISTRY:
			return String(record.fn_registry_id)
		_:
			if not node.scene_file_path.is_empty():
				return node.scene_file_path.get_file().get_basename()
			return String(node.name)


## Stamps identity onto [param root], a node every peer already holds at the
## same tree location, and issues the [constant NetwSpawnBook.Recipe.ADOPT]
## frame that stamps the same identity onto the peers' instances in place.
## Called by [NetwSyncCompat] when a consumed synchronizer's root has no
## [NetwEntity], so a synced node always has a route without reconstructing
## structure the peers already built. The frame flushes synchronously because
## the node is already placed and the caller runs inside the tick pump, where
## a deferred flush could trail the tick's own sync frames by a whole frame.
## [br][br][b]Server Only.[/b]
func adopt_in_place(root: Node) -> NetwEntity:
	if not _is_server_authority() or not root.is_inside_tree():
		return null
	var record := NetwSpawnBook.SpawnRecord.new()
	record.recipe = NetwSpawnBook.Recipe.ADOPT
	var entity := _arm_authoritative_spawn(root, record, null)
	if entity:
		# The node never re-enters the tree, so the tree-entry hook that binds
		# freshly spawned routes cannot fire for it.
		var api := _api()
		if api:
			api._liveness.bind_route(record.route, entity)
		_flush_armed_spawn(record.route)
	return entity


## Arms a spawner-consumed spawn so it replicates through the pipeline instead
## of the native replicator. Called by [NetwSpawnerCompat] from the seam.
func arm_consumed_spawn(
		node: Node,
		spawner: MultiplayerSpawner,
		scene_index: int,
		data: Variant,
) -> NetwEntity:
	var record := NetwSpawnBook.SpawnRecord.new()
	record.recipe = NetwSpawnBook.Recipe.SPAWNER
	record.spawner_ref = weakref(spawner)
	record.scene_index = scene_index
	record.custom_data = data
	return _arm_authoritative_spawn(node, record, null)


# True when [param node] is already an armed or issued spawn on this peer, the
# consumption G1 guard against a second identity for a verb-spawned node.
func _is_booked(node: Node) -> bool:
	for record: NetwSpawnBook.SpawnRecord in _spawn_book.armed.values():
		if record.node() == node:
			return true
	for record: NetwSpawnBook.SpawnRecord in _spawn_book.spawned.values():
		if record.node() == node:
			return true
	return false


# Defers the snapshot to end-of-frame so properties set after add_child in
# the same frame still ride the SPAWN frame.
func _on_armed_tree_entered(route: int) -> void:
	_flush_armed_spawn.call_deferred(route)


func _flush_armed_spawn(route: int) -> void:
	var record := _spawn_book.take_armed(route)
	if not record:
		return
	var node := record.node()
	if not node or not node.is_inside_tree():
		Netw.dbg.trace(
			"NetwSpawnPipeline: armed route %d left the tree before "
			+ "its spawn flushed, dropping the arm",
			[route],
		)
		return

	record.node_name = String(node.name)
	var parent_entity := NetwEntity.of(node.get_parent())
	var api := _api()
	record.parent_route = (
			api._liveness.route_of(parent_entity) if api and parent_entity else 0
	)
	if api:
		api._interest._sync_scene_membership(NetwEntity.of(node))
	_spawn_book.spawned[route] = record
	if not api:
		return
	api._connect_once(
		node.tree_exiting,
		_on_tracked_root_exiting.bind(route),
		CONNECT_ONE_SHOT,
	)

	if not api.inner.multiplayer_peer:
		return
	var payload := _encode_spawn_frame(record, node)
	if payload.is_empty():
		return
	var recipients: Array[int] = []
	for peer_id in api.inner.get_peers():
		if _spawn_visible_to(peer_id, record, node):
			recipients.append(peer_id)
	record.recipients = recipients
	for peer_id in recipients:
		api._replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.SPAWN,
			payload,
			true,
			0,
			"",
			true,
		)
	_schedule_carrier_flush()


## Returns [code]true[/code] when [param route] is tracked by the spawn
## ledger on this peer, as an issued authority spawn or a received
## materialization. [LivenessShell] consults this to grant tracked
## roots the end-of-frame reparent grace.
func owns_spawned_route(route: int) -> bool:
	return _spawn_book.spawned.has(route) or _spawn_book.is_recv(route)


# Authority-side implicit despawn with the reparent grace: a tracked root
# leaving the tree resolves at end-of-frame. Back inside the tree means
# reparent, and a REPARENT frame rides out with the route surviving.
# Still outside means despawn, cascaded child-first.
func _on_tracked_root_exiting(route: int) -> void:
	_resolve_tracked_root_exit.call_deferred(route)


func _resolve_tracked_root_exit(route: int) -> void:
	var record: NetwSpawnBook.SpawnRecord = _spawn_book.spawned.get(route)
	if not record:
		return
	var node := record.node()
	if node and node.is_inside_tree():
		var api := _api()
		if not api:
			return
		api._connect_once(
			node.tree_exiting,
			_on_tracked_root_exiting.bind(route),
			CONNECT_ONE_SHOT,
		)
		_send_reparent(record, node)
		return
	_despawn_tracked_route(route)


# Despawns a tracked route, its tracked descendants first, so a receiver
# always processes child despawns before the ancestor that contains them.
func _despawn_tracked_route(route: int) -> void:
	var record: NetwSpawnBook.SpawnRecord = _spawn_book.spawned.get(route)
	if not record:
		return
	var api := _api()
	var node := record.node()
	var entity := NetwEntity.of(node) if is_instance_valid(node) else null
	if api and entity:
		api._spawn_undeclare(entity.rid)
	_spawn_book.spawned.erase(route)
	for child_route in _spawn_book.spawned.keys():
		var child: NetwSpawnBook.SpawnRecord = _spawn_book.spawned.get(child_route)
		if child and child.parent_route == route:
			_despawn_tracked_route(child_route)
	if not api or not api.inner.multiplayer_peer:
		return
	if not api.is_online:
		return
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, route)
	var payload := w.to_bytes()
	var connected := api.inner.get_peers()
	for peer_id in record.recipients:
		if peer_id in connected:
			api._replication.send_to(
				peer_id,
				0,
				NetwFrameEnvelope.Channel.DESPAWN,
				payload,
				true,
				0,
				"",
				true,
			)
	_schedule_carrier_flush()


# Re-derives a record's parent route and, for a consumed spawner, its spawner
# anchor from the node's live tree position. A reparent has no observable edge
# on either, so any code that reads the record after a move must refresh first
# or it addresses the origin scene the node already left.
func _refresh_record_anchors(record: NetwSpawnBook.SpawnRecord, node: Node) -> void:
	var api := _api()
	if not api:
		return
	var parent_entity := NetwEntity.of(node.get_parent())
	record.parent_route = (
			api._liveness.route_of(parent_entity) if parent_entity else 0
	)
	api._interest._sync_scene_membership(NetwEntity.of(node))
	# A consumed record's recipe must stay reconstructible from the destination,
	# or the sweep re-encode and late-join replay hand new observers a spawner
	# anchor inside a scene they were never admitted to.
	api._replication._spawner_compat.reanchor_record(record, node)


func _send_reparent(record: NetwSpawnBook.SpawnRecord, node: Node) -> void:
	var api := _api()
	if not api:
		return
	_refresh_record_anchors(record, node)
	if not api.inner.multiplayer_peer:
		return
	if not api.is_online:
		return
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, record.route)
	if not _encode_anchor(w, node.get_parent()):
		Netw.dbg.warn(
			"NetwSpawnPipeline: reparented '%s' outside the "
			+ "MultiplayerTree, peers keep the old parent",
			[node.name],
		)
		return
	var payload := w.to_bytes()
	var connected := api.inner.get_peers()
	for peer_id in record.recipients:
		if peer_id not in connected:
			continue
		# The move producing this REPARENT can be the same edge that revokes a
		# recipient's visibility. That peer gets the sweep's DESPAWN, and a
		# REPARENT anchored on a route it was never sent must not race it.
		if not _spawn_visible_to(peer_id, record, node):
			continue
		api._replication.send_to(
			peer_id,
			0,
			NetwFrameEnvelope.Channel.REPARENT,
			payload,
			true,
			0,
			"",
			true,
		)
	_schedule_carrier_flush()
	# The new parent changes the book-derived ancestor clamp without firing any
	# synchronizer visibility signal, so the sweep must be asked for explicitly.
	schedule_visibility_sweep()


# Encodes one SPAWN frame, carrying identity and control fields, the parent
# anchor, the reconstruction recipe, then the collected spawn state.
func _encode_spawn_frame(record: NetwSpawnBook.SpawnRecord, node: Node) -> PackedByteArray:
	var api := _api()
	var w := NetwBitBufferWriter.new()
	var entity := NetwEntity.of(node)
	NetwCodec.put_varint(w, record.route)
	_put_str(w, String(record.entity_id))
	NetwCodec.put_varint(w, record.peer_id)
	# Read the live controller, not record.controller: a late joiner's SPAWN
	# frame must reflect a mid-session grant_control/revoke_control, not the
	# value captured once when this entity first armed.
	NetwCodec.put_varint(w, entity.controller if entity else record.controller)
	NetwCodec.put_varint(w, entity.action_spawn_tick if entity else -1)
	NetwCodec.put_varint(w, entity.action_requester if entity else 0)
	NetwCodec.put_varint(w, entity.components.wire_hash if entity else 0)
	# The scene facet rides the header, not a spawn property: a client has to
	# know the entity owns an admission boundary before it anchors any child
	# under it. The stem only follows a declared scene.
	var declares_scene := entity != null and entity.declares_scene
	NetwCodec.put_varint(w, 1 if declares_scene else 0)
	if declares_scene:
		_put_str(w, String(entity.scene_label))
	_put_str(w, record.node_name)

	if not _encode_anchor(w, node.get_parent()):
		Netw.dbg.error(
			"NetwSpawnPipeline: parent of '%s' is outside the "
			+ "MultiplayerTree, the spawn cannot be addressed",
			[node.name],
		)
		return PackedByteArray()

	w.put_aligned_u8(record.recipe)
	if record.recipe == NetwSpawnBook.Recipe.ADOPT:
		pass # The parent anchor and name already address the existing instance.
	elif record.recipe == NetwSpawnBook.Recipe.SCENE:
		_put_scene_recipe(w, record.scene_path)
	elif record.recipe == NetwSpawnBook.Recipe.SPAWNER:
		var spawner := record.spawner()
		if not spawner or not _encode_anchor(w, spawner):
			Netw.dbg.error(
				"NetwSpawnPipeline: consumed spawner for route %d is gone or "
				+ "outside the MultiplayerTree",
				[record.route],
			)
			return PackedByteArray()
		# scene_index+1 keeps 0 free to mean a custom spawn, mirroring native's
		# own scene-index compression.
		NetwCodec.put_varint(w, record.scene_index + 1)
		if record.scene_index < 0:
			var dbytes := var_to_bytes(record.custom_data)
			NetwCodec.put_varint(w, dbytes.size())
			w.put_aligned_bytes(dbytes)
	elif record.recipe == NetwSpawnBook.Recipe.FN_REGISTRY:
		var fn := _spawn_constructor(record.fn_registry_id)
		if not fn.is_valid():
			Netw.dbg.error(
				"NetwSpawnPipeline: no spawn constructor registered under id "
				+ "'%s' for route %d",
				[record.fn_registry_id, record.route],
			)
			return PackedByteArray()
		_put_str(w, String(record.fn_registry_id))
		var host := fn.get_object()
		_encode_fn_args(w, host.get_script() as Script, fn.get_method(), record.fn_args)
	elif record.recipe == NetwSpawnBook.Recipe.FN:
		var host := record.fn_host()
		if not host or not _encode_anchor(w, host):
			Netw.dbg.error(
				"NetwSpawnPipeline: spawn function host for route %d "
				+ "is gone or outside the MultiplayerTree",
				[record.route],
			)
			return PackedByteArray()
		_put_str(w, String(record.fn_method))
		_encode_fn_args(w, host.get_script() as Script, record.fn_method, record.fn_args)
	else:
		Netw.dbg.error(
			"NetwSpawnPipeline: unknown spawn recipe %d for route %d",
			[record.recipe, record.route],
		)
		return PackedByteArray()

	var entries := _collect_spawn_state(node)
	NetwCodec.put_varint(w, entries.size())
	for entry in entries:
		var source: Node = entry["node"]
		var prop: StringName = entry["prop"]
		var cfg: NetwScriptModel.SyncConfig = entry["cfg"]
		# A receiver applies spawn state while the node is still orphaned,
		# before its component table exists, so only the root (comp 0) and a
		# path relative to the root (comp 255) can be resolved. Address the
		# source as one of those two and carry the property as a script-derived
		# token rather than its name.
		if source == node:
			w.put_aligned_u8(0)
		else:
			w.put_aligned_u8(255)
			_put_str(w, String(node.get_path_to(source)))
		NetwScriptModel.write_token(
			w,
			api._replication._sync_pipeline._encode_prop_val(entity, source, prop),
		)
		var quantizer: NetwQuantize = (
				cfg.quantizers[0] if not cfg.quantizers.is_empty() else null
		)
		var type := NetwScriptModel.get_node_property_type(source, prop)
		# Length-prefix the value bytes so a receiver that cannot resolve this
		# entry skips exactly its own bytes rather than misreading them and
		# corrupting every later entry in the frame.
		var vw := NetwBitBufferWriter.new()
		NetwScriptModel.write_values(vw, [source.get(prop)], [quantizer], [type])
		var vbytes := vw.to_bytes()
		NetwCodec.put_varint(w, vbytes.size())
		w.put_aligned_bytes(vbytes)

	# Native spawn-state (conformance row 6): the union of authority-held
	# synchronizers' spawn properties, exactly the set the native spawn packet
	# carries, so a stock synchronizer under the spawned root receives its
	# replication_config spawn values pre-tree.
	var native_entries := _collect_native_spawn_state(node)
	NetwCodec.put_varint(w, native_entries.size())
	for entry in native_entries:
		_put_str(w, String(entry["path"]))
		var nbytes: PackedByteArray = var_to_bytes(entry["value"])
		NetwCodec.put_varint(w, nbytes.size())
		w.put_aligned_bytes(nbytes)

	# Consumed sync-set descriptors: one (ordinal, schema hash) per consumed
	# synchronizer under this route, so the receiver validates its translated
	# sets against the sender's at spawn time instead of misreading bytes.
	api._replication._sync_compat.encode_descriptors(w, record.route)
	# Derived sync-set descriptors, the same (ordinal, schema hash) contract at
	# the unified ordinals above the consumed count, for the sets a
	# configure_property node declares.
	api._replication._sync_pipeline.encode_derived_descriptors(w, record.route)
	return w.to_bytes()


# Mirrors the native spawn-state collection: every MultiplayerSynchronizer in
# the subtree whose resolved root IS the spawned root and whose authority is
# held locally contributes its replication_config spawn properties, in tree
# preorder. Property NodePaths stay relative to the spawned root, subnames
# included, matching MultiplayerSynchronizer.get_state.
func _collect_native_spawn_state(root: Node) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var api := _api()
	var local_id := api.get_unique_id() if api and api.inner.multiplayer_peer else 1
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		var sync := n as MultiplayerSynchronizer
		if sync and sync.replication_config \
				and sync.get_multiplayer_authority() == local_id \
				and sync.get_node_or_null(sync.root_path) == root:
			for prop: NodePath in sync.replication_config.get_properties():
				if not sync.replication_config.property_get_spawn(prop):
					continue
				if prop.get_subname_count() == 0:
					continue
				var target := root
				var names := NodePath(prop.get_concatenated_names())
				if not names.is_empty():
					target = root.get_node_or_null(names)
				if not target:
					continue
				var subnames := NodePath(":" + prop.get_concatenated_subnames())
				out.append(
					{
						"path": prop,
						"value": target.get_indexed(subnames),
					},
				)
		var children := n.get_children()
		for i in range(children.size() - 1, -1, -1):
			stack.append(children[i])
	return out


# Collects every property marked .on_spawn() under root, in preorder, so the
# apply order on the receiver matches the authority's declaration order.
func _collect_spawn_state(root: Node) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		var configs := NetwScriptModel.get_node_property_configs(n)
		for prop: StringName in configs:
			var cfg: NetwScriptModel.SyncConfig = configs[prop]
			if cfg.is_spawn_state and prop in n:
				out.append({ "node": n, "prop": prop, "cfg": cfg })
		var children := n.get_children()
		for i in range(children.size() - 1, -1, -1):
			stack.append(children[i])
	return out


func _handle_spawn_frame(payload: PackedByteArray, sender: int) -> void:
	var api := _api()
	var verdict := api._spawn_admit_frame(
		sender,
		0,
		NetwFrameEnvelope.Channel.SPAWN,
		payload,
	) if api else ERR_UNAVAILABLE
	if not api or api._finish_gate_verdict(verdict, 0) != OK:
		if sender != 1:
			_drops_spawn_bad_sender += 1
		return
	_try_apply_spawn(payload)


# Routes one materializer through the installed independent constructor stage.
func _run_construct_stage(constructor: Callable) -> Node:
	var api := _api()
	if not api:
		return constructor.call() as Node
	api._spawn_constructor = constructor
	var node := api._spawn_construct(RID())
	api._spawn_constructor = Callable()
	return node


# The receive pipeline: resolve dependencies (park when a parent or fn host
# has not spawned yet), construct, stamp identity while orphaned, apply spawn
# state, then place. Ordering here IS the I1/I2 contract: by add_child every
# hook from root PARENTED onward sees valid identity and state.
func _try_apply_spawn(payload: PackedByteArray) -> void:
	var api := _api()
	if not api:
		return
	var liveness := api._liveness
	var r := NetwBitBufferReader.create(payload)

	var route := NetwCodec.get_safe_varint(r)
	if route <= 0:
		_drops_spawn_unresolved += 1
		return
	# A route already materialized here is an idempotent duplicate. A DEAD
	# route is NOT: SPAWN and DESPAWN share one reliable ordered channel, so a
	# SPAWN arriving after the DESPAWN that tombstoned the route is an
	# interest re-admission reviving the entity, never a stale packet.
	var route_state := liveness.route_state(route)
	if route_state == NetwMultiplayer.EntityState.LIVE \
			or route_state == NetwMultiplayer.EntityState.LINGERING \
			or _spawn_book.is_recv(route):
		_drops_spawn_duplicate += 1
		return

	var entity_id := StringName(_get_str(r))
	var peer_id := NetwCodec.get_safe_varint(r)
	var controller := NetwCodec.get_safe_varint(r)
	var action_spawn_tick := NetwCodec.get_safe_varint(r)
	var action_requester := NetwCodec.get_safe_varint(r)
	var netw_table_hash := NetwCodec.get_safe_varint(r)
	var declares_scene := NetwCodec.get_safe_varint(r) != 0
	var scene_label := StringName(_get_str(r)) if declares_scene else &""
	var node_name := _get_str(r)

	var parent_anchor := _decode_anchor(r)
	if int(parent_anchor["route"]) > 0 \
			and _anchor_parks(liveness.route_state(int(parent_anchor["route"]))):
		_park_spawn(payload, int(parent_anchor["route"]), route)
		return

	var recipe := r.get_aligned_u8()
	var node: Node = null
	var recv_spawner: MultiplayerSpawner = null
	var adopted := false
	if recipe == NetwSpawnBook.Recipe.ADOPT:
		# Nothing is reconstructed: the instance already exists at the anchored
		# parent and name, built out of band on every peer, and the frame only
		# stamps identity onto it.
		var adopt_parent := _resolve_anchor(parent_anchor)
		node = _run_construct_stage(
			func() -> Node:
				return adopt_parent.get_node_or_null(node_name) \
				if adopt_parent else null,
		)
		if not node:
			Netw.dbg.warn(
				"NetwSpawnPipeline: ADOPT for route %d found no node named "
				+ "'%s' under '%s'; the peers' structure must match",
				[route, node_name, parent_anchor["path"]],
			)
			_drops_spawn_unresolved += 1
			return
		adopted = true
	elif recipe == NetwSpawnBook.Recipe.SCENE:
		var scene_path := _get_scene_recipe(r)
		var packed: PackedScene = null
		if not scene_path.is_empty() and (ResourceLoader.has_cached(scene_path) \
						or ResourceLoader.exists(scene_path)):
			packed = load(scene_path) as PackedScene
		if not packed:
			Netw.dbg.warn(
				"NetwSpawnPipeline: SPAWN for route %d names an "
				+ "unknown scene '%s'",
				[route, scene_path],
			)
			_drops_spawn_unresolved += 1
			return
		node = _run_construct_stage(
			func() -> Node:
				return packed.instantiate(),
		)
	elif recipe == NetwSpawnBook.Recipe.SPAWNER:
		var spawner_anchor := _decode_anchor(r)
		if int(spawner_anchor["route"]) > 0 \
				and _anchor_parks(liveness.route_state(int(spawner_anchor["route"]))):
			_park_spawn(payload, int(spawner_anchor["route"]), route)
			return
		recv_spawner = _resolve_anchor(spawner_anchor) as MultiplayerSpawner
		if not recv_spawner:
			# The scene subtree holding the spawner is not here yet (a late-join
			# replay outran its scene, or a path anchor cannot park on a route).
			# Wait for a scene to arrive rather than dropping the player.
			_park_spawn_for_scene(payload, route)
			return
		var scene_index := NetwCodec.get_safe_varint(r) - 1
		var data: Variant = null
		if scene_index < 0:
			var dlen := NetwCodec.get_safe_varint(r)
			data = bytes_to_var(r.get_aligned_bytes(maxi(dlen, 0)))
		node = _run_construct_stage(
			func() -> Node:
				return api._replication._spawner_compat.instantiate(
					recv_spawner,
					scene_index,
					data,
				),
		)
		if not node:
			Netw.dbg.warn(
				"NetwSpawnPipeline: consumed spawner could not reconstruct "
				+ "route %d",
				[route],
			)
			_drops_spawn_unresolved += 1
			return
	elif recipe == NetwSpawnBook.Recipe.FN_REGISTRY:
		var reg_id := StringName(_get_str(r))
		var fn := _spawn_constructor(reg_id)
		if not fn.is_valid():
			Netw.dbg.warn(
				"NetwSpawnPipeline: SPAWN for route %d names spawn "
				+ "constructor '%s' that is not registered",
				[route, reg_id],
			)
			_drops_spawn_unresolved += 1
			return
		var host := fn.get_object()
		var script := host.get_script() as Script
		var method := fn.get_method()
		var encoded_args := _read_fn_args(r, script, method)
		if encoded_args == null:
			Netw.dbg.warn(
				"NetwSpawnPipeline: SPAWN for route %d names constructor "
				+ "'%s' whose arguments are not configured",
				[route, reg_id],
			)
			_drops_spawn_unresolved += 1
			return
		var args = _resolve_spawn_args(encoded_args, payload, route, liveness)
		if args == null:
			return
		node = _run_construct_stage(
			func() -> Node:
				return fn.callv(args) as Node,
		)
		if not node:
			Netw.dbg.warn(
				"NetwSpawnPipeline: spawn constructor '%s' did not "
				+ "return a Node for route %d",
				[reg_id, route],
			)
			_drops_spawn_unresolved += 1
			return
	elif recipe == NetwSpawnBook.Recipe.FN:
		var host_anchor := _decode_anchor(r)
		if int(host_anchor["route"]) > 0 \
				and _anchor_parks(liveness.route_state(int(host_anchor["route"]))):
			_park_spawn(payload, int(host_anchor["route"]), route)
			return
		var host := _resolve_anchor(host_anchor)
		if not host:
			Netw.dbg.warn(
				"NetwSpawnPipeline: SPAWN for route %d cannot resolve "
				+ "its spawn function host",
				[route],
			)
			_drops_spawn_unresolved += 1
			return
		var method := StringName(_get_str(r))
		var script := host.get_script() as Script
		var encoded_args := _read_fn_args(r, script, method)
		if encoded_args == null:
			Netw.dbg.warn(
				"NetwSpawnPipeline: SPAWN for route %d names spawn "
				+ "function '%s' that is not registered on '%s'",
				[route, method, host.name],
			)
			_drops_spawn_unresolved += 1
			return
		var args = _resolve_spawn_args(encoded_args, payload, route, liveness)
		if args == null:
			return
		node = _run_construct_stage(
			func() -> Node:
				return host.callv(method, args) as Node,
		)
		if not node:
			Netw.dbg.warn(
				"NetwSpawnPipeline: spawn function '%s' did not "
				+ "return a Node for route %d",
				[method, route],
			)
			_drops_spawn_unresolved += 1
			return
	else:
		Netw.dbg.warn(
			"NetwSpawnPipeline: SPAWN for route %d carries unknown recipe %d",
			[route, recipe],
		)
		_drops_spawn_unresolved += 1
		return

	var parent := _resolve_anchor(parent_anchor)
	if not parent:
		Netw.dbg.warn(
			"NetwSpawnPipeline: SPAWN for route %d cannot resolve "
			+ "its parent anchor '%s'",
			[route, parent_anchor["path"]],
		)
		_drops_spawn_unresolved += 1
		if not adopted:
			node.queue_free()
		return

	var entity := NetwEntity.ensure(node)
	entity.entity_id = entity_id
	entity.peer_id = peer_id
	entity.controller = controller
	entity.action_spawn_tick = action_spawn_tick
	entity.action_requester = action_requester
	# An action spawn wires the display gate so the entity hides on its live edge
	# until the local display reaches the action tick.
	if action_spawn_tick >= 0:
		_ensure_action_gate_connection()
	entity.components.wire_hash = netw_table_hash
	entity.declares_scene = declares_scene
	entity.scene_label = scene_label
	entity.route = route
	# Seal the record and settle authority on the orphan, before add_child, so
	# is_multiplayer_authority() is correct in every child's tree entry on the
	# client the same way it is on the server. The armer holds the session and
	# hands it over, so the record never re-walks the tree to find it.
	if entity.stage == NetwEntity.Stage.UNBOUND:
		entity.arm(api)

	var state_count := NetwCodec.get_safe_varint(r)
	for i in maxi(state_count, 0):
		# Every field self-advances the reader so an entry whose target does
		# not resolve is skipped cleanly, leaving the rest of the frame intact.
		var comp := r.get_aligned_u8()
		var target := node
		if comp == 255:
			target = node.get_node_or_null(_get_str(r))
		var prop_token := NetwScriptModel.read_token(r)
		var value_len := NetwCodec.get_safe_varint(r)
		var value_bytes := r.get_aligned_bytes(maxi(value_len, 0))
		if not target:
			continue
		var prop: StringName
		if prop_token is int:
			prop = NetwScriptModel.get_property_name_by_id(
				target.get_script() as Script,
				prop_token,
			)
		else:
			prop = StringName(prop_token)
		if prop.is_empty() or not (prop in target):
			continue
		var quantizer: NetwQuantize = null
		var cfg := NetwScriptModel.get_node_property_configs(target) \
				.get(prop) as NetwScriptModel.SyncConfig
		if cfg and not cfg.quantizers.is_empty():
			quantizer = cfg.quantizers[0]
		var type := NetwScriptModel.get_node_property_type(target, prop)
		var vr := NetwBitBufferReader.create(value_bytes)
		var values := NetwScriptModel.read_values(vr, [quantizer], [type])
		if not values.is_empty():
			target.set(prop, values[0])

	# Native spawn-state section, applied against the orphaned root exactly as
	# the native pending-spawn consumption would, but before add_child.
	var native_count := NetwCodec.get_safe_varint(r)
	for i in maxi(native_count, 0):
		var prop_path := NodePath(_get_str(r))
		var value_len := NetwCodec.get_safe_varint(r)
		var value_bytes := r.get_aligned_bytes(maxi(value_len, 0))
		var target := node
		var names := NodePath(prop_path.get_concatenated_names())
		if not names.is_empty():
			target = node.get_node_or_null(names)
		if not target:
			continue
		var subnames := NodePath(":" + prop_path.get_concatenated_subnames())
		target.set_indexed(subnames, bytes_to_var(value_bytes))

	# Consumed sync-set descriptors, recorded before add_child so the bindings
	# registering during tree entry validate against them.
	var descriptor_count := NetwCodec.get_safe_varint(r)
	var descriptors: Dictionary = { }
	for i in maxi(descriptor_count, 0):
		var ordinal := NetwCodec.get_safe_varint(r)
		descriptors[ordinal] = r.get_aligned_u16()
	api._replication._sync_compat.note_schema(route, descriptors)

	# Derived sync-set descriptors, the same contract at the unified ordinals
	# above the consumed count, validated as each configure_property node's frames
	# arrive.
	var derived_count := NetwCodec.get_safe_varint(r)
	var derived_descriptors: Dictionary = { }
	for i in maxi(derived_count, 0):
		var d_ordinal := NetwCodec.get_safe_varint(r)
		derived_descriptors[d_ordinal] = r.get_aligned_u16()
	api._replication._sync_pipeline.note_derived_schema(route, derived_descriptors)

	if adopted:
		_spawn_book.enroll_recv(route, node)
		# An adopted instance never re-enters the tree, so the route binds here
		# instead of through the tree-entry hook.
		liveness.bind_route(route, entity)
		return

	if not node_name.is_empty():
		if parent.has_node(node_name):
			node.name = "%s@%d" % [node_name, route]
		else:
			node.name = node_name

	_spawn_book.enroll_recv(route, node)
	if recv_spawner:
		api._replication._spawner_compat.note_recv(route, recv_spawner)
	var was_applying := _applying_remote_frame
	_applying_remote_frame = true
	parent.add_child(node)
	_applying_remote_frame = was_applying
	if recv_spawner:
		api._replication._spawner_compat.emit_spawned(recv_spawner, node)


func _handle_despawn_frame(payload: PackedByteArray, sender: int) -> void:
	var gate_api := _api()
	var verdict := gate_api._spawn_admit_frame(
		sender,
		0,
		NetwFrameEnvelope.Channel.DESPAWN,
		payload,
	) if gate_api else ERR_UNAVAILABLE
	if not gate_api or gate_api._finish_gate_verdict(verdict, 0) != OK:
		if sender != 1:
			_drops_spawn_bad_sender += 1
		return
	var r := NetwBitBufferReader.create(payload)
	var route := NetwCodec.get_safe_varint(r)
	if route <= 0:
		return
	if _parked_spawn_routes.has(route):
		# Spawned and despawned while the SPAWN was parked. Net result zero.
		_parked_spawn_routes.erase(route)
		_spawn_parked_cancelled += 1
		return
	if not _spawn_book.is_recv(route):
		_drops_despawn_unknown += 1
		return
	_spawn_book.recv.erase(route)

	var api := _api()
	var liveness := api._liveness if api else null
	var entity := liveness.entity_of(route) if liveness else null
	if not entity or not is_instance_valid(entity.owner):
		_drops_despawn_unknown += 1
		return
	var node := entity.owner

	var cfg := NetwScriptModel.get_despawn_config(node.get_script() as Script)
	if cfg and cfg.hook_method != &"" and node.has_method(cfg.hook_method):
		node.call(cfg.hook_method)
	var linger_seconds := cfg.linger_seconds if cfg else 0.0
	# Drive the record's teardown edges so despawning and despawned fire
	# uniformly on every peer. Liveness reads the emission to transition the
	# route to LINGERING, replacing the direct reach-in this path used to make.
	entity._remote_despawn(&"despawn", linger_seconds)
	if linger_seconds > 0.0 and node.is_inside_tree():
		api._connect_once(
			node.get_tree().create_timer(linger_seconds).timeout,
			_free_despawned.bind(route),
		)
		return
	_free_despawned(route)


func _handle_reparent_frame(payload: PackedByteArray, sender: int) -> void:
	var gate_api := _api()
	var verdict := gate_api._spawn_admit_frame(
		sender,
		0,
		NetwFrameEnvelope.Channel.REPARENT,
		payload,
	) if gate_api else ERR_UNAVAILABLE
	if not gate_api or gate_api._finish_gate_verdict(verdict, 0) != OK:
		if sender != 1:
			_drops_spawn_bad_sender += 1
		return
	var api := _api()
	if not api:
		return
	var liveness := api._liveness
	var r := NetwBitBufferReader.create(payload)
	var route := NetwCodec.get_safe_varint(r)
	if route <= 0:
		return
	var anchor := _decode_anchor(r)

	var entity := liveness.entity_of(route)
	if not entity or not is_instance_valid(entity.owner):
		# The SPAWN is parked or the route already died. Reliable ordering
		# makes a lost reparent heal on the next spawn edge, so drop counted.
		_drops_spawn_unresolved += 1
		return
	if int(anchor["route"]) > 0 \
			and _anchor_parks(liveness.route_state(int(anchor["route"]))):
		var retry := func() -> void: _handle_reparent_frame(payload, 1)
		_spawn_deferrals += 1
		liveness.when_live(int(anchor["route"]), retry, _park_timeout_ticks())
		return
	var parent := _resolve_anchor(anchor)
	if not parent:
		_drops_spawn_unresolved += 1
		return
	var node := entity.owner
	if node.get_parent() == parent:
		return
	if node.get_parent():
		node.get_parent().remove_child(node)
	var was_applying := _applying_remote_frame
	_applying_remote_frame = true
	parent.add_child(node)
	_applying_remote_frame = was_applying
	# Visibility is ancestry-derived, so the parent clamp recomputes the child's
	# row from its new ancestor with nothing to do here. The destination scene's
	# own player book has no edge to observe on a route-stable reparent, so it is
	# refreshed through the session rather than by reaching for a scene class.
	if api:
		api._scene_adopt_entity(api.rid_of(node))


func _free_despawned(route: int) -> void:
	var api := _api()
	if not api:
		return
	var entity := api._liveness.entity_of(route)
	if not entity or not is_instance_valid(entity.owner):
		return
	var node := entity.owner
	var parent := node.get_parent()
	if parent:
		parent.remove_child(node)
	api._replication._spawner_compat.emit_despawned(route, node)
	node.queue_free()


# A frame dependency parks when its route is not resolvable yet: UNKNOWN means
# the frame arrived early, DEAD means an interest re-admission whose reviving
# SPAWN is still in flight on the same reliable ordered channel.
func _anchor_parks(state: LivenessShell.State) -> bool:
	return state == NetwMultiplayer.EntityState.UNKNOWN \
			or state == NetwMultiplayer.EntityState.DEAD


# The park window in when_live ticks, derived from the configured tickrate or
# the same 30-tick fallback when_live itself assumes without a clock.
func _park_timeout_ticks() -> int:
	var api := _api()
	var tickrate := 30.0
	if api and api.clock.is_configured():
		tickrate = float(api.clock.tickrate)
	return int(ceil(tickrate * park_timeout_seconds))


# Parks the frame spawning [param route] until [param dep_route] goes live.
# A DESPAWN arriving during the park cancels it with net result zero. An
# expired park unparks the route, so the flag can never outlive the wait and
# swallow a later DESPAWN as a phantom cancel.
func _park_spawn(payload: PackedByteArray, dep_route: int, route: int) -> void:
	var api := _api()
	if not api:
		return
	_spawn_deferrals += 1
	_parked_spawn_routes[route] = true
	var retry := func() -> void:
		if not _parked_spawn_routes.has(route):
			return
		_parked_spawn_routes.erase(route)
		_try_apply_spawn(payload)
	var expire := func() -> void:
		if not _parked_spawn_routes.has(route):
			return
		_parked_spawn_routes.erase(route)
		_spawn_park_expired += 1
	api.when_live(dep_route, retry, _park_timeout_ticks(), expire)


# Parks a spawner-consumed SPAWN whose scene subtree is not present, retrying
# when a scene spawns. Unlike a routed dependency, a consumed spawner is
# addressed by a path this peer cannot yet resolve and has no route to wait on
# through when_live, so the arrival of any scene is the retry trigger. The wait
# is bounded by [member park_timeout_seconds] so an anchor that never resolves
# still gives up instead of holding the payload forever.
func _park_spawn_for_scene(payload: PackedByteArray, route: int) -> void:
	var api := _api()
	if not api or not api._scenes:
		_drops_spawn_unresolved += 1
		return
	_spawn_deferrals += 1
	_parked_spawn_routes[route] = true
	_spawner_parked[route] = {
		"payload": payload,
		"deadline": Time.get_ticks_msec() + int(park_timeout_seconds * 1000.0),
	}
	api._connect_once(api.entity_live, _retry_scene_parked_spawns)


# Retries every scene-parked SPAWN whenever any entity goes live, since a scene
# is an ordinary entity and one arrival can carry the spawner several parked
# players waited on. A DESPAWN that cleared the route mid-park drops the entry,
# an expired wait gives up with a warning, and a retry that still cannot resolve
# re-parks itself for the next arrival.
func _retry_scene_parked_spawns(_route: int, _entity: NetwEntity) -> void:
	var now := Time.get_ticks_msec()
	for parked_route: int in _spawner_parked.keys():
		if not _parked_spawn_routes.has(parked_route):
			_spawner_parked.erase(parked_route)
			continue
		var entry: Dictionary = _spawner_parked[parked_route]
		_spawner_parked.erase(parked_route)
		_parked_spawn_routes.erase(parked_route)
		if now >= int(entry["deadline"]):
			_spawn_park_expired += 1
			Netw.dbg.warn(
				"NetwSpawnPipeline: SPAWN for route %d gave up waiting for its "
				+ "consumed spawner's scene",
				[parked_route],
			)
			continue
		_try_apply_spawn(entry["payload"])
	var api := _api()
	if _spawner_parked.is_empty() and api and api._scenes \
			and api._scenes.scene_spawned.is_connected(_retry_scene_parked_spawns):
		api._scenes.scene_spawned.disconnect(_retry_scene_parked_spawns)


# Two-level reference addressing: a target inside a routed entity encodes as
# (route, subpath from that entity's root), anything else as a path from the
# replication root node. Returns false when the target cannot be addressed.
func _encode_anchor(w: NetwBitBufferWriter, target: Node) -> bool:
	var api := _api()
	if not api or not is_instance_valid(target):
		return false
	var entity := NetwEntity.of(target)
	var route := api._liveness.route_of(entity) if entity else 0
	if entity and route > 0:
		w.put_aligned_u8(1)
		NetwCodec.put_varint(w, route)
		_put_str(w, String(entity.owner.get_path_to(target)))
		return true
	var root := api.root
	if not root or not (target == root or root.is_ancestor_of(target)):
		return false
	w.put_aligned_u8(0)
	_put_str(w, String(root.get_path_to(target)))
	return true


func _decode_anchor(r: NetwBitBufferReader) -> Dictionary:
	var kind := r.get_aligned_u8()
	if kind == 1:
		var route := NetwCodec.get_safe_varint(r)
		return { "kind": 1, "route": route, "path": _get_str(r) }
	return { "kind": 0, "route": 0, "path": _get_str(r) }


func _resolve_anchor(anchor: Dictionary) -> Node:
	var api := _api()
	if not api:
		return null
	if int(anchor["kind"]) == 1:
		var entity := api._liveness.entity_of(int(anchor["route"]))
		if not entity or not is_instance_valid(entity.owner):
			return null
		return entity.owner.get_node_or_null(String(anchor["path"]))
	var root := api.root
	return root.get_node_or_null(String(anchor["path"])) if root else null


# A scene reconstructs from its resource UID when it has one, which is stable
# across file moves and more compact than the path. Scenes without a UID (an
# in-memory test scene, for one) fall back to the path string.
func _put_scene_recipe(w: NetwBitBufferWriter, path: String) -> void:
	var uid := ResourceLoader.get_resource_uid(path)
	if uid != ResourceUID.INVALID_ID:
		w.put_aligned_u8(1)
		_put_u64(w, uid)
	else:
		w.put_aligned_u8(0)
		_put_str(w, path)


func _get_scene_recipe(r: NetwBitBufferReader) -> String:
	if r.get_aligned_u8() == 1:
		var uid := _get_u64(r)
		return ResourceUID.get_id_path(uid) if ResourceUID.has_id(uid) else ""
	return _get_str(r)


func _put_u64(w: NetwBitBufferWriter, value: int) -> void:
	var spb := StreamPeerBuffer.new()
	spb.put_64(value)
	w.put_aligned_bytes(spb.data_array)


func _get_u64(r: NetwBitBufferReader) -> int:
	var spb := StreamPeerBuffer.new()
	spb.data_array = r.get_aligned_bytes(8)
	return spb.get_64()


func _put_str(w: NetwBitBufferWriter, value: String) -> void:
	var bytes := value.to_utf8_buffer()
	NetwCodec.put_varint(w, bytes.size())
	w.put_aligned_bytes(bytes)


func _get_str(r: NetwBitBufferReader) -> String:
	var size := NetwCodec.get_safe_varint(r)
	if size <= 0:
		return ""
	return r.get_aligned_bytes(size).get_string_from_utf8()


# The API answers server offline and in a mid-connect disconnected window, so
# unit rigs without a peer still exercise the server-only verbs, tree or not.
func _is_server_authority() -> bool:
	var api := _api()
	return api.is_server() if api else true


## Drops all per-session spawn state. Called by
## [method ReplicationCore.clear_session].
func clear_session() -> void:
	var api := _api()
	if api and api._scenes \
			and api._scenes.scene_spawned.is_connected(_retry_scene_parked_spawns):
		api._scenes.scene_spawned.disconnect(_retry_scene_parked_spawns)
	_spawner_parked.clear()
	_parked_spawn_routes.clear()
	_spawn_book.clear()
	_clear_action_gates()


## Drops [param route]'s armed and receive-side book traces when its
## [NetwEntity] despawns. The spawned record deliberately survives so the
## authority's tree-exit handler can still issue the DESPAWN frame from it.
func clear_route(route: int) -> void:
	_spawn_book.armed.erase(route)
	_spawn_book.recv.erase(route)
	if _action_gates.has(route):
		_action_gates.erase(route)
		_disconnect_action_reveal_if_idle()

#region Action display gate

# One suspended action-spawn owner, hidden until its display tick arrives. Held
# by route in _action_gates. Weakref-backed so a freed owner clears itself.
class _ActionGate:
	extends RefCounted

	var owner_ref: WeakRef
	var hidden := false
	var original_visible := true
	var action_tick := -1


# The display clock, or null while none is configured, so the gate degrades to a
# no-op against a clockless session.
func _gate_display_clock() -> ClockCore:
	var api := _api()
	if api and api.clock.is_configured():
		return api._clock
	return null


# Hides a remote NetwAction result until the local display playhead reaches the
# action tick, so a spawned effect appears in step with the displayed world
# rather than the moment its frame arrived. The requester keeps its immediate
# predicted presentation, and an entity that never came from an action, or whose
# display already passed the tick, is never touched. Reached from the spawn frame
# through a liveness live edge so both the adopted and tree-entry bind paths gate.
func _apply_action_gate(route: int, entity: NetwEntity) -> void:
	if _action_gates.has(route):
		return
	if entity.action_spawn_tick < 0 or _is_local_action_requester(entity):
		return
	var clock := _gate_display_clock()
	if not clock or clock.display_tick >= entity.action_spawn_tick:
		return
	var gate := _ActionGate.new()
	gate.owner_ref = weakref(entity.owner)
	gate.action_tick = entity.action_spawn_tick
	if not _set_gate_visible(gate, false):
		return
	gate.hidden = true
	_action_gates[route] = gate
	var api := _api()
	if api:
		api._connect_once(clock.on_tick, _on_action_reveal_tick)


func _on_action_reveal_tick(_delta: float, tick: int) -> void:
	var clock := _gate_display_clock()
	for gate_route in _action_gates.keys():
		var gate := _action_gates[gate_route] as _ActionGate
		var reached := true
		if clock:
			reached = maxi(0, tick - clock.display_offset) >= gate.action_tick
		if reached:
			_reveal_gate(gate_route)
	_disconnect_action_reveal_if_idle()


func _reveal_gate(route: int) -> void:
	var gate := _action_gates.get(route) as _ActionGate
	if not gate:
		return
	if gate.hidden:
		_set_gate_visible(gate, gate.original_visible)
		gate.hidden = false
	_action_gates.erase(route)


func _disconnect_action_reveal_if_idle() -> void:
	if not _action_gates.is_empty():
		return
	var clock := _gate_display_clock()
	if clock and clock.on_tick.is_connected(_on_action_reveal_tick):
		clock.on_tick.disconnect(_on_action_reveal_tick)


func _is_local_action_requester(entity: NetwEntity) -> bool:
	if entity.action_requester == 0:
		return false
	var api := _api()
	if not api or api.multiplayer_peer == null:
		return false
	return entity.action_requester == api.get_unique_id()


# Sets the gated owner's visibility, capturing its original state on the first
# hide so the reveal restores exactly what the scene declared. Returns
# [code]false[/code] for a non-visual owner (a bare logic node), which the gate
# then leaves alone.
func _set_gate_visible(gate: _ActionGate, value: bool) -> bool:
	var owner := gate.owner_ref.get_ref() as Node
	if owner is CanvasItem:
		var item := owner as CanvasItem
		if not gate.hidden:
			gate.original_visible = item.visible
		item.visible = value
		return true
	if owner is Node3D:
		var spatial := owner as Node3D
		if not gate.hidden:
			gate.original_visible = spatial.visible
		spatial.visible = value
		return true
	return false


# Ensures the liveness live edge drives the gate. Connected lazily from the spawn
# frame so a session with no action spawns never wires it.
func _ensure_action_gate_connection() -> void:
	var api := _api()
	var lv := api._liveness if api else null
	if lv:
		api._connect_once(lv.entity_live, _apply_action_gate)


# Clears every pending gate and drops the reveal and live connections, for
# session teardown.
func _clear_action_gates() -> void:
	_action_gates.clear()
	_disconnect_action_reveal_if_idle()
	var api := _api()
	var lv := api._liveness if api else null
	if lv and lv.entity_live.is_connected(_apply_action_gate):
		lv.entity_live.disconnect(_apply_action_gate)

#endregion

## Returns this pipeline's contribution to
## [method ReplicationCore.counters].
func counters() -> Dictionary:
	return {
		&"drops_spawn_bad_sender": _drops_spawn_bad_sender,
		&"drops_spawn_duplicate": _drops_spawn_duplicate,
		&"drops_spawn_unresolved": _drops_spawn_unresolved,
		&"drops_despawn_unknown": _drops_despawn_unknown,
		&"spawn_deferrals": _spawn_deferrals,
		&"spawn_parked_cancelled": _spawn_parked_cancelled,
		&"spawn_park_expired": _spawn_park_expired,
		&"spawn_book_armed": _spawn_book.armed.size(),
		&"spawn_book_spawned": _spawn_book.spawned.size(),
		&"spawn_book_recv": _spawn_book.recv.size(),
	}
