## Applies [NetwInterestLayer] state to Godot replication.
##
## One lives under each [MultiplayerTree], owned by [NetwMultiplayer] and
## exposed at [member NetwMultiplayer.interest]. [InterestEngine] owns the
## committed visibility matrix. This interface translates object-facing
## mutations into plain engine state, applies the resulting delta, and relays
## optional owner-side observer events.
##
## [br][br]
## Client-side [signal NetwInterestLayer.entity_visible] and
## [signal NetwInterestLayer.entity_hidden] are delivered through one reliable,
## route-addressed awareness projection. An enter waits for its route to become
## live. An exit for a route that is no longer live is already satisfied.
##
## [br][br]
## Layers do not replicate viewer sets or policies. Their committed rows gate
## the spawn and synchronization pipelines directly, while clients learn only
## the attribution for their own row.
##
## [br][br]
## Scene wrappers are ordinary members of their scene layer. Their committed
## parent row clamps every descendant, so generic layers should refine an
## already-admitted scene rather than reveal its root.
##
## [br][br]
## Server code usually creates layers through [method layer]. Client code may
## observe local attribution and callbacks through [member NetwEntity.interest].
## [codeblock]
## var arena := Netw.of(self).interest.layer(&"arena")
## arena.add_entity(player_entity)
## arena.add_viewer(player.peer_id)
## [/codeblock]
class_name NetwInterestInterface
extends RefCounted

var _layers: Dictionary[StringName, NetwInterestLayer] = { }
var _entity_layers: Dictionary[NetwEntity, Dictionary] = { }
var _scene_memberships: Dictionary[NetwEntity, StringName] = { }
var _intent_entities: Dictionary[NetwEntity, bool] = { }
var _committed_intent_entities: Dictionary[NetwEntity, bool] = { }
var _entity_exit_handlers: Dictionary[NetwEntity, Callable] = { }
var _dirty_entities: Dictionary[NetwEntity, bool] = { }
var _pending_leave_layers: Dictionary[NetwEntity, Dictionary] = { }
var _retained_peers: Dictionary[NetwEntity, Dictionary] = { }
var _perception_visible: Dictionary[NetwEntity, bool] = { }
var _perception_snapshots: Dictionary[NetwEntity, Array] = { }
var _perception_custom_actions: Dictionary[NetwEntity, Array] = { }
var _refresh_scheduled: bool = false
var _engine := InterestEngine.new()
var _peer_bits: Dictionary[int, int] = { }
var _bit_peers: Dictionary[int, int] = { }
var _next_peer_bit: int = 0
var _entity_order: Dictionary[NetwEntity, int] = { }
var _next_entity_order: int = 1

## Transition kind for relayed visibility / observer events.
enum Kind { EXIT, ENTER }

## Wire behavior when a layer stops admitting an entity to a peer.
enum LeavePolicy {
	## Send [constant NetwFrameEnvelope.Channel.DESPAWN] and free the peer's node.
	DESPAWN,
	## Keep the peer's node spawned while its continuous state stream is frozen.
	RETAIN,
	## Run the entity's configured callback in place of a wire despawn.
	CUSTOM,
}

## Local presentation behavior when the participant row stops admitting an
## entity that remains present in this process.
enum PerceptionPolicy {
	## Hide visual nodes and mute audio without changing simulation processing.
	HIDE,
	## Leave presentation unchanged.
	SHOW,
	## Run the entity's configured callback for local enter and leave edges.
	CUSTOM,
}


