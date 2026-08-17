## Applies [NetwInterestLayer] state to Godot replication.
##
## One lives under each [MultiplayerTree], owned by [NetwMultiplayer] and
## reached through the session's layer verbs. [NetwInterestEngine] owns the
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
class_name InterestCore
extends RefCounted

# The settle keys this interface schedules under. A key coalesces, so a cascade
# of mutations in one pump produces one flush, and naming them here keeps the
# scheduling site and the withdrawing site spelling the same thing.
const VISIBILITY_SETTLE_KEY := &"interest_visibility"
const COMPAT_INTENT_SETTLE_KEY := &"interest_compat_intents"

var _layers: Dictionary[StringName, NetwInterestLayer] = { }
var _leave := NetwInterestLeave.new()
var _perception := NetwInterestPerception.new()
var _perception_snapshots: Dictionary[NetwEntity, Array] = { }
var _engine: NetwInterestEngine
var _pending_delta: NetwInterestDelta

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


## Fluent declarative builder returned by [method Netw.configure_interest].
class InterestConfig:
	extends RefCounted

	var _handle: NetwInterestHandle
	var _layers: Array[StringName] = []


	func _init(handle: NetwInterestHandle) -> void:
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


var _awareness_relay := NetwInterestRelay.new()

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef

# The session's own plane, held directly rather than reached through the shell,
# because every verb below it is one the shell only relays.
var session: NetwMultiplayerCore


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null
	session = api._native_core if api else null
	_engine = (
		session.interest_engine if session else NetwInterestEngine.new()
	)
	if api:
		api._connect_once(api.peer_connected, _on_peer_connected)
		api._connect_once(api.peer_disconnected, _on_peer_disconnected)
		api._connect_once(api.session_ended, _on_session_ended)
		api._replication.register_protocol(
			NetwFrameEnvelope.Channel.INTEREST_AWARENESS,
			_handle_awareness_events,
		)


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null


# The session anchor node for path addressing: the owning MultiplayerTree under a
# subpath install, /root under a root install. Both resolve the same relative
# paths every peer built symmetrically.
func _anchor() -> Node:
	var api := _api()
	return api.root if api else null


func _on_peer_connected(peer_id: int) -> void:
	_engine.peer_bit_for(peer_id)
	_sync_live_peers()
	if session:
		session.settle_schedule(
			_refresh_compat_intents, COMPAT_INTENT_SETTLE_KEY
		)
	_schedule_visibility_flush()


func _on_peer_disconnected(peer_id: int) -> void:
	_awareness_relay.forget(peer_id)
	_leave.forget_peer(peer_id)
	_sync_live_peers()
	_schedule_visibility_flush()


# Defers the reset so it runs after any scene despawn driven by the same
# session_ended emission has drained entity state through the normal
# tree_exiting path. Clearing the layers and admit counters mid-despawn would
# desync them and trip the underflow assert. Deferring makes the reset
# independent of the order [SceneManager] and this interface handle the signal.
#
# TODO: move this onto the settle queue once the queue can state that this row
# runs after the despawn rows. FIFO is not enough on its own: the order the
# session_ended handlers are called decides which is enqueued first, and a
# despawn cascade that schedules from inside a drain lands a pass later than a
# clear enqueued before it.
func _on_session_ended() -> void:
	_clear_session_state.call_deferred()


# Drops every per-session entry so a same-layer second session starts clean.
func _clear_session_state() -> void:
	_layers.clear()
	_leave.clear()
	var perceived_entities: Array[NetwEntity] = []
	perceived_entities.assign(_perception_snapshots.keys())
	for entity: NetwEntity in perceived_entities:
		_restore_hidden_presentation(entity)
	for key: int in _perception.armed_keys():
		_dispatch_custom_perception(key, true, _local_participant_id())
	_perception.clear()
	_perception_snapshots.clear()
	_awareness_relay.clear()
	_reset_engine()
	if session:
		session.settle_cancel(VISIBILITY_SETTLE_KEY)
	_pending_delta = null


