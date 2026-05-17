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
## [b]Binding model:[/b] FilterBinding. [member public_visibility] is
## kept [code]true[/code] so [member peer_visibility] short-circuits to
## true, leaving the per-peer verdict to the installed visibility
## filters (anchor self + each enrolled entity sync).
##
## [b]Replication config:[/b] [member replication_config] is built on
## [constant Node.NOTIFICATION_PARENTED] to spawn-sync [member viewers]
## and [member policy]. User-defined replicated properties must live on
## a sibling [MultiplayerSynchronizer].
##
## [b]Signals:[/b] [signal interest_enter] / [signal interest_exit]
## fire for each per-peer visibility transition. Lifecycle signals
## [signal viewer_added] / [signal viewer_removed] /
## [signal entity_added] / [signal entity_removed] fire eagerly on
## each mutation. See the class header in [code]docs/[/code] for the
## full driver contract.
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


## Stable identifier for the layer this synchronizer anchors. Used by
## [NetwInterest] to look up the anchor from [NetwEntity] members.
@export var layer_id: StringName

## Composition policy. See [enum Policy]. Mutating triggers a driver
## pass; on the client this lands via spawn-sync replication.
@export var policy: Policy = Policy.HIDE_FROM_OUTSIDERS:
	set(value):
		var changed := value != policy
		policy = value
		if changed and _initial_sync_done:
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
			viewer_added.emit(p)
		for p in removed:
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

# Per-(entity, peer) visibility cache. Server: cached verdict for
# every live peer. Client: cached verdict for the local peer.
var _visibility: Dictionary = {}

var _entity_filters: Dictionary = {}
var _entity_exit_handlers: Dictionary = {}
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
	public_visibility = true
	add_visibility_filter(_self_filter)
	# Fallback for cases where [constant Node.NOTIFICATION_PARENTED]
	# fired before [member Node.owner] was assigned (script-driven
	# instantiation). Idempotent via [member _config_built].
	_build_replication_config()


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
func add_entity(entity: NetwEntity) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
	if entities.has(entity):
		return
	entities[entity] = true

	if _is_server():
		var filter := _make_entity_filter(entity)
		_entity_filters[entity] = filter
		for sync in entity.synchronizers():
			if is_instance_valid(sync):
				sync.add_visibility_filter(filter)

	var handler := _on_entity_tree_exiting.bind(entity)
	_entity_exit_handlers[entity] = handler
	if not entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.connect(handler)

	entity_added.emit(entity)
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
	var prev_view: Dictionary = _visibility.get(entity, {})
	for peer_id: int in prev_view:
		if prev_view[peer_id]:
			interest_exit.emit(entity, peer_id)
			entity.interest_exit.emit(peer_id)
	_visibility.erase(entity)

	entities.erase(entity)

	if _is_server():
		var filter: Callable = _entity_filters.get(entity, Callable())
		if filter.is_valid() and is_instance_valid(entity.owner):
			for sync in entity.synchronizers():
				if is_instance_valid(sync):
					sync.remove_visibility_filter(filter)
		_entity_filters.erase(entity)

	var handler: Callable = _entity_exit_handlers.get(entity, Callable())
	if handler.is_valid() and is_instance_valid(entity.owner) \
			and entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.disconnect(handler)
	_entity_exit_handlers.erase(entity)

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
	var per_entity: Dictionary = _visibility.get(entity, {})
	return per_entity.get(peer_id, false)


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