## Stable entity-level interest configuration owned by
## [member NetwEntity.interest].
##
## Membership declarations survive tree exits and reapply when the entity
## enters a session. The server owns the real [NetwInterestLayer] membership.
## Clients keep the same labels and callback surface for local visibility and
## observer-awareness events.
## [codeblock]
## var interest := NetwEntity.resolve(self).interest
## interest.join(&"team:red")
## interest.on_enter(&"team:red", _on_visible)
## [/codeblock]
class InterestHandle:
	extends RefCounted

	var _entity_ref: WeakRef
	var _service_ref: WeakRef
	var _layer_ids: Array[StringName] = []
	var _enter_callbacks: Dictionary[StringName, Array] = { }
	var _leave_callbacks: Dictionary[StringName, Array] = { }
	var _observed_callbacks: Array[Callable] = []
	var _unobserved_callbacks: Array[Callable] = []
	var _leave_policies: Dictionary[StringName, int] = { }
	var _custom_leave_callbacks: Dictionary[StringName, Callable] = { }
	var _perception_policies: Dictionary[StringName, int] = { }
	var _custom_perception_callbacks: Dictionary[StringName, Callable] = { }
	var _report_observers := false


	# Binds the handle once and installs its entity lifecycle hooks.
	func _bind(entity: NetwEntity) -> void:
		assert(entity != null, "InterestHandle._bind: entity is null")
		var current := _entity()
		assert(
			current == null or current == entity,
			"InterestHandle cannot be rebound to another entity",
		)
		if current == entity:
			return
		_entity_ref = weakref(entity)
		var root := entity.owner
		if not is_instance_valid(root):
			return
		if not root.tree_entered.is_connected(_activate):
			root.tree_entered.connect(_activate)
		if not root.tree_exiting.is_connected(_deactivate):
			root.tree_exiting.connect(_deactivate)
		if not entity.observer_entered.is_connected(_on_observer_entered):
			entity.observer_entered.connect(_on_observer_entered)
		if not entity.observer_left.is_connected(_on_observer_left):
			entity.observer_left.connect(_on_observer_left)
		if root.is_inside_tree():
			_activate()


	## Adds the entity to [param layer_id]. Idempotent.
	##
	## The declaration is safe in [method Object._init] on every peer. Only
	## server authority mutates the live [NetwInterestLayer] entity set.
	func join(layer_id: StringName) -> InterestHandle:
		assert(
			not layer_id.is_empty(),
			"InterestHandle.join: layer_id is empty",
		)
		if layer_id in _layer_ids:
			return self
		_layer_ids.append(layer_id)
		var service := _service()
		if service:
			var entity := _entity()
			if service._is_server() and entity:
				service.layer(layer_id).add_entity(entity)
		return self


	## Removes the entity from [param layer_id]. Idempotent.
	func leave(layer_id: StringName) -> InterestHandle:
		if layer_id not in _layer_ids:
			return self
		_layer_ids.erase(layer_id)
		var service := _service()
		var entity := _entity()
		if service and service._is_server() and entity:
			var layer := service.get_layer(layer_id)
			if layer:
				layer.remove_entity(entity)
		return self


	## Returns a copy of the locally known layer labels.
	func layer_ids() -> Array[StringName]:
		return _layer_ids.duplicate()


	## Returns whether [param peer_id] currently sees this entity.
	func is_visible_to(peer_id: int) -> bool:
		var service := _service()
		var entity := _entity()
		if not service or not entity:
			return false
		if not service.has_filter(entity):
			return true
		return service.participant_sees(peer_id, entity)


	## Calls [param callback] with [code](layer_id, peer_id)[/code] whenever this
	## entity becomes visible through [param layer_id].
	func on_enter(
			layer_id: StringName,
			callback: Callable,
	) -> InterestHandle:
		_assert_callback("on_enter", callback)
		var callbacks: Array = _enter_callbacks.get_or_add(layer_id, [])
		if callback not in callbacks:
			callbacks.append(callback)
		return self


	## Calls [param callback] with [code](layer_id, peer_id)[/code] whenever this
	## entity stops being visible through [param layer_id].
	func on_leave(
			layer_id: StringName,
			callback: Callable,
	) -> InterestHandle:
		_assert_callback("on_leave", callback)
		var callbacks: Array = _leave_callbacks.get_or_add(layer_id, [])
		if callback not in callbacks:
			callbacks.append(callback)
		return self


	## Calls [param callback] with [code](peer_id)[/code] when another peer
	## starts observing this entity. Registering opts the entity into the
	## owner-awareness relay.
	func on_observed(callback: Callable) -> InterestHandle:
		_assert_callback("on_observed", callback)
		_report_observers = true
		if callback not in _observed_callbacks:
			_observed_callbacks.append(callback)
		return self


	## Calls [param callback] with [code](peer_id)[/code] when another peer
	## stops observing this entity. Registering opts the entity into the
	## owner-awareness relay.
	func on_unobserved(callback: Callable) -> InterestHandle:
		_assert_callback("on_unobserved", callback)
		_report_observers = true
		if callback not in _unobserved_callbacks:
			_unobserved_callbacks.append(callback)
		return self


	## Overrides the leave behavior for [param layer_id].
	##
	## [enum LeavePolicy].CUSTOM requires [param custom_callback], called with
	## [code](peer_id, layer_id)[/code] on server authority. Other policies
	## reject a callback so configuration mistakes fail at declaration time.
	func on_leave_policy(
			layer_id: StringName,
			policy: LeavePolicy,
			custom_callback: Callable = Callable(),
	) -> InterestHandle:
		assert(
			not layer_id.is_empty(),
			"InterestHandle.on_leave_policy: layer_id is empty",
		)
		assert(
			policy >= LeavePolicy.DESPAWN and policy <= LeavePolicy.CUSTOM,
			"InterestHandle.on_leave_policy: invalid policy",
		)
		if policy == LeavePolicy.CUSTOM:
			assert(
				custom_callback.is_valid(),
				"InterestHandle.on_leave_policy: CUSTOM requires a callback",
			)
			_custom_leave_callbacks[layer_id] = custom_callback
		else:
			assert(
				not custom_callback.is_valid(),
				"InterestHandle.on_leave_policy: callback requires CUSTOM",
			)
			_custom_leave_callbacks.erase(layer_id)
		_leave_policies[layer_id] = policy
		return self


	## Overrides local presentation behavior for [param layer_id].
	##
	## [enum PerceptionPolicy].CUSTOM requires [param custom_callback]. The
	## callback receives [code](visible, peer_id, layer_id)[/code] on both local
	## participant edges. Other policies reject a callback.
	func on_perception_policy(
			layer_id: StringName,
			policy: PerceptionPolicy,
			custom_callback: Callable = Callable(),
	) -> InterestHandle:
		assert(
			not layer_id.is_empty(),
			"InterestHandle.on_perception_policy: layer_id is empty",
		)
		assert(
			policy >= PerceptionPolicy.HIDE
			and policy <= PerceptionPolicy.CUSTOM,
			"InterestHandle.on_perception_policy: invalid policy",
		)
		if policy == PerceptionPolicy.CUSTOM:
			assert(
				custom_callback.is_valid(),
				"InterestHandle.on_perception_policy: CUSTOM requires a callback",
			)
			_custom_perception_callbacks[layer_id] = custom_callback
		else:
			assert(
				not custom_callback.is_valid(),
				"InterestHandle.on_perception_policy: callback requires CUSTOM",
			)
			_custom_perception_callbacks.erase(layer_id)
		_perception_policies[layer_id] = policy
		var service := _service()
		var entity := _entity()
		if service and entity:
			service._reapply_local_perception(entity)
		return self


	# Enables or disables observer carriage for builder and migration paths.
	func _set_report_observers(enabled: bool) -> void:
		_report_observers = enabled


	# Returns whether the server should carry observer-awareness events.
	func _reports_observers() -> bool:
		return _report_observers


	# Resolves the entity override before a layer default.
	func _leave_policy_for(layer_id: StringName, fallback: LeavePolicy) -> int:
		return int(_leave_policies.get(layer_id, fallback))


	# Returns the callback configured for one CUSTOM layer.
	func _custom_leave_for(layer_id: StringName) -> Callable:
		return _custom_leave_callbacks.get(layer_id, Callable())


	# Resolves the entity perception override before a layer default.
	func _perception_policy_for(
			layer_id: StringName,
			fallback: PerceptionPolicy,
	) -> int:
		return int(_perception_policies.get(layer_id, fallback))


	# Returns the callback configured for one CUSTOM perception layer.
	func _custom_perception_for(layer_id: StringName) -> Callable:
		return _custom_perception_callbacks.get(layer_id, Callable())


	# Adds a server-authored label learned from an unbound-layer relay.
	func _client_join_label(layer_id: StringName) -> void:
		if layer_id not in _layer_ids:
			_layer_ids.append(layer_id)


	# Dispatches a layer-specific enter transition for this entity.
	func _dispatch_enter(layer_id: StringName, peer_id: int) -> void:
		_emit_callbacks(_enter_callbacks.get(layer_id, []), layer_id, peer_id)


	# Dispatches a layer-specific leave transition for this entity.
	func _dispatch_leave(layer_id: StringName, peer_id: int) -> void:
		_emit_callbacks(_leave_callbacks.get(layer_id, []), layer_id, peer_id)


	# Activates declared memberships and callback bindings for this session.
	func _activate() -> void:
		var entity := _entity()
		if not entity or not is_instance_valid(entity.owner):
			return
		var api := entity.multiplayer
		if not api:
			api = NetwMultiplayer.of(entity.owner)
		if not api:
			return
		_service_ref = weakref(api.interest)
		for layer_id in _layer_ids:
			var layer := api.interest.layer(layer_id)
			if api.interest._is_server():
				layer.add_entity(entity)


	# Removes live memberships and session signal bindings on tree exit.
	func _deactivate() -> void:
		var service := _service()
		var entity := _entity()
		if service and service._is_server() and entity:
			for layer_id in _layer_ids:
				var layer := service.get_layer(layer_id)
				if layer:
					layer.remove_entity(entity)
		_service_ref = null


	# Dispatches owner-awareness enter callbacks.
	func _on_observer_entered(
			_layer_id: StringName,
			peer_id: int,
	) -> void:
		for callback in _observed_callbacks:
			callback.call(peer_id)


	# Dispatches owner-awareness leave callbacks.
	func _on_observer_left(
			_layer_id: StringName,
			peer_id: int,
	) -> void:
		for callback in _unobserved_callbacks:
			callback.call(peer_id)


	# Returns the bound entity while it is alive.
	func _entity() -> NetwEntity:
		return _entity_ref.get_ref() as NetwEntity if _entity_ref else null


	# Returns the active session service while it is alive.
	func _service() -> NetwInterestInterface:
		return _service_ref.get_ref() as NetwInterestInterface \
		if _service_ref else null


	# Validates a callback at registration time.
	func _assert_callback(source: String, callback: Callable) -> void:
		assert(
			callback.is_valid(),
			"InterestHandle.%s: callback is invalid" % source,
		)


	# Calls layer callbacks with their documented argument pair.
	func _emit_callbacks(
			callbacks: Array,
			layer_id: StringName,
			peer_id: int,
	) -> void:
		for callback: Callable in callbacks:
			callback.call(layer_id, peer_id)