# The session owns the engine, so a reset asks the owner for one rather than
# swapping in an instance the session would not be holding.
func _reset_engine() -> void:
	if session:
		session.reset_interest()
	else:
		_engine.clear()


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
	if session == null:
		return
	var wanted: Dictionary[StringName, bool] = { }
	for id: StringName in layer_ids:
		wanted[id] = true
	for candidate: NetwEntity in session.liveness_live_entities():
		if candidate == entity \
				or not is_instance_valid(candidate) \
				or not is_instance_valid(candidate.owner):
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
	out.assign(_engine.memberships(_entity_slot(entity)))
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
	for slot: int in _engine.co_members(_entity_slot(entity), layer_ids):
		var candidate := _entity_for_slot(slot)
		if candidate and is_instance_valid(candidate) \
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
##   ┠╴ transitions_total: int  # summed per-layer churn since creation
##   ┖╴ vanished_dirty_skips: int # keys removed before recompute
## }
## [/codeblock]
func monitor_snapshot() -> Dictionary:
	var visible_edges := _engine.stats().edges
	var transitions_total := _engine.transitions_total()
	return {
		&"layers": _layers.size(),
		&"entities_filtered": _engine.membership_keys().size(),
		&"visible_edges": visible_edges,
		&"dirty_entities": _engine.dirty_count(),
		&"relay_backlog": _liveness_backlog(),
		&"transitions_total": transitions_total,
		&"vanished_dirty_skips": _engine.stats().vanished_dirty_skips,
	}


## Returns the committed peer admits for [param entity].
func committed_admits(entity: NetwEntity) -> Dictionary:
	var out: Dictionary = { }
	var row := _engine.row_of(_entity_slot(entity))
	for bit in NetwInterestBitSet.bits(row):
		var peer_id := _engine.peer_of_bit(bit)
		if peer_id != 0:
			out[peer_id] = 1
	return out


## Returns [param entity]'s committed row as packed peer-bit words.
##
## An entity the matrix has never held answers an empty row rather than a
## zero-filled one, so "admits nobody" and "not registered" stay distinct.
func committed_row(entity: NetwEntity) -> PackedInt64Array:
	return _engine.row_of(_entity_slot(entity))


## Returns whether [param entity]'s committed row admits [param peer_bit].
##
## [param peer_bit] is a dense viewer index rather than a peer id. An entity the
## matrix has never held admits nobody, which fails closed.
func bit_admits(entity: NetwEntity, peer_bit: int) -> bool:
	return _engine.test(_entity_slot(entity), peer_bit)


## Names the first term denying [param peer_bit] to [param entity].
func explain_bit(entity: NetwEntity, peer_bit: int) -> String:
	return _engine.explain(_entity_slot(entity), peer_bit)


## Returns [code]true[/code] if [param entity] has interest memberships.
func has_filter(entity: NetwEntity) -> bool:
	if not _is_server() and entity:
		return not entity.interest.layer_ids().is_empty()
	return _engine.has_memberships(_entity_slot(entity))


## Returns whether [param entity] has intent folded into its committed row.
func has_committed_intent(entity: NetwEntity) -> bool:
	return _engine.had_committed_intent(_entity_slot(entity))


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
	var bit := _engine.peer_bit_of(peer_id)
	if bit < 0:
		return false
	return _engine.test(_entity_slot(entity), bit)


func _on_layer_policy_changed(changed_layer: NetwInterestLayer) -> void:
	_mark_layer_dirty(changed_layer)


func _on_layer_viewer_changed(
		changed_layer: NetwInterestLayer,
		_peer_id: int,
		_added: bool,
) -> void:
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
#
# Visibility already follows ancestry through the engine's parent clamp, so this
# exists only to populate the scene layer's entity set, which diagnostics and
# the scene surface both read. It resolves the scene through the entity facet
# walk rather than a node type, so core never names a scene class.
func _sync_scene_membership(entity: NetwEntity) -> void:
	if not _is_server() or entity == null \
			or not is_instance_valid(entity.owner):
		return
	var api := _api()
	if api == null:
		return
	var slot := _ensure_entity_slot(entity)
	var previous := _engine.scene_membership(slot)
	var current := _scene_layer_id_for(api, entity)
	if not _engine.set_scene_membership(slot, current):
		return
	if not previous.is_empty():
		var previous_layer := get_layer(previous)
		if previous_layer:
			previous_layer.remove_entity(entity)
	if not current.is_empty():
		layer(current).add_entity(entity)


# The layer id of the scene containing [param entity], resolved through the
# entity facet walk and the scene's own layer handle. Empty when no scene
# encloses the entity.
func _scene_layer_id_for(api: NetwMultiplayer, entity: NetwEntity) -> StringName:
	var scene := api.scene_of(api.entity_of(entity.owner))
	return api._scene_layer_id(scene) if scene.is_valid() else &""


func _track_entity_layer(entity: NetwEntity, layer_id: StringName) -> void:
	_engine.membership_add(_ensure_entity_slot(entity), layer_id)


