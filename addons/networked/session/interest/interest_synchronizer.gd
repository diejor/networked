## Anchored interest layer expressed as a [MultiplayerSynchronizer].
##
## One node owns one layer of the interest model: the [member viewers]
## set (peers that participate in the layer), the [member entities] set
## (entities whose synchronizers this anchor gates), and the [enum
## Policy] that composes the two into a per-peer verdict.
##
## [b]Engine effects[/b] go through [InterestBinding]: it installs
## per-entity visibility filters and admits peers at the anchor via
## [method MultiplayerSynchronizer.set_visibility_for]. Re-evaluation
## is engine-driven - [signal MultiplayerSynchronizer.delta_synchronized]
## fires every replication tick and calls [method
## MultiplayerSynchronizer.update_visibility] with no argument on every
## tracked sync. This mirrors the contract proven out by the legacy
## [SceneSynchronizer].
##
## [b]Transition signals[/b] - [signal interest_enter] / [signal
## interest_exit] - are emitted from a separate [InterestDriver] pass.
## The driver computes per-(entity, peer) transitions; it does not
## drive engine effects.
##
## [codeblock]
##     var arena := %InterestSynchronizer
##     arena.layer_id = &"arena:1"
##     arena.policy = InterestSynchronizer.Policy.HIDE_FROM_OUTSIDERS
##     arena.add_viewer(player.peer_id)
## [/codeblock]
class_name InterestSynchronizer
extends MultiplayerSynchronizer


## Composition rule for the per-peer verdict.
enum Policy {
	## Peers in [member viewers] see [member entities]; outsiders do
	## not. Default; matches today's [SceneSynchronizer] behavior.
	HIDE_FROM_OUTSIDERS,
	## Peers in [member viewers] do [b]not[/b] see [member entities];
	## outsiders do. Use for stealth-style bubbles.
	HIDE_FROM_INSIDERS,
}


## Anchor-gating strategy. See [enum InterestBinding.AnchorStrategy].
@export var anchor_strategy: InterestBinding.AnchorStrategy = \
		InterestBinding.AnchorStrategy.OPEN

## Stable identifier for the layer this synchronizer anchors. Used by
## [NetwInterest] to look up the anchor from [NetwEntity] members.
@export var layer_id: StringName

## Composition policy. See [enum Policy]. Mutating triggers a refresh.
@export var policy: Policy = Policy.HIDE_FROM_OUTSIDERS:
	set(value):
		var changed := value != policy
		policy = value
		if changed and _initial_sync_done:
			Netw.dbg.trace(
					"IS[%s] policy changed -> %d", [layer_id, value])
			_sync_layer_policy()
			_schedule_refresh()

## Peer IDs participating in this layer. Spawn-synced
## ([code]REPLICATION_MODE_ON_CHANGE[/code]) so the viewer set travels
## with the spawn packet to anyone allowed to see the anchor.
@export var viewers: Dictionary[int, bool] = {}:
	set(value):
		var prev_keys := viewers.keys()
		viewers = value
		if not _initial_sync_done:
			return
		var added: Array[int] = []
		var removed: Array[int] = []
		for p: int in viewers:
			if p not in prev_keys:
				added.append(p)
		for p: int in prev_keys:
			if not viewers.has(p):
				removed.append(p)
		for p in added:
			Netw.dbg.trace(
					"IS[%s] viewer added (spawn-sync) %d",
					[layer_id, p])
			viewer_added.emit(p)
		for p in removed:
			Netw.dbg.trace(
					"IS[%s] viewer removed (spawn-sync) %d",
					[layer_id, p])
			viewer_removed.emit(p)
		_sync_layer_viewers()
		if added.size() > 0 or removed.size() > 0:
			_schedule_refresh()


## Emitted on the server for each per-peer visibility transition
## where [param entity] became visible to [param peer_id]. On the
## client, fires only for the local peer.
signal interest_enter(entity: NetwEntity, peer_id: int)