## Fluent declarative builder returned by [method Netw.configure_interest].
class InterestConfig:
	extends RefCounted

	var _handle: InterestHandle
	var _layers: Array[StringName] = []


	func _init(handle: InterestHandle) -> void:
		_handle = handle


	## Adds [param layer_id], optionally overrides its leave and perception
	## policies, and returns this builder.
	func layer(
			layer_id: StringName,
			leave_policy: Variant = null,
			perception_policy: Variant = null,
	) -> InterestConfig:
		_handle.join(layer_id)
		if leave_policy != null:
			assert(
				leave_policy is int,
				"InterestConfig.layer: leave_policy must be a LeavePolicy",
			)
			_handle.on_leave_policy(layer_id, int(leave_policy))
		if perception_policy != null:
			assert(
				perception_policy is int,
				"InterestConfig.layer: perception_policy must be a PerceptionPolicy",
			)
			_handle.on_perception_policy(layer_id, int(perception_policy))
		if layer_id not in _layers:
			_layers.append(layer_id)
		return self


	## Registers [param callback] for enter events on every layer already added
	## through [method layer].
	func on_enter(callback: Callable) -> InterestConfig:
		for layer_id in _layers:
			_handle.on_enter(layer_id, callback)
		return self


	## Registers [param callback] for leave events on every layer already added
	## through [method layer].
	func on_leave(callback: Callable) -> InterestConfig:
		for layer_id in _layers:
			_handle.on_leave(layer_id, callback)
		return self


# Server-side payload for one route-addressed awareness projection.
class _AwarenessRelay:
	extends RefCounted
	var type: int
	var route: int
	var layer_id: StringName
	var observer_peer: int
	var kind: int


	func _init(
			t: int,
			r: int,
			l: StringName,
			o: int,
			k: int,
	) -> void:
		type = t
		route = r
		layer_id = l
		observer_peer = o
		kind = k


	func to_wire() -> Array:
		return [type, route, layer_id, observer_peer, kind]


	static func from_wire(raw: Variant) -> _AwarenessRelay:
		if typeof(raw) != TYPE_ARRAY or (raw as Array).size() != 5:
			return null
		if not raw[0] is int or not raw[1] is int \
				or not raw[2] is StringName or not raw[3] is int \
				or not raw[4] is int:
			return null
		var event_type: int = raw[0]
		var event_route: int = raw[1]
		var event_layer: StringName = raw[2]
		var event_observer: int = raw[3]
		var event_kind: int = raw[4]
		if event_type < AwarenessType.LAYER \
				or event_type > AwarenessType.OBSERVER \
				or event_route <= 0 \
				or event_layer.is_empty() \
				or event_observer < 0 \
				or (event_kind != Kind.ENTER and event_kind != Kind.EXIT):
			return null
		if event_type == AwarenessType.LAYER and event_observer != 0:
			return null
		if event_type == AwarenessType.OBSERVER and event_observer == 0:
			return null
		return _AwarenessRelay.new(
			event_type,
			event_route,
			event_layer,
			event_observer,
			event_kind,
		)


enum AwarenessType { LAYER, OBSERVER }

var _awareness_relay: Dictionary[int, Array] = { }

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	if api:
		api.peer_connected.connect(_on_peer_connected)
		api.peer_disconnected.connect(_on_peer_disconnected)
		api.session_ended.connect(_on_session_ended)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# The session anchor node for path addressing: the owning MultiplayerTree under a
# subpath install, /root under a root install. Both resolve the same relative
# paths every peer built symmetrically.
func _anchor() -> Node:
	var api := _api()
	return api.root if api else null


func _on_peer_connected(peer_id: int) -> void:
	_ensure_peer_bit(peer_id)
	_sync_live_peers()
	_refresh_compat_intents.call_deferred()
	_schedule_visibility_flush()


func _on_peer_disconnected(peer_id: int) -> void:
	_awareness_relay.erase(peer_id)
	for per_peer: Dictionary in _pending_leave_layers.values():
		per_peer.erase(peer_id)
	for per_peer: Dictionary in _retained_peers.values():
		per_peer.erase(peer_id)
	_sync_live_peers()
	_schedule_visibility_flush()


# Defers the reset so it runs after any scene despawn driven by the same
# session_ended emission has drained entity state through the normal
# tree_exiting path. Clearing the layers and admit counters mid-despawn would
# desync them and trip the underflow assert. Deferring makes the reset
# independent of the order [SceneManager] and this interface handle the signal.
func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


# Drops every per-session entry so a same-layer second session starts clean.
func _clear_session_state() -> void:
	_layers.clear()
	_entity_layers.clear()
	_scene_memberships.clear()
	_intent_entities.clear()
	_committed_intent_entities.clear()
	_entity_exit_handlers.clear()
	_dirty_entities.clear()
	_pending_leave_layers.clear()
	_retained_peers.clear()
	var perceived_entities: Array[NetwEntity] = []
	perceived_entities.assign(_perception_visible.keys())
	for entity: NetwEntity in perceived_entities:
		_clear_local_perception(entity, true)
	_perception_visible.clear()
	_perception_snapshots.clear()
	_perception_custom_actions.clear()
	_awareness_relay.clear()
	_engine = InterestEngine.new()
	_peer_bits.clear()
	_bit_peers.clear()
	_next_peer_bit = 0
	_entity_order.clear()
	_next_entity_order = 1
	_refresh_scheduled = false