func _untrack_entity_layer(entity: NetwEntity, layer_id: StringName) -> void:
	_engine.membership_remove(_entity_slot(entity), layer_id)


func _track_entity_lifecycle(entity: NetwEntity) -> void:
	var handler := _on_entity_tree_exiting.bind(entity)
	if not _engine.set_exit_handler(_ensure_entity_slot(entity), handler):
		return
	var api := _api()
	if api:
		api._connect_once(entity.owner.tree_exiting, handler)


func _untrack_entity_lifecycle(entity: NetwEntity) -> void:
	var handler := _engine.take_exit_handler(_entity_slot(entity))
	if handler.is_valid() and is_instance_valid(entity) \
			and is_instance_valid(entity.owner) \
			and entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.disconnect(handler)


func _on_entity_tree_exiting(entity: NetwEntity) -> void:
	for layer_id: StringName in _engine.memberships(_entity_slot(entity)):
		var exiting_layer := get_layer(layer_id)
		if not exiting_layer:
			continue
		if _is_server():
			exiting_layer.remove_entity(entity)
		else:
			exiting_layer._client_untrack_entity(entity)
	_untrack_entity_lifecycle(entity)
	_retire_entity(entity)
	_leave.forget_entity(_entity_slot(entity))
	_engine.set_scene_membership(_entity_slot(entity), &"")
	_clear_local_perception(entity, false)


func _mark_layer_dirty(_dirty_layer: NetwInterestLayer) -> void:
	_schedule_visibility_flush()


func _mark_entity_dirty(entity: NetwEntity) -> void:
	assert(
		entity != null,
		"InterestCore: _mark_entity_dirty called with null entity",
	)
	_schedule_visibility_flush()


# Keyed, so a cascade of mutations inside one pump settles once and settles
# after the whole cascade. The key IS the coalescing, which is why this no
# longer carries a scheduled flag of its own.
func _schedule_visibility_flush() -> void:
	if session:
		session.settle_schedule(_flush_visibility, VISIBILITY_SETTLE_KEY)


## Flushes the committed matrix and its awareness projection.
func flush() -> Error:
	var api := _api()
	# A caller that flushed on the spot has nothing left to settle.
	if api:
		api._settle_cancel(VISIBILITY_SETTLE_KEY)
	var verdict := api._interest_recompute() \
	if api else _engine_recompute()
	if verdict != OK:
		return verdict
	if api:
		api._interest_commit()
	else:
		_engine_commit()
	_flush_awareness_relay()
	# Admission changes drive per-peer spawn/despawn through the spawn book.
	if api and api.has_multiplayer_peer() \
			and api.multiplayer_peer.get_connection_status() \
					!= MultiplayerPeer.CONNECTION_DISCONNECTED:
		api._replication._spawn_pipeline.schedule_visibility_sweep()
	return OK


## Flushes pending mutations immediately.
##
## Normal gameplay observes mutations at the next pump's settle, which is at
## most one frame away because the session polls every frame. Use this method
## before an in-frame spawn that needs the new row, and in a test that would
## rather not pump.
func flush_now() -> void:
	flush()


func _flush_visibility() -> void:
	var api := _api()
	if api:
		api._sink_verdict(api.interest_flush(), 0)


# Computes the pending interest delta without mutating committed rows.
func _engine_recompute() -> Error:
	if not _is_server():
		_pending_delta = null
		return OK
	_sync_live_peers()
	var entities: Dictionary[NetwEntity, bool] = { }
	for slot: int in _engine.membership_keys():
		var member := _entity_for_slot(slot)
		if member:
			entities[member] = true
	for slot: int in _engine.intent_keys():
		var holder := _entity_for_slot(slot)
		if holder:
			entities[holder] = true
	for entity: NetwEntity in entities:
		if is_instance_valid(entity) and is_instance_valid(entity.owner):
			_sync_engine_entity(entity)
	_pending_delta = _engine.recompute()
	return OK


# Applies and commits the delta produced by [method _engine_recompute].
func _engine_commit() -> void:
	if _pending_delta == null:
		return
	_apply_engine_delta(_pending_delta)
	_engine.commit(_pending_delta)
	if session:
		session.wrapper_sweep_retired()
	_refresh_all_local_perception()
	_pending_delta = null