## Emitted before [param entity] is hidden from [param peer_id].
## Listeners read the entity's last-known state. Same scoping as
## [signal interest_enter].
signal interest_exit(entity: NetwEntity, peer_id: int)

## Emitted when a peer is added to [member viewers].
signal viewer_added(peer_id: int)

## Emitted when a peer is removed from [member viewers].
signal viewer_removed(peer_id: int)

## Emitted when an entity is enrolled under this anchor.
signal entity_added(entity: NetwEntity)

## Emitted when an entity is unenrolled.
signal entity_removed(entity: NetwEntity)


## Entities whose synchronizers this anchor gates. Mutated through
## [method add_entity] / [method remove_entity].
var entities: Dictionary[NetwEntity, bool] = {}

## Per-(entity, peer) cache used for [signal interest_enter] /
## [signal interest_exit] emission. Does not drive engine effects.
var driver: InterestDriver = InterestDriver.new()

## Engine-touching binding. Public so debug code can call
## [code]binding.dump_state(...)[/code].
var binding: InterestBinding

var _entity_exit_handlers: Dictionary = {}
var _layer: NetwInterestLayer
var _config_built: bool = false
var _initial_sync_done: bool = false
var _refresh_scheduled: bool = false
var _registered_with_interest: bool = false

func _init() -> void:
	unique_name_in_owner = true

func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		_build_replication_config()


func _enter_tree() -> void:
	_register_with_interest()
	_initial_sync_done = true


func _exit_tree() -> void:
	_unregister_with_interest()


func _ready() -> void:
	binding = InterestBinding.new(self, anchor_strategy)
	binding.install_anchor()


# ---------------------------------------------------------------------------
# Viewer API (server-only mutators).
# ---------------------------------------------------------------------------

## Adds [param peer_id] to [member viewers]. Idempotent; rejects
## [code]0[/code]. Server-only.
##
## Anchor admission ([code]set_visibility_for(peer, true)[/code]) runs
## first so the spawn-sync of [member viewers] cannot reach the peer
## before the wrapper itself is admitted; the entity filter refresh
## follows synchronously.
func add_viewer(peer_id: int) -> void:
	if not _is_server():
		return
	if peer_id == 0:
		return
	if viewers.has(peer_id):
		return
	if binding:
		binding.admit(peer_id)
	viewers[peer_id] = true
	_add_layer_viewer(peer_id)
	Netw.dbg.trace("IS[%s] add_viewer %d", [layer_id, peer_id])
	viewer_added.emit(peer_id)
	_refresh_now()


## Removes [param peer_id] from [member viewers]. Idempotent.
## Server-only.
##
## Entity hides run synchronously this frame; the anchor hide is
## deferred by the binding so the wrapper teardown lands after any
## per-entity despawns the engine still has in flight (godot issue
## #68508).
func remove_viewer(peer_id: int) -> void:
	if not _is_server():
		return
	if not viewers.has(peer_id):
		return
	viewers.erase(peer_id)
	_remove_layer_viewer(peer_id)
	Netw.dbg.trace("IS[%s] remove_viewer %d", [layer_id, peer_id])
	viewer_removed.emit(peer_id)
	_refresh_now()
	if binding:
		binding.unadmit(peer_id)


## Returns [code]true[/code] if [param peer_id] is currently a viewer.
func has_viewer(peer_id: int) -> bool:
	return viewers.has(peer_id)


## Snapshot of current viewer peer ids.
func viewer_ids() -> Array[int]:
	var out: Array[int] = []
	out.assign(viewers.keys())
	return out


# ---------------------------------------------------------------------------
# Entity API. Every peer installs local filters, updates the local
# [member entities] dict, and runs the driver to fire signals.
# ---------------------------------------------------------------------------