## Returns the layer for [param layer_id], creating it on first use.
func layer(layer_id: StringName) -> NetwInterestLayer:
	return layer_for(layer_id)


## Returns the layer for [param layer_id], creating it when missing.
func layer_for(layer_id: StringName) -> NetwInterestLayer:
	if layer_id.is_empty():
		return null
	var found: NetwInterestLayer = _layers.get(layer_id)
	if found:
		return found
	found = NetwInterestLayer.new(layer_id, self)
	_layers[layer_id] = found
	_sync_engine_layer(found)
	return found


## Returns the layer for [param layer_id], or [code]null[/code].
func get_layer(layer_id: StringName) -> NetwInterestLayer:
	return _layers.get(layer_id)


## Returns every known layer.
func all_layers() -> Array[NetwInterestLayer]:
	var out: Array[NetwInterestLayer] = []
	out.assign(_layers.values())
	return out


# Fills [param found] with the live entities whose own declared labels intersect
# [param layer_ids], for a peer that holds no committed roster to walk.
#
# A route exists on this peer only because it was sent the spawn, so the walk
# ranges over exactly what this peer can already see. The labels come from each
# candidate's own relay-filled cache rather than from any layer the server
# committed, which is why this stays inside the knowledge budget instead of
# reconstructing another peer's row.
func _collect_shared_from_live(
		entity: NetwEntity,
		layer_ids: Array[StringName],
		found: Dictionary[NetwEntity, bool],
) -> void:
	var api := _api()
	if api == null:
		return
	var wanted: Dictionary[StringName, bool] = { }
	for id: StringName in layer_ids:
		wanted[id] = true
	for candidate: NetwEntity in api.liveness.live_entities():
		if candidate == entity or not is_instance_valid(candidate) 				or not is_instance_valid(candidate.owner):
			continue
		for id: StringName in candidate.interest.layer_ids():
			if wanted.has(id):
				found[candidate] = true
				break


## Returns the resolved interest memberships for [param entity] on this peer.
##
## The result includes ancestry-derived scene membership as well as labels
## declared through [member NetwEntity.interest].
func resolved_layer_ids(entity: NetwEntity) -> Array[StringName]:
	var out: Array[StringName] = []
	if entity == null:
		return out
	out.assign(_entity_layers.get(entity, { }).keys())
	if out.is_empty() and not _is_server():
		out = entity.interest.layer_ids()
	out.sort_custom(
		func(a: StringName, b: StringName) -> bool:
			return String(a) < String(b),
	)
	return out


## Returns locally replicated entities sharing [param entity]'s resolved
## interest scope, or only [param layer_id] when it is non-empty.
##
## The result excludes [param entity] and is stable by entity id. It contains
## only live entities present in this peer's interest layers.
##
## A peer that computes no admission holds no layer roster, so off the server
## the answer is derived from the two facts such a peer legitimately has: the
## entities it holds a live route for, and each of their own declared labels.
## Both are already its own row, so the derivation adds no knowledge and no
## traffic. It cannot name an entity this peer cannot see, and it never reports
## what any other peer sees.
## [codeblock]
## server   walk the committed layer roster
## peer     walk my live routes, keep the ones whose labels intersect mine
## [/codeblock]
func shared_entities(
		entity: NetwEntity,
		layer_id: StringName = &"",
) -> Array[NetwEntity]:
	if not layer_id.is_empty() and layer_id not in resolved_layer_ids(entity):
		return []
	var layer_ids: Array[StringName] = []
	if layer_id.is_empty():
		layer_ids = resolved_layer_ids(entity)
	else:
		layer_ids.append(layer_id)
	var found: Dictionary[NetwEntity, bool] = { }
	for current_id: StringName in layer_ids:
		var current := get_layer(current_id)
		if current == null:
			continue
		for candidate: NetwEntity in current.entities:
			if candidate != entity and is_instance_valid(candidate) \
					and is_instance_valid(candidate.owner):
				found[candidate] = true
	if found.is_empty() and not _is_server():
		_collect_shared_from_live(entity, layer_ids, found)
	var out: Array[NetwEntity] = []
	out.assign(found.keys())
	out.sort_custom(
		func(a: NetwEntity, b: NetwEntity) -> bool:
			var a_id := String(a.entity_id)
			var b_id := String(b.entity_id)
			if a_id == b_id:
				return a.get_instance_id() < b.get_instance_id()
			return a_id < b_id,
	)
	return out


## Returns tree-wide interest occupancy counters for [InterestMonitor].
##
## [code]visible_edges[/code] is the count of admitted (entity, peer) pairs across
## every layer, the matrix occupancy that drives replication bandwidth.
## [code]transitions_total[/code] sums the per-layer
## [method NetwInterestLayer.monitor_snapshot] churn since creation, so the monitor
## reads it as a delta over an interval.
## [codeblock]
## {
##   ┠╴ layers: int             # active layers
##   ┠╴ entities_filtered: int  # entities with a visibility filter installed
##   ┠╴ visible_edges: int      # admitted (entity, peer) pairs, all layers
##   ┠╴ dirty_entities: int     # entities pending a visibility recompute
##   ┠╴ relay_backlog: int      # relayed transitions awaiting entity liveness
##   ┖╴ transitions_total: int  # summed per-layer churn since creation
## }
## [/codeblock]
func monitor_snapshot() -> Dictionary:
	var visible_edges := _engine.stats().edges
	var transitions_total := 0
	for layer_id: StringName in _layers:
		transitions_total += int(
			_layers[layer_id].monitor_snapshot()[&"transitions_total"],
		)
	return {
		&"layers": _layers.size(),
		&"entities_filtered": _entity_layers.size(),
		&"visible_edges": visible_edges,
		&"dirty_entities": _dirty_entities.size(),
		&"relay_backlog": _liveness_backlog(),
		&"transitions_total": transitions_total,
	}


## Returns the committed peer admits for [param entity].
func committed_admits(entity: NetwEntity) -> Dictionary:
	var out: Dictionary = { }
	var row := _engine.row_of(entity)
	for bit in InterestBitSet.bits(row):
		var peer_id := int(_bit_peers.get(bit, 0))
		if peer_id != 0:
			out[peer_id] = 1
	return out