func _apply_engine_delta(delta: NetwInterestDelta) -> void:
	for transition: Array in delta.layer_hides:
		_apply_layer_delta_transition(transition, false)
	for transition: Array in delta.layer_shows:
		_apply_layer_delta_transition(transition, true)
	for transition: Array in delta.shows:
		var entity := _entity_for_slot(transition[0])
		if entity:
			_leave.release(
				_entity_slot(entity),
				_engine.peer_of_bit(transition[1]),
			)


func _apply_layer_delta_transition(
		transition: Array,
		visible: bool,
) -> void:
	var layer_id: StringName = transition[0]
	var entity := _entity_for_slot(transition[1])
	var peer_id := _engine.peer_of_bit(transition[2])
	var event_layer := get_layer(layer_id)
	if not event_layer or not entity or peer_id == 0:
		return
	event_layer._apply_server_transition(entity, peer_id, visible)
	if visible:
		_on_layer_interest_enter(entity, peer_id, event_layer)
	else:
		_on_layer_interest_exit(entity, peer_id, event_layer)


func _client_projection_admits(entity: NetwEntity) -> bool:
	return _engine.projection_admits(
		_entity_slot(entity),
		entity.interest._decl,
	)


func _sync_live_peers() -> void:
	var peer_ids: Dictionary[int, bool] = { }
	var api := _api()
	if api and api.has_multiplayer_peer():
		for peer_id: int in api.get_peers():
			peer_ids[peer_id] = true
	if api and api.role == NetwMultiplayer.Role.LISTEN_SERVER:
		peer_ids[MultiplayerPeer.TARGET_PEER_SERVER] = true
	for peer_id: int in _engine.viewer_peers():
		peer_ids[peer_id] = true
	var live_bits := PackedInt64Array()
	var added_peer := false
	for peer_id in peer_ids:
		added_peer = added_peer or _engine.peer_bit_of(peer_id) < 0
		var bit := _engine.peer_bit_for(peer_id)
		live_bits = NetwInterestBitSet.with_bit(live_bits, bit)
	_engine.set_live_peers(live_bits)
	if added_peer and api:
		api._replication._sync_compat.refresh_interest_intents()


func _sync_engine_entity(entity: NetwEntity) -> void:
	var slot := _entity_slot(entity)
	if not _engine.has_memberships(slot) and not _engine.has_intent(slot):
		_retire_entity(entity)
		return
	_sync_engine_entity_record(entity)


func _sync_engine_entity_record(entity: NetwEntity) -> void:
	if session and entity:
		session.interest_sync_record(entity)


func _set_entity_intent(
		entity: NetwEntity,
		admitted_peers: Array[int],
) -> void:
	if entity == null:
		return
	_track_entity_lifecycle(entity)
	_sync_engine_entity_record(entity)
	var row := PackedInt64Array()
	for peer_id in admitted_peers:
		var bit := _engine.peer_bit_for(peer_id)
		row = NetwInterestBitSet.with_bit(row, bit)
	_engine.set_intent(_ensure_entity_slot(entity), row)
	_mark_entity_dirty(entity)


func _clear_entity_intent(entity: NetwEntity) -> void:
	if _engine.has_memberships(_entity_slot(entity)):
		_engine.set_intent_all(_ensure_entity_slot(entity))
		_mark_entity_dirty(entity)
	else:
		_retire_entity(entity)


func _known_peer_ids() -> Array[int]:
	var out: Array[int] = []
	out.assign(_engine.known_peers())
	return out


func _refresh_compat_intents() -> void:
	var api := _api()
	if api:
		api._replication._sync_compat.refresh_interest_intents()


# The key an entity is known to the engine by, which is its handle's integer
# form. The engine keys on plain integers so a verdict never depends on an
# object staying alive, and handles are never reissued, so a late row can only
# resolve to the entity that earned it or to nothing.
func _ensure_entity_slot(entity: NetwEntity) -> int:
	if entity == null:
		return 0
	if session:
		session.liveness_adopt(entity)
	return entity.rid.get_id()


# The key an entity already answers to. Reads take this door, which registers
# nothing, so asking about an entity the engine never saw does not enrol it.
func _entity_slot(entity: NetwEntity) -> int:
	return entity.rid.get_id() if entity else 0


func _entity_for_slot(slot: int) -> NetwEntity:
	return session.wrapper_for_id(slot) as NetwEntity if session else null