## Enrolls [param entity] under this anchor. Installs the entity-side
## visibility filter on every [MultiplayerSynchronizer] owned by
## [param entity], records it locally, and hooks
## [signal Node.tree_exiting] for automatic cleanup. Idempotent.
func add_entity(entity: NetwEntity) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
	if entities.has(entity):
		return
	entities[entity] = true

	if _uses_interest_service():
		_interest().register_entity_for_layer(layer_id, entity)
	elif _layer:
		_layer.add_entity(entity)

	if binding and not _uses_interest_service():
		binding.install_entity(entity, _make_entity_filter(entity))

	var handler := _on_entity_tree_exiting.bind(entity)
	_entity_exit_handlers[entity] = handler
	if not entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.connect(handler)

	Netw.dbg.trace(
			"IS[%s] add_entity %s (in_tree=%s)",
			[layer_id, entity.owner.name,
			entity.owner.is_inside_tree()])
	entity_added.emit(entity)
	_refresh_now()


## Removes [param entity] from this anchor. Emits [signal interest_exit]
## for every peer that previously saw it, detaches the filter, and
## clears the cleanup hook. Idempotent.
func remove_entity(entity: NetwEntity) -> void:
	if entity == null:
		return
	if not entities.has(entity):
		return

	var prev_view := driver.cached_view_for(entity)
	for peer_id: int in prev_view:
		if prev_view[peer_id]:
			interest_exit.emit(entity, peer_id)
			entity.interest_exit.emit(peer_id)
	driver.forget(entity)

	entities.erase(entity)

	if _uses_interest_service():
		_interest().unregister_entity_from_layer(layer_id, entity)
	elif _layer:
		_layer.remove_entity(entity)

	if binding and not _uses_interest_service():
		binding.uninstall_entity(entity)

	var handler: Callable = _entity_exit_handlers.get(entity, Callable())
	if handler.is_valid() and is_instance_valid(entity) \
			and is_instance_valid(entity.owner) \
			and entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.disconnect(handler)
	_entity_exit_handlers.erase(entity)

	Netw.dbg.trace("IS[%s] remove_entity", [layer_id])
	entity_removed.emit(entity)


## Returns [code]true[/code] if [param entity] is currently enrolled.
func has_entity(entity: NetwEntity) -> bool:
	return entities.has(entity)


# ---------------------------------------------------------------------------
# Queries.
# ---------------------------------------------------------------------------

## Returns the cached per-peer verdict for [param entity] under
## [param peer_id]. On the server, valid for every live peer; on the
## client, valid only for the local peer.
func is_visible_to(entity: NetwEntity, peer_id: int) -> bool:
	return driver.cached_verdict(entity, peer_id)


# ---------------------------------------------------------------------------
# Convenience.
# ---------------------------------------------------------------------------

## Adds [param entity] as both an entity of this layer and a viewer
## (using [member NetwEntity.peer_id]). For peer-owned entities the
## peer becomes a viewer; for server-owned entities (peer_id == 0)
## only the enrollment side runs.
func admit(entity: NetwEntity) -> void:
	add_entity(entity)
	if entity != null and entity.peer_id != 0:
		add_viewer(entity.peer_id)


## Reverse of [method admit].
func dismiss(entity: NetwEntity) -> void:
	if entity != null and entity.peer_id != 0:
		remove_viewer(entity.peer_id)
	remove_entity(entity)


# ---------------------------------------------------------------------------
# Debugging.
# ---------------------------------------------------------------------------

## Returns a structured snapshot of every layer subsystem for
## [param peer_id].
func debug_dump(peer_id: int = 0) -> Dictionary:
	var verdict := InterestPolicy.verdict(policy, viewers, peer_id)
	var explanation := InterestPolicy.explain(
			policy, viewers, peer_id)
	var binding_state: Dictionary
	if binding:
		binding_state = binding.dump_state(policy, viewers, peer_id)
	else:
		binding_state = {}
	return {
		"layer_id": String(layer_id),
		"policy": policy,
		"viewers": viewer_ids(),
		"peer_id": peer_id,
		"verdict": verdict,
		"explanation": explanation,
		"driver_cache": driver.dump(),
		"binding": binding_state,
	}