## Returns [code]true[/code] if [param entity] has interest memberships.
func has_filter(entity: NetwEntity) -> bool:
	if not _is_server() and entity:
		return not entity.interest.layer_ids().is_empty()
	return _entity_layers.has(entity)


## Returns whether [param entity] has intent folded into its committed row.
func has_committed_intent(entity: NetwEntity) -> bool:
	return _committed_intent_entities.has(entity)


## Returns whether replication may send [param entity] to [param peer_id].
##
## Server authority always knows every entity, so the server peer short-circuits
## here. Gameplay presentation should call [method participant_sees].
func wire_admits(peer_id: int, entity: NetwEntity) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	return participant_sees(peer_id, entity)


## Returns the honest committed row for [param peer_id] and [param entity].
##
## Unlike [method wire_admits], the listen host is evaluated as an ordinary
## participant. Clients can answer only from their own projected row.
func participant_sees(peer_id: int, entity: NetwEntity) -> bool:
	if peer_id == 0 or entity == null:
		return false
	if not _is_server():
		return _client_projection_admits(entity)
	if not _peer_bits.has(peer_id):
		return false
	return _engine.test(entity, _peer_bits[peer_id])


func _on_layer_policy_changed(changed_layer: NetwInterestLayer) -> void:
	_sync_engine_layer(changed_layer)
	_mark_layer_dirty(changed_layer)


func _on_layer_viewer_changed(
		changed_layer: NetwInterestLayer,
		_peer_id: int,
		_added: bool,
) -> void:
	_sync_engine_layer(changed_layer)
	_mark_layer_dirty(changed_layer)


func _on_layer_entity_changed(
		changed_layer: NetwInterestLayer,
		entity: NetwEntity,
		added: bool,
) -> void:
	if added:
		_track_entity_layer(entity, changed_layer.layer_id)
		_track_entity_lifecycle(entity)
	else:
		_untrack_entity_layer(entity, changed_layer.layer_id)
	_sync_engine_entity(entity)
	_mark_entity_dirty(entity)


# Reconciles one entity's ancestry-derived scene layer membership.
func _sync_scene_membership(entity: NetwEntity) -> void:
	if not _is_server() or entity == null \
			or not is_instance_valid(entity.owner):
		return
	var previous: StringName = _scene_memberships.get(entity, &"")
	var scene := MultiplayerScene.of(entity.owner)
	var current := scene.scene_layer_id() if scene else &""
	if previous == current:
		return
	if not previous.is_empty():
		var previous_layer := get_layer(previous)
		if previous_layer:
			previous_layer.remove_entity(entity)
	if current.is_empty():
		_scene_memberships.erase(entity)
		return
	_scene_memberships[entity] = current
	layer(current).add_entity(entity)


func _track_entity_layer(entity: NetwEntity, layer_id: StringName) -> void:
	var layers: Dictionary = _entity_layers.get_or_add(entity, { })
	layers[layer_id] = true


func _untrack_entity_layer(entity: NetwEntity, layer_id: StringName) -> void:
	var layers: Dictionary = _entity_layers.get(entity, { })
	layers.erase(layer_id)
	if layers.is_empty():
		_entity_layers.erase(entity)


func _track_entity_lifecycle(entity: NetwEntity) -> void:
	if _entity_exit_handlers.has(entity):
		return
	var handler := _on_entity_tree_exiting.bind(entity)
	_entity_exit_handlers[entity] = handler
	if not entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.connect(handler)


func _untrack_entity_lifecycle(entity: NetwEntity) -> void:
	var handler: Callable = _entity_exit_handlers.get(entity, Callable())
	if handler.is_valid() and is_instance_valid(entity) \
			and is_instance_valid(entity.owner) \
			and entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.disconnect(handler)
	_entity_exit_handlers.erase(entity)


func _on_entity_tree_exiting(entity: NetwEntity) -> void:
	var layer_ids: Dictionary = _entity_layers.get(entity, { }).duplicate()
	for layer_id: StringName in layer_ids:
		var exiting_layer := get_layer(layer_id)
		if not exiting_layer:
			continue
		if _is_server():
			exiting_layer.remove_entity(entity)
		else:
			exiting_layer._client_untrack_entity(entity)
	_untrack_entity_lifecycle(entity)
	_engine.remove_entity(entity)
	_intent_entities.erase(entity)
	_entity_order.erase(entity)
	_dirty_entities.erase(entity)
	_pending_leave_layers.erase(entity)
	_retained_peers.erase(entity)
	_scene_memberships.erase(entity)
	_clear_local_perception(entity, false)


func _mark_layer_dirty(dirty_layer: NetwInterestLayer) -> void:
	for entity: NetwEntity in dirty_layer._entities:
		_mark_entity_dirty(entity)


func _mark_entity_dirty(entity: NetwEntity) -> void:
	assert(
		entity != null,
		"NetwInterestInterface: _mark_entity_dirty called with null entity",
	)
	_dirty_entities[entity] = true
	_schedule_visibility_flush()


func _schedule_visibility_flush() -> void:
	if _refresh_scheduled:
		return
	_refresh_scheduled = true
	_flush_visibility.call_deferred()


## Flushes the committed matrix and its awareness projection.
func flush() -> void:
	var api := _api()
	_refresh_scheduled = false
	_flush_engine()
	_flush_awareness_relay()
	# Admission changes drive per-peer spawn/despawn through the spawn book.
	if api and api.has_multiplayer_peer() \
			and api.multiplayer_peer.get_connection_status() \
					!= MultiplayerPeer.CONNECTION_DISCONNECTED:
		api.replication._spawn_pipeline.schedule_visibility_sweep()


## Flushes pending mutations immediately.
##
## Normal gameplay observes mutations at the deferred end-of-frame flush. Use
## this method in tests and before an in-frame spawn that needs the new row.
func flush_now() -> void:
	flush()


func _flush_visibility() -> void:
	if not _refresh_scheduled:
		return
	flush()


func _flush_engine() -> void:
	if not _is_server():
		_dirty_entities.clear()
		return
	_sync_live_peers()
	var entities: Dictionary[NetwEntity, bool] = { }
	for entity: NetwEntity in _entity_layers:
		entities[entity] = true
	for entity: NetwEntity in _intent_entities:
		entities[entity] = true
	for entity: NetwEntity in entities:
		if is_instance_valid(entity) and is_instance_valid(entity.owner):
			_sync_engine_entity(entity)
	var committed_intents := _intent_entities.duplicate()
	var delta := _engine.recompute()
	_apply_engine_delta(delta)
	_engine.commit(delta)
	_committed_intent_entities = committed_intents
	_refresh_all_local_perception()
	_dirty_entities.clear()