# Recomputes visibility and emits transition signals.
#
# Server iterates every live peer; client iterates only itself.
# Hide transitions are sorted deep-first to avoid Godot issue #68508
# (hiding a shallow ancestor before its visible descendants).
func _drive_visibility_update() -> void:
	_drive_scheduled = false
	if not is_inside_tree():
		return

	var peers := _live_peers()
	# Per-(entity, peer) transitions for signal emission. Independent
	# of synchronizer count so entities without a sync still emit.
	var hide_transitions: Array = []
	var show_transitions: Array = []
	# Per-sync tuples for [method MultiplayerSynchronizer.update_visibility].
	var sync_hides: Array = []
	var sync_shows: Array = []
	var new_state: Dictionary = {}

	for entity: NetwEntity in entities:
		if not is_instance_valid(entity) \
				or not is_instance_valid(entity.owner):
			continue
		var prev: Dictionary = _visibility.get(entity, {})
		var per_entity: Dictionary = {}
		new_state[entity] = per_entity
		for peer: int in peers:
			var now := _verdict_for(peer)
			per_entity[peer] = now
			var was: bool = prev.get(peer, false)
			if was == now:
				continue
			var transition := [entity, peer]
			if now:
				show_transitions.append(transition)
			else:
				hide_transitions.append(transition)
			for sync in entity.synchronizers():
				if not is_instance_valid(sync):
					continue
				var tup := [sync, peer]
				if now:
					sync_shows.append(tup)
				else:
					sync_hides.append(tup)

	sync_hides.sort_custom(_sync_deeper_first)
	sync_shows.sort_custom(_sync_shallower_first)
	hide_transitions.sort_custom(_entity_deeper_first)
	show_transitions.sort_custom(_entity_shallower_first)

	if _is_server() and multiplayer \
			and multiplayer.multiplayer_peer != null:
		for t in sync_hides:
			(t[0] as MultiplayerSynchronizer).update_visibility(t[1])
		for t in sync_shows:
			(t[0] as MultiplayerSynchronizer).update_visibility(t[1])

	for t in hide_transitions:
		var entity: NetwEntity = t[0]
		var peer: int = t[1]
		interest_exit.emit(entity, peer)
		entity.interest_exit.emit(peer)
	for t in show_transitions:
		var entity: NetwEntity = t[0]
		var peer: int = t[1]
		interest_enter.emit(entity, peer)
		entity.interest_enter.emit(peer)

	_visibility = new_state


## Peers the driver iterates over each pass. Unions:
## [br]- the multiplayer peer list (real session participants),
## [br]- current [member viewers] (peers that should become visible),
## [br]- peers cached in [member _visibility] (peers that may need to
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
	for entity in _visibility:
		var per_entity: Dictionary = _visibility[entity]
		for p in per_entity:
			seen[p] = true
	var out: Array[int] = []
	out.assign(seen.keys())
	return out


func _sync_deeper_first(a: Array, b: Array) -> bool:
	return (a[0] as Node).get_path().get_name_count() \
		> (b[0] as Node).get_path().get_name_count()


func _sync_shallower_first(a: Array, b: Array) -> bool:
	return (a[0] as Node).get_path().get_name_count() \
		< (b[0] as Node).get_path().get_name_count()


func _entity_deeper_first(a: Array, b: Array) -> bool:
	return (a[0] as NetwEntity).owner.get_path().get_name_count() \
		> (b[0] as NetwEntity).owner.get_path().get_name_count()


func _entity_shallower_first(a: Array, b: Array) -> bool:
	return (a[0] as NetwEntity).owner.get_path().get_name_count() \
		< (b[0] as NetwEntity).owner.get_path().get_name_count()


# ---------------------------------------------------------------------------
# Verdict and filters.
# ---------------------------------------------------------------------------

## Per-peer verdict resolved from [member policy] and [member viewers].
## The server peer is always admitted; peer id [code]0[/code] (no peer
## context) is always rejected.
func _verdict_for(peer_id: int) -> bool:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	if peer_id == 0:
		return false
	match policy:
		Policy.HIDE_FROM_OUTSIDERS:
			return viewers.has(peer_id)
		Policy.HIDE_FROM_INSIDERS:
			return not viewers.has(peer_id)
	return true


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
	if not owner:
		return
	_config_built = true
	root_path = get_path_to(owner)
	var config := SceneReplicationConfig.new()
	var viewers_path := NodePath(
			str(owner.get_path_to(self)) + ":viewers")
	config.add_property(viewers_path)
	config.property_set_spawn(viewers_path, true)
	config.property_set_replication_mode(
			viewers_path,
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	var policy_path := NodePath(
			str(owner.get_path_to(self)) + ":policy")
	config.add_property(policy_path)
	config.property_set_spawn(policy_path, true)
	config.property_set_replication_mode(
			policy_path,
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	replication_config = config
