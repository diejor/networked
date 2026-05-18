## Anchored interest layer expressed as a [MultiplayerSynchronizer].
##
## One node owns one layer of the interest model: the [member viewers]
## set (peers that participate in the layer), the [member entities] set
## (entities whose synchronizers this anchor gates), and the [enum
## Policy] that composes the two into a per-peer verdict.
##
## [b]Placement:[/b] [InterestSynchronizer] is pre-instantiated in a
## scene as a sibling of the level it gates (mirrors the existing
## [SceneSynchronizer] placement). It is never added with
## [method Node.add_child] from script.
##
## [codeblock]
##     var arena := %InterestSynchronizer
##     arena.layer_id = &"arena:1"
##     arena.policy = InterestSynchronizer.Policy.HIDE_FROM_OUTSIDERS
##     arena.add_viewer(player.peer_id)
## [/codeblock]
##
## [b]Composition:[/b] this node is a thin orchestrator over three
## helpers, each independently testable and inspectable:
## [br]- [InterestPolicy]: stateless [code](kind, viewers, peer)[/code]
##   verdict and [method InterestPolicy.explain] reason string.
## [br]- [member driver]: per-(entity, peer) cache and transition
##   computation. Engine-free.
## [br]- [member binding]: the only engine-touching piece; installs
##   visibility filters and calls [method
##   MultiplayerSynchronizer.update_visibility].
##
## [b]Debugging:[/b] every mutation traces through [code]Netw.dbg[/code]
## (level [code]trace[/code]); [method debug_dump] returns a full
## snapshot of policy / driver / binding state for a peer.
##
## [b]Signals:[/b] [signal interest_enter] / [signal interest_exit]
## fire for each per-peer visibility transition. Lifecycle signals
## [signal viewer_added] / [signal viewer_removed] /
## [signal entity_added] / [signal entity_removed] fire eagerly on
## each mutation.
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


## Selects the engine-side gating strategy. See [member binding_mode].
enum BindingMode {
	## Default-allow: anchor uses [code]public_visibility=true[/code]
	## plus an anchor-side visibility filter; entities receive per-sync
	## visibility filters. Use for AoI and per-entity gating where the
	## spawner is shared across peers.
	FILTER,
	## Default-deny: anchor uses [code]public_visibility=false[/code]
	## and is admitted per peer via [method
	## MultiplayerSynchronizer.set_visibility_for]. Use when the anchor
	## sits on a subtree that contains its own [MultiplayerSpawner]
	## (the [SceneSynchronizer] case).
	PUBLIC_VISIBILITY,
}


## Stable identifier for the layer this synchronizer anchors. Used by
## [NetwInterest] to look up the anchor from [NetwEntity] members.
@export var layer_id: StringName

## Engine-side gating strategy. See [enum BindingMode]. Must be set
## before [method _ready] runs; switching at runtime is not supported.
@export var binding_mode: BindingMode = BindingMode.FILTER

## Composition policy. See [enum Policy]. Mutating triggers a driver
## pass; on the client this lands via spawn-sync replication.
@export var policy: Policy = Policy.HIDE_FROM_OUTSIDERS:
	set(value):
		var changed := value != policy
		policy = value
		if changed and _initial_sync_done:
			Netw.dbg.trace(
					"IS[%s] policy changed -> %d", [layer_id, value])
			_schedule_drive()

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
		if added.size() > 0 or removed.size() > 0:
			_schedule_drive()


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
## [method add_entity] / [method remove_entity]. On the server, used
## to install visibility filters; on the client, used to drive local
## per-peer signals.
var entities: Dictionary[NetwEntity, bool] = {}

## Per-(entity, peer) cache + transition computation. Public so tests
## and debug code can inspect [method InterestDriver.dump].
var driver: InterestDriver = InterestDriver.new()

## Engine-touching binding. Concrete type depends on [member
## binding_mode]: [InterestFilterBinding] for [code]FILTER[/code],
## [InterestPublicVisibilityBinding] for [code]PUBLIC_VISIBILITY[/code].
## Public so debug code can call [code]binding.dump_state(...)[/code].
## Created in [method _ready].
var binding: RefCounted

var _entity_exit_handlers: Dictionary = {}
var _entity_install_handlers: Dictionary = {}
var _config_built: bool = false
var _initial_sync_done: bool = false
var _drive_scheduled: bool = false
var _registered_with_interest: bool = false


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		_build_replication_config()


func _enter_tree() -> void:
	_register_with_interest()
	_initial_sync_done = true


func _exit_tree() -> void:
	_unregister_with_interest()