func _apply_engine_delta(delta: InterestDelta) -> void:
	for transition: Array in delta.layer_hides:
		_apply_layer_delta_transition(transition, false)
	for transition: Array in delta.layer_shows:
		_apply_layer_delta_transition(transition, true)
	for transition: Array in delta.shows:
		var entity := transition[0] as NetwEntity
		if entity:
			_clear_retained_peer(entity, _peer_for_bit(transition[1]))


func _apply_layer_delta_transition(
		transition: Array,
		visible: bool,
) -> void:
	var layer_id: StringName = transition[0]
	var entity := transition[1] as NetwEntity
	var peer_id := _peer_for_bit(transition[2])
	var event_layer := get_layer(layer_id)
	if not event_layer or not entity or peer_id == 0:
		return
	event_layer._apply_server_transition(entity, peer_id, visible)
	if visible:
		_on_layer_interest_enter(entity, peer_id, event_layer)
	else:
		_on_layer_interest_exit(entity, peer_id, event_layer)


func _client_projection_admits(entity: NetwEntity) -> bool:
	var labels := entity.interest.layer_ids()
	if labels.is_empty():
		return true
	for layer_id: StringName in labels:
		var projected_layer := get_layer(layer_id)
		if projected_layer and projected_layer.has_entity(entity):
			return true
	return false


func _sync_engine_layer(engine_layer: NetwInterestLayer) -> void:
	var viewer_bits := PackedInt64Array()
	for peer_id: int in engine_layer.viewers:
		var bit := _ensure_peer_bit(peer_id)
		viewer_bits = InterestBitSet.with_bit(viewer_bits, bit)
	_engine.set_layer(engine_layer.layer_id, viewer_bits, engine_layer.policy)


func _sync_live_peers() -> void:
	var peer_ids: Dictionary[int, bool] = { }
	var api := _api()
	if api and api.has_multiplayer_peer():
		for peer_id: int in api.get_peers():
			peer_ids[peer_id] = true
	if api and api.is_listen_server():
		peer_ids[MultiplayerPeer.TARGET_PEER_SERVER] = true
	for engine_layer: NetwInterestLayer in _layers.values():
		for peer_id: int in engine_layer.viewers:
			peer_ids[peer_id] = true
	var live_bits := PackedInt64Array()
	var added_peer := false
	for peer_id in peer_ids:
		added_peer = added_peer or not _peer_bits.has(peer_id)
		var bit := _ensure_peer_bit(peer_id)
		live_bits = InterestBitSet.with_bit(live_bits, bit)
	_engine.set_live_peers(live_bits)
	if added_peer and api:
		api.replication._sync_compat.refresh_interest_intents()


func _sync_engine_entity(entity: NetwEntity) -> void:
	if not _entity_layers.has(entity) and not _intent_entities.has(entity):
		_engine.remove_entity(entity)
		return
	_sync_engine_entity_record(entity, { })


func _sync_engine_entity_record(
		entity: NetwEntity,
		visited: Dictionary,
) -> void:
	if entity == null or visited.has(entity):
		return
	visited[entity] = true
	var parent := entity.parent_entity()
	if parent:
		_sync_engine_entity_record(parent, visited)
	var memberships: Array[StringName] = []
	memberships.assign(_entity_layers.get(entity, { }).keys())
	_engine.set_membership(entity, memberships)
	_engine.set_parent(entity, parent)
	var depth := 0
	var current := parent
	while current != null:
		depth += 1
		current = current.parent_entity()
	var route := entity.route
	if route <= 0:
		if not _entity_order.has(entity):
			_entity_order[entity] = _next_entity_order
			_next_entity_order += 1
		route = _entity_order[entity]
	_engine.set_order_key(entity, depth, route)


func _set_entity_intent(
		entity: NetwEntity,
		admitted_peers: Array[int],
) -> void:
	if entity == null:
		return
	_intent_entities[entity] = true
	_track_entity_lifecycle(entity)
	_sync_engine_entity_record(entity, { })
	var row := PackedInt64Array()
	for peer_id in admitted_peers:
		var bit := _ensure_peer_bit(peer_id)
		row = InterestBitSet.with_bit(row, bit)
	_engine.set_intent(entity, row)
	_mark_entity_dirty(entity)


func _clear_entity_intent(entity: NetwEntity) -> void:
	_intent_entities.erase(entity)
	if _entity_layers.has(entity):
		_engine.set_intent_all(entity)
		_mark_entity_dirty(entity)
	else:
		_engine.remove_entity(entity)


func _known_peer_ids() -> Array[int]:
	var out: Array[int] = []
	out.assign(_peer_bits.keys())
	return out


func _refresh_compat_intents() -> void:
	var api := _api()
	if api:
		api.replication._sync_compat.refresh_interest_intents()


func _ensure_peer_bit(peer_id: int) -> int:
	if _peer_bits.has(peer_id):
		return _peer_bits[peer_id]
	var bit := _next_peer_bit
	_next_peer_bit += 1
	_peer_bits[peer_id] = bit
	_bit_peers[bit] = peer_id
	return bit


func _peer_for_bit(bit: int) -> int:
	return int(_bit_peers.get(bit, 0))


## Returns committed edge occupancy for [param layer_id].
func layer_visible_edges(layer_id: StringName) -> int:
	return _engine.layer_edge_count(layer_id)


# Relayed transitions parked in NetwLivenessInterface.when_live for the monitor.
func _liveness_backlog() -> int:
	var api := _api()
	var liveness := api.liveness if api else null
	return liveness.pending_live_count() if liveness else 0


# Authority comes from the API, never the tree. The API answers server offline
# and in a disconnected window, so a unit rig without a peer reads as server.
func _is_server() -> bool:
	var api := _api()
	return api == null or api.is_server()


# Resolves the pending layer exits that govern one materialized peer copy.
func _resolve_leave_decision(entity: NetwEntity, peer_id: int) -> Dictionary:
	var retained: Dictionary = _retained_peers.get(entity, { })
	var pending_by_peer: Dictionary = _pending_leave_layers.get(entity, { })
	var layer_ids: Dictionary = pending_by_peer.get(peer_id, { })
	if layer_ids.is_empty():
		return {
			&"despawn": not bool(retained.get(peer_id, false)),
			&"custom": [],
		}

	var custom_actions: Array = []
	for layer_id: StringName in layer_ids:
		var event_layer := get_layer(layer_id)
		var fallback := LeavePolicy.DESPAWN
		if event_layer:
			fallback = event_layer.default_leave_policy
		var policy := entity.interest._leave_policy_for(layer_id, fallback)
		if policy == LeavePolicy.DESPAWN:
			return { &"despawn": true, &"custom": [] }
		if policy == LeavePolicy.CUSTOM:
			var callback := entity.interest._custom_leave_for(layer_id)
			assert(
				callback.is_valid(),
				"InterestHandle: CUSTOM leave callback became invalid",
			)
			custom_actions.append([callback, layer_id])
	return { &"despawn": false, &"custom": custom_actions }


