## Anchored interest layer expressed as a [MultiplayerSynchronizer].
##
## One node owns one layer of the interest model: the [member viewers]
## set (peers that participate in the layer), the [member entities] set
## (entities whose synchronizers this anchor gates), and the [enum Policy]
## that composes the two into a per-peer verdict.
##
## [b]Placement:[/b] [InterestSynchronizer] is pre-instantiated in a
## scene as a sibling of the level it gates (mirrors the existing
## [SceneSynchronizer] placement). It is never added with
## [method Node.add_child] from script.
##
## [codeblock]
## var arena := %InterestSynchronizer
## arena.layer_id = &"arena:1"
## arena.policy = InterestSynchronizer.Policy.HIDE_FROM_OUTSIDERS
## arena.admit(player_entity)
## [/codeblock]
##
## [b]Dormant:[/b] this primitive is not yet wired into [NetwInterest]
## or the [SceneSynchronizer] production path. It exists so the unit
## tests can lock its viewer / entity / policy semantics before any
## production subsystem depends on it.
##
## [b]Binding model:[/b] FilterBinding. [member public_visibility] is
## kept [code]true[/code] so [member peer_visibility] short-circuits to
## true, leaving the per-peer verdict to the installed visibility
## filters (anchor self + each enrolled entity sync).
##
## [b]Replication config:[/b] [member replication_config] is built on
## [constant Node.NOTIFICATION_PARENTED] to spawn-sync [member viewers].
## User-defined replicated properties must live on a sibling
## [MultiplayerSynchronizer].
class_name InterestSynchronizer
extends MultiplayerSynchronizer


## Composition rule for the per-peer verdict.
enum Policy {
	## Peers in [member viewers] see [member entities]; outsiders do not.
	## Default; matches today's [SceneSynchronizer] behavior.
	HIDE_FROM_OUTSIDERS,
	## Peers in [member viewers] do [b]not[/b] see [member entities];
	## outsiders do. Use for stealth-style bubbles.
	HIDE_FROM_INSIDERS,
}


## Stable identifier for the layer this synchronizer anchors.
@export var layer_id: StringName

## Composition policy. See [enum Policy].
@export var policy: Policy = Policy.HIDE_FROM_OUTSIDERS

## Peer IDs participating in this layer. Spawn-synced
## ([code]REPLICATION_MODE_ON_CHANGE[/code]) so the viewer set travels
## with the spawn packet to anyone allowed to see the anchor.
@export var viewers: Dictionary[int, bool] = {}


## Emitted on the server after [param entity] becomes visible to
## [param peer_id]. Phase 1 declares the signal; emission is wired in a
## later phase that drives [method update_visibility] cycles.
signal revealed(entity: NetwEntity, peer_id: int)

## Emitted on the server after [param entity] becomes hidden from
## [param peer_id]. See note on [signal revealed].
signal hidden(entity: NetwEntity, peer_id: int)


## Entities whose synchronizers this anchor gates. Server-only; not
## replicated. Mutated through [method add_entity] / [method remove_entity].
var entities: Dictionary[NetwEntity, bool] = {}

var _entity_filters: Dictionary = {}
var _entity_exit_handlers: Dictionary = {}
var _config_built: bool = false


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		_build_replication_config()


func _ready() -> void:
	unique_name_in_owner = true
	public_visibility = true
	add_visibility_filter(_self_filter)


# ---------------------------------------------------------------------------
# Viewer API. Server-only mutators.
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


## Removes [param peer_id] from [member viewers]. Idempotent.
## Server-only.
func remove_viewer(peer_id: int) -> void:
	if not _is_server():
		return
	if not viewers.has(peer_id):
		return
	viewers.erase(peer_id)


## Returns [code]true[/code] if [param peer_id] is currently a viewer.
func has_viewer(peer_id: int) -> bool:
	return viewers.has(peer_id)


## Snapshot of current viewer peer ids.
func viewer_ids() -> Array[int]:
	var out: Array[int] = []
	out.assign(viewers.keys())
	return out


# ---------------------------------------------------------------------------
# Entity API. Server-only mutators.
# ---------------------------------------------------------------------------

## Enrolls [param entity] under this anchor. Installs the anchor's
## visibility filter on every [MultiplayerSynchronizer] owned by the
## entity, and connects to [signal Node.tree_exiting] for automatic
## cleanup. Idempotent. Server-only.
func add_entity(entity: NetwEntity) -> void:
	if not _is_server():
		return
	if entity == null or not is_instance_valid(entity.owner):
		return
	if entities.has(entity):
		return
	entities[entity] = true

	var filter := _make_entity_filter(entity)
	_entity_filters[entity] = filter
	for sync in entity.synchronizers():
		if is_instance_valid(sync):
			sync.add_visibility_filter(filter)

	var handler := _on_entity_tree_exiting.bind(entity)
	_entity_exit_handlers[entity] = handler
	if not entity.owner.tree_exiting.is_connected(handler):
		entity.owner.tree_exiting.connect(handler)


## Removes [param entity] from this anchor. Detaches the visibility
## filter from each of the entity's synchronizers and disconnects the
## cleanup handler. Idempotent. Server-only.
func remove_entity(entity: NetwEntity) -> void:
	if not _is_server():
		return
	if entity == null:
		return
	if not entities.has(entity):
		return
	entities.erase(entity)

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


## Returns [code]true[/code] if [param entity] is currently enrolled.
func has_entity(entity: NetwEntity) -> bool:
	return entities.has(entity)


# ---------------------------------------------------------------------------
# Convenience.
# ---------------------------------------------------------------------------

## Adds [param entity] as both an entity of this layer and a viewer
## (using [member NetwEntity.peer_id]). For peer-owned entities the
## peer becomes a viewer; for server-owned entities (peer_id == 0) only
## the enrollment side runs.
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
# Internals.
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


func _build_replication_config() -> void:
	if _config_built:
		return
	if not owner:
		return
	_config_built = true
	root_path = get_path_to(owner)
	var config := SceneReplicationConfig.new()
	var path := NodePath(str(owner.get_path_to(self)) + ":viewers")
	config.add_property(path)
	config.property_set_spawn(path, true)
	config.property_set_replication_mode(
			path,
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	replication_config = config