# Drops one entity from the engine. The wrapper it names outlives the removal
# on purpose: a removed entity's last act is a hide to every peer that held it,
# and those transitions arrive in the NEXT delta naming its key, so a leave
# policy would otherwise have nothing to run against. The commit that applies
# that delta is what sweeps the wrapper.
func _retire_entity(entity: NetwEntity) -> void:
	var slot := _entity_slot(entity)
	if slot == 0 or not _engine.has_entity(slot):
		return
	_engine.remove_entity(slot)


## Returns committed edge occupancy for [param layer_id].
func layer_visible_edges(layer_id: StringName) -> int:
	return _engine.layer_edge_count(layer_id)


## Drops [param layer_id]'s admit row, so members stop being granted by it.
##
## A membership naming a layer that is gone contributes nothing, which is not
## the same as that layer admitting everyone.
func forget_layer_row(layer_id: StringName) -> void:
	_engine.remove_layer(layer_id)


# Relayed transitions parked in NetwMultiplayerCore.when_live for the monitor.
func _liveness_backlog() -> int:
	return session.liveness_pending_live_count() if session else 0


# Authority comes from the API, never the tree. The API answers server offline
# and in a disconnected window, so a unit rig without a peer reads as server.
func _is_server() -> bool:
	var api := _api()
	return api == null or api.is_server()


# Resolves the pending layer exits that govern one materialized peer copy.
func _resolve_leave_decision(entity: NetwEntity, peer_id: int) -> Dictionary:
	return _leave.resolve(
		_entity_slot(entity),
		peer_id,
		entity.interest._decl,
		_engine,
	)


# Commits a resolved leave effect after the spawn pipeline applies ancestry.
func _commit_leave_decision(
		entity: NetwEntity,
		peer_id: int,
		decision: Dictionary,
		forced_despawn: bool = false,
) -> void:
	_leave.commit(_entity_slot(entity), peer_id, decision, forced_despawn)


# Drops unused exit attribution after one spawn reconciliation pass.
func _finish_leave_sweep() -> void:
	_leave.finish_sweep()


# Reapplies presentation after a local perception configuration change.
func _reapply_local_perception(entity: NetwEntity) -> void:
	if not _perception.is_known(_entity_slot(entity)):
		_refresh_local_perception(entity)
		return
	_clear_local_perception(entity, true)
	_refresh_local_perception(entity)


# Reapplies a changed layer default to every locally present member.
func _on_layer_perception_policy_changed(
		changed_layer: NetwInterestLayer,
) -> void:
	for slot: int in _engine.roster(changed_layer.layer_id):
		var member := _entity_for_slot(slot)
		if member:
			_reapply_local_perception(member)


# Reconciles every server entity against the local participant row.
func _refresh_all_local_perception() -> void:
	if _local_participant_id() == 0:
		return
	for slot: int in _engine.membership_keys():
		var member := _entity_for_slot(slot)
		if member:
			_refresh_local_perception(member)


# Applies one local participant row edge without touching simulation state.
func _refresh_local_perception(
		entity: NetwEntity,
		layer_hints: Array[StringName] = [],
) -> void:
	var peer_id := _local_participant_id()
	if peer_id == 0 or entity == null or not is_instance_valid(entity.owner):
		return
	if _is_server():
		if not _engine.has_entity(_entity_slot(entity)):
			return
	elif entity.interest.layer_ids().is_empty():
		return
	var visible := participant_sees(peer_id, entity)
	var slot := _entity_slot(entity)
	if not _perception.set_visible(slot, visible):
		return
	if visible:
		_restore_hidden_presentation(entity)
		_dispatch_custom_perception(slot, true, peer_id)
		return
	var layer_ids := layer_hints
	if layer_ids.is_empty():
		layer_ids = _local_perception_layers(entity, peer_id)
	var verdict := _perception.resolve(layer_ids, entity.interest._decl, _engine)
	if bool(verdict[&"hide"]):
		_hide_presentation(entity)
	var custom_actions: Array = verdict[&"custom"]
	_perception.arm(slot, custom_actions)
	for action: Array in custom_actions:
		var callback := action[0] as Callable
		callback.call(false, peer_id, action[1])


# Chooses the layer edges responsible for the current aggregate local loss.
func _local_perception_layers(
		entity: NetwEntity,
		peer_id: int,
) -> Array[StringName]:
	var pending := _leave.pending_layers(_entity_slot(entity), peer_id)
	if not pending.is_empty():
		var pending_ids: Array[StringName] = []
		pending_ids.assign(pending)
		return pending_ids
	var out: Array[StringName] = []
	if _is_server():
		out.assign(_engine.memberships(_entity_slot(entity)))
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
		key: int,
		visible: bool,
		peer_id: int,
) -> void:
	for action: Array in _perception.disarm(key):
		var callback := action[0] as Callable
		if callback.is_valid():
			callback.call(visible, peer_id, action[1])