# Commits a resolved leave effect after the spawn pipeline applies ancestry.
func _commit_leave_decision(
		entity: NetwEntity,
		peer_id: int,
		decision: Dictionary,
		forced_despawn: bool = false,
) -> void:
	_clear_pending_leave(entity, peer_id)
	if forced_despawn or bool(decision.get(&"despawn", true)):
		_clear_retained_peer(entity, peer_id)
		return
	var retained: Dictionary = _retained_peers.get_or_add(entity, { })
	retained[peer_id] = true
	for action: Array in decision.get(&"custom", []):
		var callback := action[0] as Callable
		callback.call(peer_id, action[1])


# Drops unused exit attribution after one spawn reconciliation pass.
func _finish_leave_sweep() -> void:
	_pending_leave_layers.clear()


# Records the layer whose exit may govern the next aggregate wire loss.
func _record_pending_leave(
		entity: NetwEntity,
		peer_id: int,
		layer_id: StringName,
) -> void:
	var per_peer: Dictionary = _pending_leave_layers.get_or_add(entity, { })
	var layers: Dictionary = per_peer.get_or_add(peer_id, { })
	layers[layer_id] = true


# Clears consumed exit attribution for one entity and peer.
func _clear_pending_leave(entity: NetwEntity, peer_id: int) -> void:
	var per_peer: Dictionary = _pending_leave_layers.get(entity, { })
	per_peer.erase(peer_id)
	if per_peer.is_empty():
		_pending_leave_layers.erase(entity)


# Clears a retained materialization when admission resumes or despawn wins.
func _clear_retained_peer(entity: NetwEntity, peer_id: int) -> void:
	var per_peer: Dictionary = _retained_peers.get(entity, { })
	per_peer.erase(peer_id)
	if per_peer.is_empty():
		_retained_peers.erase(entity)


# Reapplies presentation after a local perception configuration change.
func _reapply_local_perception(entity: NetwEntity) -> void:
	if not _perception_visible.has(entity):
		_refresh_local_perception(entity)
		return
	_clear_local_perception(entity, true)
	_refresh_local_perception(entity)


# Reapplies a changed layer default to every locally present member.
func _on_layer_perception_policy_changed(
		changed_layer: NetwInterestLayer,
) -> void:
	for entity: NetwEntity in changed_layer.entities:
		_reapply_local_perception(entity)


# Reconciles every server entity against the local participant row.
func _refresh_all_local_perception() -> void:
	if _local_participant_id() == 0:
		return
	for entity: NetwEntity in _entity_layers:
		_refresh_local_perception(entity)


# Applies one local participant row edge without touching simulation state.
func _refresh_local_perception(
		entity: NetwEntity,
		layer_hints: Array[StringName] = [],
) -> void:
	var peer_id := _local_participant_id()
	if peer_id == 0 or entity == null or not is_instance_valid(entity.owner):
		return
	if _is_server():
		if not _engine.has_entity(entity):
			return
	elif entity.interest.layer_ids().is_empty():
		return
	var visible := participant_sees(peer_id, entity)
	if _perception_visible.get(entity, null) == visible:
		return
	_perception_visible[entity] = visible
	if visible:
		_restore_hidden_presentation(entity)
		_dispatch_custom_perception(entity, true, peer_id)
		return
	var layer_ids := layer_hints
	if layer_ids.is_empty():
		layer_ids = _local_perception_layers(entity, peer_id)
	var hide := false
	var custom_actions: Array = []
	for layer_id: StringName in layer_ids:
		var event_layer := get_layer(layer_id)
		var fallback := PerceptionPolicy.HIDE
		if event_layer:
			fallback = event_layer.default_perception_policy
		var policy := entity.interest._perception_policy_for(
			layer_id,
			fallback,
		)
		if policy == PerceptionPolicy.HIDE:
			hide = true
		elif policy == PerceptionPolicy.CUSTOM:
			var callback := entity.interest._custom_perception_for(layer_id)
			assert(
				callback.is_valid(),
				"InterestHandle: CUSTOM perception callback became invalid",
			)
			custom_actions.append([callback, layer_id])
	if hide:
		_hide_presentation(entity)
	if not custom_actions.is_empty():
		_perception_custom_actions[entity] = custom_actions
		for action: Array in custom_actions:
			var callback := action[0] as Callable
			callback.call(false, peer_id, action[1])


# Chooses the layer edges responsible for the current aggregate local loss.
func _local_perception_layers(
		entity: NetwEntity,
		peer_id: int,
) -> Array[StringName]:
	var pending: Dictionary = _pending_leave_layers.get(entity, { }) \
			.get(peer_id, { })
	if not pending.is_empty():
		var pending_ids: Array[StringName] = []
		pending_ids.assign(pending.keys())
		return pending_ids
	var out: Array[StringName] = []
	if _is_server():
		out.assign(_entity_layers.get(entity, { }).keys())
	else:
		out = entity.interest.layer_ids()
	return out


# Saves and suppresses every presentation-bearing node in the entity subtree.
func _hide_presentation(entity: NetwEntity) -> void:
	if _perception_snapshots.has(entity):
		return
	var snapshot: Array = []
	var stack: Array[Node] = [entity.owner]
	while not stack.is_empty():
		var node := stack.pop_back()
		if node is CanvasItem:
			snapshot.append([node, &"visible", (node as CanvasItem).visible])
			(node as CanvasItem).visible = false
		elif node is Node3D:
			snapshot.append([node, &"visible", (node as Node3D).visible])
			(node as Node3D).visible = false
		if node is AudioStreamPlayer \
				or node is AudioStreamPlayer2D \
				or node is AudioStreamPlayer3D:
			snapshot.append([node, &"volume_db", node.get(&"volume_db")])
			node.set(&"volume_db", -80.0)
		for child: Node in node.get_children():
			stack.append(child)
	_perception_snapshots[entity] = snapshot


# Restores exactly the presentation values captured by the built-in hide.
func _restore_hidden_presentation(entity: NetwEntity) -> void:
	var snapshot: Array = _perception_snapshots.get(entity, [])
	for entry: Array in snapshot:
		var node := entry[0] as Node
		if is_instance_valid(node):
			node.set(entry[1], entry[2])
	_perception_snapshots.erase(entity)