func _ready() -> void:
	unique_name_in_owner = true
	binding = _make_binding()
	binding.install_anchor(_self_filter)
	# Fallback for cases where [constant Node.NOTIFICATION_PARENTED]
	# fired before [member Node.owner] was assigned (script-driven
	# instantiation). Idempotent via [member _config_built].
	_build_replication_config()


func _make_binding() -> RefCounted:
	match binding_mode:
		BindingMode.PUBLIC_VISIBILITY:
			return InterestPublicVisibilityBinding.new(self)
		_:
			return InterestFilterBinding.new(self)


# ---------------------------------------------------------------------------
# Viewer API (server-only mutators).
# ---------------------------------------------------------------------------

## Adds [param peer_id] to [member viewers]. Idempotent; rejects
## [code]0[/code]. Server-only.
func add_viewer(peer_id: int) -> void:
	if not _is_server():
		return
	if peer_id == 0:
		return
	if viewers.has(peer_id):
		return
	viewers[peer_id] = true
	Netw.dbg.trace("IS[%s] add_viewer %d", [layer_id, peer_id])
	viewer_added.emit(peer_id)
	_schedule_drive()


## Removes [param peer_id] from [member viewers]. Idempotent.
## Server-only.
func remove_viewer(peer_id: int) -> void:
	if not _is_server():
		return
	if not viewers.has(peer_id):
		return
	viewers.erase(peer_id)
	Netw.dbg.trace("IS[%s] remove_viewer %d", [layer_id, peer_id])
	viewer_removed.emit(peer_id)
	_schedule_drive()


## Returns [code]true[/code] if [param peer_id] is currently a viewer.
func has_viewer(peer_id: int) -> bool:
	return viewers.has(peer_id)


## Snapshot of current viewer peer ids.
func viewer_ids() -> Array[int]:
	var out: Array[int] = []
	out.assign(viewers.keys())
	return out


# ---------------------------------------------------------------------------
# Entity API. Server installs filters; both sides update the local
# [member entities] dict and run the driver to fire signals.
# ---------------------------------------------------------------------------

## Enrolls [param entity] under this anchor. On the server, installs
## the anchor's visibility filter on every [MultiplayerSynchronizer]
## owned by the entity. On both sides, records the entity locally and
## hooks [signal Node.tree_exiting] for automatic cleanup. Idempotent.
##
## [b]Off-tree owners:[/b] when the entity's owner has not yet entered
## the scene tree (e.g., [code]add_player[/code] calls [method
## track_node] before [method Node.add_child]), filter install is
## deferred to [signal Node.tree_entered] so [method
## NetwEntity.synchronizers] returns the populated list.
func add_entity(entity: NetwEntity) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
	if entities.has(entity):
		return
	entities[entity] = true

	if _is_server() and binding:
		if entity.owner.is_inside_tree():
			binding.install_entity(entity, _make_entity_filter(entity))
		else:
			var install_handler := _install_entity_filter.bind(entity)
			_entity_install_handlers[entity] = install_handler
			entity.owner.tree_entered.connect(
					install_handler, CONNECT_ONE_SHOT)

	var handler := _on_entity_tree_exiting.bind(entity)
	_entity_exit_handlers[entity] = handler
	if not entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.connect(handler)

	Netw.dbg.trace(
			"IS[%s] add_entity %s (in_tree=%s)",
			[layer_id, entity.owner.name,
			entity.owner.is_inside_tree()])
	entity_added.emit(entity)
	_schedule_drive()


# Fires from [signal Node.tree_entered] when [method add_entity] was
# called before the entity's owner was in the tree. Installs the
# visibility filter now that [method NetwEntity.synchronizers] has
# real syncs to attach to, then drives so the new filter state is
# reflected in the engine's per-peer visibility.
func _install_entity_filter(entity: NetwEntity) -> void:
	_entity_install_handlers.erase(entity)
	if not _is_server() or not binding:
		return
	if not is_instance_valid(entity) \
			or not is_instance_valid(entity.owner):
		return
	if not entities.has(entity):
		return
	binding.install_entity(entity, _make_entity_filter(entity))
	_schedule_drive()