# Releases local presentation state, optionally restoring the live subtree.
func _clear_local_perception(entity: NetwEntity, restore: bool) -> void:
	var slot := _entity_slot(entity)
	if restore:
		_restore_hidden_presentation(entity)
		_dispatch_custom_perception(slot, true, _local_participant_id())
	else:
		_perception_snapshots.erase(entity)
		_perception.disarm(slot)
	_perception.forget(slot)


# Returns the local gameplay participant, excluding dedicated authority.
func _local_participant_id() -> int:
	var api := _api()
	if not api:
		return MultiplayerPeer.TARGET_PEER_SERVER
	if not api.is_local_client:
		return 0
	if api.role == NetwMultiplayer.Role.LISTEN_SERVER:
		return MultiplayerPeer.TARGET_PEER_SERVER
	if api.has_multiplayer_peer():
		return api.get_unique_id()
	return 0


func _on_layer_interest_enter(
		entity: NetwEntity,
		peer_id: int,
		layer_source: NetwInterestLayer,
) -> void:
	_leave.release(_entity_slot(entity), peer_id)
	_queue_layer_awareness(layer_source, entity, peer_id, Kind.ENTER)
	_queue_observer_awareness(layer_source, entity, peer_id, Kind.ENTER)


func _on_layer_interest_exit(
		entity: NetwEntity,
		peer_id: int,
		layer_source: NetwInterestLayer,
) -> void:
	_leave.record(_entity_slot(entity), peer_id, layer_source.layer_id)
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
	if not session:
		return
	var route := session.liveness_allocate_route(entity)
	_awareness_relay.append(
		observer_peer,
		NetwInterestAwareness.layer_edge(route, event_layer.layer_id, kind),
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
	if not session:
		return
	var route := session.liveness_allocate_route(entity)
	_awareness_relay.append(
		entity.peer_id,
		NetwInterestAwareness.observer_edge(
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
	for target_peer: int in _awareness_relay.targets():
		if not _can_send_to_peer(target_peer):
			continue
		var wire := _awareness_relay.wire_for(target_peer)
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
	api._replication.send_to(
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
	if not api or not session:
		return
	for raw: Variant in events:
		var event := NetwInterestAwareness.from_array(raw)
		if event == null:
			continue
		if event.get_kind() != Kind.ENTER \
				and session.liveness_route_state(event.get_route()) \
						!= NetwLivenessCore.STATE_LIVE:
			continue
		api.when_live(
			event.get_route(),
			func():
				_apply_awareness_event(event)
		)


func _apply_awareness_event(event: NetwInterestAwareness) -> void:
	var api := _api()
	if not api or not session:
		return
	var entity := session.wrapper_for_route(event.get_route()) as NetwEntity
	if not entity:
		return
	var entered := event.get_kind() == Kind.ENTER
	if event.get_edge_type() == NetwInterestAwareness.LAYER:
		var event_layer := layer_for(event.get_layer_id())
		if not event_layer:
			return
		_report_edge(api, event, entity, entered, true)
		if entered:
			event_layer._client_admit(entity)
		else:
			event_layer._client_revoke(entity)
		return
	_report_edge(api, event, entity, entered, false)
	if entered:
		entity.observer_entered.emit(
			event.get_layer_id(),
			event.get_observer_peer(),
		)
	else:
		entity.observer_left.emit(
			event.get_layer_id(),
			event.get_observer_peer(),
		)


# Reports one awareness edge to the event plane. A layer edge is the entity
# moving in or out of a layer; an observer edge is one peer beginning or
# ceasing to see it, so the two are separate taxonomy values rather than one
# with a flag.
func _report_edge(
		api: NetwMultiplayer,
		event: NetwInterestAwareness,
		entity: NetwEntity,
		entered: bool,
		layer_edge: bool,
) -> void:
	var value := NetwMultiplayerCore.INTEREST_ENTER if entered \
			else NetwMultiplayerCore.INTEREST_EXIT
	if not layer_edge:
		value = NetwMultiplayerCore.OBSERVER_ENTERED if entered \
				else NetwMultiplayerCore.OBSERVER_LEFT
	api.report_event(
		value,
		event.get_route(),
		{ layer = event.get_layer_id() },
		event.get_observer_peer(),
		entity.entity_id,
	)