# ---------------------------------------------------------------------------
# Refresh.
# ---------------------------------------------------------------------------

## Synchronously runs an engine refresh and signal pass. Tests can
## call this to observe transitions immediately; game code lets the
## deferred path coalesce mutations.
func drive_now() -> void:
	_refresh_now()


func _schedule_refresh() -> void:
	if _refresh_scheduled:
		return
	if not is_inside_tree():
		_refresh_now()
		return
	_refresh_scheduled = true
	_refresh_now.call_deferred()


func _refresh_now() -> void:
	_refresh_scheduled = false
	if not is_inside_tree():
		return
	if _is_server() and binding and multiplayer \
			and multiplayer.multiplayer_peer != null:
		binding.refresh_entities()
	_emit_transitions()


# Engine-driven re-evaluation hook. Fires every replication tick.
func _on_delta_synchronized() -> void:
	if _is_server() and binding and multiplayer \
			and multiplayer.multiplayer_peer != null:
		binding.refresh_entities()


# Recomputes per-(entity, peer) verdicts and emits any signal-level
# transitions. Does [b]not[/b] call [method
# MultiplayerSynchronizer.update_visibility] - that is the binding's
# job.
func _emit_transitions() -> void:
	var peers := _live_peers()
	var result := driver.compute(entities, peers, policy, viewers)

	for t in result.hide_transitions:
		var entity: NetwEntity = t[0]
		var peer: int = t[1]
		interest_exit.emit(entity, peer)
		entity.interest_exit.emit(peer)
	for t in result.show_transitions:
		var entity: NetwEntity = t[0]
		var peer: int = t[1]
		interest_enter.emit(entity, peer)
		entity.interest_enter.emit(peer)

	if (result.hide_transitions.size() > 0
			or result.show_transitions.size() > 0):
		Netw.dbg.trace(
				"IS[%s] transitions peers=%d hides=%d shows=%d",
				[layer_id, peers.size(),
				result.hide_transitions.size(),
				result.show_transitions.size()])

	driver.commit(result)


## Peers the driver iterates over each pass. Unions the multiplayer
## peer list, current [member viewers], and peers cached in the driver
## (so a [method remove_viewer] still produces a hide transition).
func _live_peers() -> Array[int]:
	var seen: Dictionary[int, bool] = {}
	var live_peer := multiplayer and multiplayer.multiplayer_peer != null
	if live_peer:
		if _is_server():
			for p in multiplayer.get_peers():
				seen[p] = true
		else:
			seen[multiplayer.get_unique_id()] = true
	for p in viewers:
		seen[p] = true
	for p in driver.cached_peers():
		seen[p] = true
	var out: Array[int] = []
	out.assign(seen.keys())
	return out


# ---------------------------------------------------------------------------
# Filters.
# ---------------------------------------------------------------------------

# Thin delegate to [InterestPolicy.verdict]. Kept on the node so tests
# and live debugging can call [code]sync._verdict_for(peer)[/code]
# without touching [member binding] or [member driver].
func _verdict_for(peer_id: int) -> bool:
	return InterestPolicy.verdict(policy, viewers, peer_id)


func _make_entity_filter(_entity: NetwEntity) -> Callable:
	return func(peer_id: int) -> bool:
		return _verdict_for(peer_id)


func _on_entity_tree_exiting(entity: NetwEntity) -> void:
	remove_entity(entity)


func _mirror_viewer_from_interest(peer_id: int, added: bool) -> void:
	if added:
		if viewers.has(peer_id):
			return
		viewers[peer_id] = true
		viewer_added.emit(peer_id)
	else:
		if not viewers.has(peer_id):
			return
		viewers.erase(peer_id)
		viewer_removed.emit(peer_id)
	_refresh_now()