## Removes [param entity] from this anchor. Emits [signal interest_exit]
## for every peer that previously saw it, detaches the filter, and
## clears the cleanup hook. Idempotent.
func remove_entity(entity: NetwEntity) -> void:
	if entity == null:
		return
	if not entities.has(entity):
		return

	# Emit interest_exit for peers that were seeing this entity, while
	# the [NetwEntity] reference is still valid.
	var prev_view := driver.cached_view_for(entity)
	for peer_id: int in prev_view:
		if prev_view[peer_id]:
			interest_exit.emit(entity, peer_id)
			entity.interest_exit.emit(peer_id)
	driver.forget(entity)

	entities.erase(entity)

	if _is_server() and binding:
		binding.uninstall_entity(entity)

	var install_handler: Callable = _entity_install_handlers.get(
			entity, Callable())
	if install_handler.is_valid() and is_instance_valid(entity) \
			and is_instance_valid(entity.owner) \
			and entity.owner.tree_entered.is_connected(install_handler):
		entity.owner.tree_entered.disconnect(install_handler)
	_entity_install_handlers.erase(entity)

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
## [param peer_id]. Use when chasing a visibility error: print the
## dump, correlate [code]binding.entities[].filter_installed[/code]
## with what the engine actually replicated.
##
## Structure:
## [codeblock]
##     {
##         "layer_id": "arena:1",
##         "policy": 0,
##         "viewers": [1, 5],
##         "peer_id": 5,
##         "verdict": true,
##         "explanation": "ADMIT peer=5 in viewers under ...",
##         "driver_cache": {<entity>: {<peer>: bool}},
##         "binding": { ...InterestFilterBinding.dump_state... },
##     }
## [/codeblock]
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
# Driver.
# ---------------------------------------------------------------------------

## Synchronously runs a visibility pass. Tests can call this to
## observe transitions immediately; game code lets the deferred path
## coalesce mutations.
func drive_now() -> void:
	_drive_visibility_update()


func _schedule_drive() -> void:
	if _drive_scheduled:
		return
	if not is_inside_tree():
		_drive_visibility_update()
		return
	_drive_scheduled = true
	_drive_visibility_update.call_deferred()


func _drive_visibility_update() -> void:
	_drive_scheduled = false
	if not is_inside_tree():
		return

	var peers := _live_peers()
	var result := driver.compute(entities, peers, policy, viewers)

	if _is_server() and binding and multiplayer \
			and multiplayer.multiplayer_peer != null:
		binding.apply(
				result.sync_hides,
				result.sync_shows,
				peers,
				policy,
				viewers)

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
				"IS[%s] drive peers=%d hides=%d shows=%d",
				[layer_id, peers.size(),
				result.hide_transitions.size(),
				result.show_transitions.size()])

	driver.commit(result)


## Peers the driver iterates over each pass. Unions:
## [br]- the multiplayer peer list (real session participants),
## [br]- current [member viewers] (peers that should become visible),
## [br]- peers cached in the driver (peers that may need to
##   transition to hidden after a [method remove_viewer]).
## [br][br]
## On a client this collapses to the local peer id when a multiplayer
## peer is attached; outside multiplayer (unit tests) it falls back to
## the union of viewers and cached peers.
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
# Verdict and filters.
# ---------------------------------------------------------------------------

# Thin delegate to [InterestPolicy.verdict]. Kept on the node so tests
# and live debugging can call [code]sync._verdict_for(peer)[/code]
# without touching [member binding] or [member driver].
func _verdict_for(peer_id: int) -> bool:
	return InterestPolicy.verdict(policy, viewers, peer_id)


func _self_filter(peer_id: int) -> bool:
	return _verdict_for(peer_id)


func _make_entity_filter(_entity: NetwEntity) -> Callable:
	return func(peer_id: int) -> bool:
		return _verdict_for(peer_id)


func _on_entity_tree_exiting(entity: NetwEntity) -> void:
	remove_entity(entity)


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
	_registered_with_interest = true


func _unregister_with_interest() -> void:
	if not _registered_with_interest:
		return
	_registered_with_interest = false
	var mt := MultiplayerTree.resolve(self)
	if not mt or not mt.interest:
		return
	mt.interest.unregister_anchor(self)


# ---------------------------------------------------------------------------
# Replication config.
# ---------------------------------------------------------------------------

func _build_replication_config() -> void:
	if _config_built:
		return
	# binding may not exist yet if NOTIFICATION_PARENTED fires before
	# _ready; build a transient binding just for config in that case.
	# The binding's build_replication_config falls back to
	# [method Node.get_parent] when [member Node.owner] is not yet
	# assigned, so this can succeed at NOTIFICATION_PARENTED time -
	# which is critical so the config is in place before the engine's
	# on_replication_start fires at NOTIFICATION_ENTER_TREE.
	var b: RefCounted = binding if binding else _make_binding()
	if b.build_replication_config():
		_config_built = true