# Fires the matching CUSTOM enter edge and forgets its active actions.
func _dispatch_custom_perception(
		entity: NetwEntity,
		visible: bool,
		peer_id: int,
) -> void:
	var actions: Array = _perception_custom_actions.get(entity, [])
	for action: Array in actions:
		var callback := action[0] as Callable
		if callback.is_valid():
			callback.call(visible, peer_id, action[1])
	_perception_custom_actions.erase(entity)


# Releases local presentation state, optionally restoring the live subtree.
func _clear_local_perception(entity: NetwEntity, restore: bool) -> void:
	if restore:
		_restore_hidden_presentation(entity)
		_dispatch_custom_perception(entity, true, _local_participant_id())
	else:
		_perception_snapshots.erase(entity)
		_perception_custom_actions.erase(entity)
	_perception_visible.erase(entity)


# Returns the local gameplay participant, excluding dedicated authority.
func _local_participant_id() -> int:
	var api := _api()
	if not api:
		return MultiplayerPeer.TARGET_PEER_SERVER
	if not api.is_local_client:
		return 0
	if api.is_listen_server():
		return MultiplayerPeer.TARGET_PEER_SERVER
	if api.has_multiplayer_peer():
		return api.get_unique_id()
	return 0


func _on_layer_interest_enter(
		entity: NetwEntity,
		peer_id: int,
		layer_source: NetwInterestLayer,
) -> void:
	_clear_retained_peer(entity, peer_id)
	_queue_layer_awareness(layer_source, entity, peer_id, Kind.ENTER)
	_queue_observer_awareness(layer_source, entity, peer_id, Kind.ENTER)


func _on_layer_interest_exit(
		entity: NetwEntity,
		peer_id: int,
		layer_source: NetwInterestLayer,
) -> void:
	_record_pending_leave(entity, peer_id, layer_source.layer_id)
	_queue_layer_awareness(layer_source, entity, peer_id, Kind.EXIT)
	_queue_observer_awareness(layer_source, entity, peer_id, Kind.EXIT)


func _queue_layer_awareness(
		event_layer: NetwInterestLayer,
		entity: NetwEntity,
		observer_peer: int,
		kind: int,
) -> void:
	if not _is_server():
		return
	if entity == null or not is_instance_valid(entity.owner):
		return
	if observer_peer == 0 \
			or observer_peer == MultiplayerPeer.TARGET_PEER_SERVER:
		return
	if not entity.owner.is_inside_tree() or not _can_send_to_peer(observer_peer):
		return
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return
	var route := liveness.allocate_route(entity)
	var bucket: Array = _awareness_relay.get_or_add(observer_peer, [])
	bucket.append(
		_AwarenessRelay.new(
			AwarenessType.LAYER,
			route,
			event_layer.layer_id,
			0,
			kind,
		),
	)
	_schedule_visibility_flush()


func _queue_observer_awareness(
		event_layer: NetwInterestLayer,
		entity: NetwEntity,
		observer_peer: int,
		kind: int,
) -> void:
	if not _is_server():
		return
	if entity == null or not is_instance_valid(entity.owner):
		return
	if entity.peer_id == 0 or observer_peer == entity.peer_id:
		return
	if not entity.interest._reports_observers():
		return
	if not entity.owner.is_inside_tree() or not _can_send_to_peer(entity.peer_id):
		return
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return
	var route := liveness.allocate_route(entity)
	var bucket: Array = _awareness_relay.get_or_add(entity.peer_id, [])
	bucket.append(
		_AwarenessRelay.new(
			AwarenessType.OBSERVER,
			route,
			event_layer.layer_id,
			observer_peer,
			kind,
		),
	)
	_schedule_visibility_flush()


func _flush_awareness_relay() -> void:
	if _awareness_relay.is_empty():
		return
	if not _is_server():
		_awareness_relay.clear()
		return
	for target_peer: int in _awareness_relay:
		if not _can_send_to_peer(target_peer):
			continue
		var wire: Array = []
		for event: _AwarenessRelay in _awareness_relay[target_peer]:
			wire.append(event.to_wire())
		if not wire.is_empty():
			_send_events(
				target_peer,
				NetwFrameEnvelope.Channel.INTEREST_AWARENESS,
				wire,
			)
	_awareness_relay.clear()


func _can_send_to_peer(peer_id: int) -> bool:
	if peer_id == 0 or peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return false
	var api := _api()
	if not api or not api.has_multiplayer_peer():
		return false
	var peer := api.multiplayer_peer
	if peer == null or peer is OfflineMultiplayerPeer:
		return false
	if peer.get_connection_status() != MultiplayerPeer.CONNECTION_CONNECTED:
		return false
	if api.is_server():
		return peer_id in api.get_peers()
	return peer_id == MultiplayerPeer.TARGET_PEER_SERVER


# Sends one relay event batch to peer_id as a reliable route-0 carrier frame.
func _send_events(peer_id: int, channel: NetwFrameEnvelope.Channel, wire: Array) -> void:
	var api := _api()
	if not api:
		return
	api.replication.send_to(
		peer_id,
		0,
		channel,
		var_to_bytes(wire),
		true,
	)


# Receives the unified route-addressed awareness projection.
func _handle_awareness_events(payload: PackedByteArray, sender: int) -> void:
	if sender != MultiplayerPeer.TARGET_PEER_SERVER:
		return
	var events = bytes_to_var(payload)
	if typeof(events) != TYPE_ARRAY:
		return
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return
	for raw in events:
		var event := _AwarenessRelay.from_wire(raw)
		if event == null:
			continue
		if event.kind != Kind.ENTER \
				and liveness.route_state(event.route) \
						!= NetwLivenessInterface.State.LIVE:
			continue
		liveness.when_live(
			event.route,
			func():
				_apply_awareness_event(event)
		)


func _apply_awareness_event(event: _AwarenessRelay) -> void:
	var api := _api()
	var liveness := api.liveness if api else null
	if not liveness:
		return
	var entity := liveness.entity_of(event.route)
	if not entity:
		return
	if event.type == AwarenessType.LAYER:
		var event_layer := layer_for(event.layer_id)
		if not event_layer:
			return
		if event.kind == Kind.ENTER:
			event_layer._client_admit(entity)
		else:
			event_layer._client_revoke(entity)
		return
	if event.kind == Kind.ENTER:
		entity.observer_entered.emit(event.layer_id, event.observer_peer)
	else:
		entity.observer_left.emit(event.layer_id, event.observer_peer)