func _mirror_entity_from_interest(entity: NetwEntity, added: bool) -> void:
	if added:
		if entity == null or not is_instance_valid(entity.owner):
			return
		if entities.has(entity):
			return
		entities[entity] = true
		var handler := _on_entity_tree_exiting.bind(entity)
		_entity_exit_handlers[entity] = handler
		if not entity.owner.tree_exiting.is_connected(handler):
			entity.owner.tree_exiting.connect(handler)
		entity_added.emit(entity)
		_refresh_now()
	else:
		if entity == null or not entities.has(entity):
			return
		driver.forget(entity)
		entities.erase(entity)
		var handler: Callable = _entity_exit_handlers.get(
				entity, Callable())
		if handler.is_valid() and is_instance_valid(entity) \
				and is_instance_valid(entity.owner) \
				and entity.owner.tree_exiting.is_connected(handler):
			entity.owner.tree_exiting.disconnect(handler)
		_entity_exit_handlers.erase(entity)
		entity_removed.emit(entity)


func _is_server() -> bool:
	if not is_inside_tree():
		return true
	if not multiplayer or multiplayer.multiplayer_peer == null:
		return true
	return multiplayer.is_server()


# ---------------------------------------------------------------------------
# NetwInterest registration.
# ---------------------------------------------------------------------------

func _register_with_interest() -> void:
	if _registered_with_interest:
		return
	if layer_id.is_empty():
		return
	var mt := MultiplayerTree.resolve(self)
	if not mt or not mt.interest:
		return
	mt.interest.register_anchor(self)
	_layer = mt.interest.layer_for(layer_id)
	_sync_layer_policy()
	_sync_layer_viewers()
	for entity: NetwEntity in entities:
		if _uses_interest_service():
			mt.interest.register_entity_for_layer(layer_id, entity)
		elif _layer:
			_layer.add_entity(entity)
	_registered_with_interest = true


func _unregister_with_interest() -> void:
	if not _registered_with_interest:
		return
	_registered_with_interest = false
	var mt := MultiplayerTree.resolve(self)
	if not mt or not mt.interest:
		return
	mt.interest.unregister_anchor(self)


func _interest() -> NetwInterest:
	var mt := MultiplayerTree.resolve(self)
	return mt.interest if mt else null


func _uses_interest_service() -> bool:
	return anchor_strategy == InterestBinding.AnchorStrategy.OPEN \
			and _interest() != null \
			and not layer_id.is_empty()


func _sync_layer_policy() -> void:
	var interest := _interest()
	if interest and not layer_id.is_empty():
		_layer = interest.layer_for(layer_id)
		if _uses_interest_service():
			interest.set_policy(layer_id, policy)
		elif _layer:
			_layer.set_policy(policy)
	elif _layer:
		_layer.set_policy(policy)


func _sync_layer_viewers() -> void:
	if not _layer and not layer_id.is_empty():
		var interest := _interest()
		_layer = interest.layer_for(layer_id) if interest else null
	if not _layer:
		return
	var prev: Array[int] = []
	prev.assign(_layer.viewers.keys())
	for peer_id in prev:
		if not viewers.has(peer_id):
			_layer.remove_viewer(peer_id)
	for peer_id in viewers:
		_layer.add_viewer(peer_id)


func _add_layer_viewer(peer_id: int) -> void:
	if _uses_interest_service():
		_interest().add_viewer(layer_id, peer_id)
	elif _layer:
		_layer.add_viewer(peer_id)


func _remove_layer_viewer(peer_id: int) -> void:
	if _uses_interest_service():
		_interest().remove_viewer(layer_id, peer_id)
	elif _layer:
		_layer.remove_viewer(peer_id)


# ---------------------------------------------------------------------------
# Replication config.
# ---------------------------------------------------------------------------

func _build_replication_config() -> void:
	if _config_built:
		return
	# Binding may not exist yet if NOTIFICATION_PARENTED fires before
	# _ready; build a transient binding just for config in that case.
	var b: InterestBinding = binding if binding \
			else InterestBinding.new(self, anchor_strategy)
	if b.build_replication_config():
		_config_built = true
