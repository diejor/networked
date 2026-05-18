## Engine-touching binding for [InterestSynchronizer].
##
## Replaces the legacy split between [code]InterestFilterBinding[/code]
## and [code]InterestPublicVisibilityBinding[/code]. The two halves of
## old [SceneSynchronizer] - anchor admission via [method
## MultiplayerSynchronizer.set_visibility_for] and per-entity visibility
## filters - are always used [b]together[/b], parameterized only by
## [enum AnchorStrategy].
##
## [b]Per-entity gating[/b]: [method install_entity] adds a closure
## visibility filter on every sibling [MultiplayerSynchronizer]. The
## closure reads [member InterestSynchronizer.viewers] via
## [InterestPolicy]. [method refresh_entities] calls [method
## MultiplayerSynchronizer.update_visibility] with no argument, letting
## the engine iterate its own [code]peers_info[/code] and re-evaluate
## every filter. This is what old [SceneSynchronizer.update_players]
## did, and what keeps the path self-healing under viewer churn.
##
## [b]Anchor admission[/b]: under [code]AnchorStrategy.ADMIT[/code] the
## anchor is [code]public_visibility = false[/code] and admitted per
## peer via [method admit] / [method unadmit]. [method unadmit] defers
## [method MultiplayerSynchronizer.set_visibility_for] so the wrapper
## hide lands after any spawner-driven entity despawns the engine is
## still flushing (godot issue #68508). Both calls are guarded by
## [method _peer_is_live] so a peer the engine already purged does not
## trigger the [code]peers_info[/code] assert.
##
## [b]Anchor strategy is a property of spawner topology[/b], not a
## choice between two visibility schemes:
## [br]- [code]ADMIT[/code]: the anchor sits next to a subtree
##   containing its own [MultiplayerSpawner] (the wrapper case). Hiding
##   the anchor on outsider peers is what gates the inner spawner.
## [br]- [code]OPEN[/code]: entities live in a shared subtree visible
##   to every peer (the AoI case). Per-entity filters do all the
##   gating; no anchor admission is needed.
class_name InterestBinding
extends RefCounted


## Determines how the anchor itself is gated. See class docs.
enum AnchorStrategy {
	## Default-deny: anchor uses [code]public_visibility = false[/code]
	## and is admitted per peer via [method admit].
	ADMIT,
	## Default-allow: anchor is [code]public_visibility = true[/code];
	## per-entity filters are the only gating.
	OPEN,
}


var _anchor_ref: WeakRef
var _strategy: AnchorStrategy
var _entity_filters: Dictionary[NetwEntity, Callable] = {}


func _init(
		anchor: MultiplayerSynchronizer,
		strategy: AnchorStrategy = AnchorStrategy.ADMIT) -> void:
	_anchor_ref = weakref(anchor)
	_strategy = strategy


## Configures the anchor for its [enum AnchorStrategy]. Idempotent.
func install_anchor() -> void:
	var anchor := _anchor()
	if not anchor:
		return
	match _strategy:
		AnchorStrategy.ADMIT:
			anchor.public_visibility = false
		AnchorStrategy.OPEN:
			anchor.public_visibility = true


## Installs [param filter] on every [MultiplayerSynchronizer] under
## [member NetwEntity.owner]. Idempotent per entity.
func install_entity(entity: NetwEntity, filter: Callable) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
	if _entity_filters.has(entity):
		return
	_entity_filters[entity] = filter
	for sync in entity.synchronizers():
		if is_instance_valid(sync):
			sync.add_visibility_filter(filter)


## Removes the filter previously installed for [param entity].
## Idempotent.
func uninstall_entity(entity: NetwEntity) -> void:
	var filter: Callable = _entity_filters.get(entity, Callable())
	if not filter.is_valid():
		return
	if is_instance_valid(entity) and is_instance_valid(entity.owner):
		for sync in entity.synchronizers():
			if is_instance_valid(sync):
				sync.remove_visibility_filter(filter)
	_entity_filters.erase(entity)


## Calls [method MultiplayerSynchronizer.update_visibility] with no
## argument on every tracked entity's syncs. The engine iterates its
## own peer list and re-runs every installed filter.
##
## [b]Server-only[/b]; [method MultiplayerSynchronizer.update_visibility]
## is a no-op on clients.
func refresh_entities() -> void:
	for entity: NetwEntity in _entity_filters:
		if not is_instance_valid(entity) \
				or not is_instance_valid(entity.owner):
			continue
		for sync in entity.synchronizers():
			if is_instance_valid(sync) and sync.is_inside_tree():
				sync.update_visibility()


## Admits [param peer_id] at the anchor level. No-op under
## [code]AnchorStrategy.OPEN[/code]. Synchronous so the spawn-sync of
## [member InterestSynchronizer.viewers] cannot reach the peer before
## the wrapper itself is admitted.
func admit(peer_id: int) -> void:
	if _strategy != AnchorStrategy.ADMIT:
		return
	var anchor := _anchor()
	if not anchor:
		return
	if not _peer_is_live(peer_id):
		return
	anchor.set_visibility_for(peer_id, true)


## Hides the anchor from [param peer_id]. No-op under
## [code]AnchorStrategy.OPEN[/code]. Deferred via [method
## Callable.call_deferred] so the wrapper hide lands [b]after[/b]
## per-entity despawns the engine flushes this frame (godot issue
## #68508).
func unadmit(peer_id: int) -> void:
	if _strategy != AnchorStrategy.ADMIT:
		return
	var anchor := _anchor()
	if not anchor:
		return
	if not _peer_is_live(peer_id):
		return
	anchor.set_visibility_for.call_deferred(peer_id, false)


## Builds the [member MultiplayerSynchronizer.replication_config] that
## spawn-syncs [member InterestSynchronizer.viewers] and
## [member InterestSynchronizer.policy]. Uses [member Node.owner] when
## available, falling back to [method Node.get_parent] so the config
## can be built during [constant Node.NOTIFICATION_PARENTED] (before
## the packed-scene loader assigns [member Node.owner]) - this lands
## the config before the engine's [code]on_replication_start[/code]
## fires at [constant Node.NOTIFICATION_ENTER_TREE].
func build_replication_config() -> bool:
	var anchor := _anchor()
	if not anchor:
		return false
	var target: Node = anchor.owner if anchor.owner else anchor.get_parent()
	if not target:
		return false
	anchor.root_path = anchor.get_path_to(target)
	var config := SceneReplicationConfig.new()
	_add_spawn_property(config, anchor, target, "viewers")
	_add_spawn_property(config, anchor, target, "policy")
	anchor.replication_config = config
	return true


static func _add_spawn_property(
		config: SceneReplicationConfig,
		anchor: MultiplayerSynchronizer,
		target: Node,
		property: String) -> void:
	var path := NodePath(
			str(target.get_path_to(anchor)) + ":" + property)
	config.add_property(path)
	config.property_set_spawn(path, true)
	config.property_set_replication_mode(
			path,
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)


## Snapshot of entities currently filter-installed.
func installed_entities() -> Array[NetwEntity]:
	var out: Array[NetwEntity] = []
	out.assign(_entity_filters.keys())
	return out


## Returns a snapshot of binding state for [param peer_id]. Pass the
## live [param kind] and [param viewers] so [code]self_explanation[/code]
## reflects the current policy state.
func dump_state(
		kind: int,
		viewers: Dictionary,
		peer_id: int) -> Dictionary:
	var anchor := _anchor()
	var anchor_admitted := false
	if anchor and _strategy == AnchorStrategy.ADMIT:
		anchor_admitted = anchor.get_visibility_for(peer_id)
	var out: Dictionary = {
		"anchor_path": String(anchor.get_path()) if anchor else "<null>",
		"anchor_strategy": _strategy,
		"public_visibility": anchor.public_visibility if anchor else false,
		"anchor_admitted": anchor_admitted,
		"peer_id": peer_id,
		"self_verdict": InterestPolicy.verdict(kind, viewers, peer_id),
		"self_explanation": InterestPolicy.explain(
				kind, viewers, peer_id),
		"entities": [],
	}
	for entity: NetwEntity in _entity_filters:
		if not is_instance_valid(entity) \
				or not is_instance_valid(entity.owner):
			continue
		var syncs: Array = []
		for sync in entity.synchronizers():
			if is_instance_valid(sync):
				syncs.append(String(sync.get_path()))
		out["entities"].append({
			"entity": entity,
			"owner_path": String(entity.owner.get_path()),
			"syncs": syncs,
			"filter_installed": true,
		})
	return out


# Mirrors the liveness guard from the legacy [SceneSynchronizer]:
# skips [method MultiplayerSynchronizer.set_visibility_for] on peers
# the engine has already purged, which would hit the engine's
# [code]peers_info[/code] assert.
func _peer_is_live(peer_id: int) -> bool:
	var anchor := _anchor()
	if not anchor:
		return false
	var mp := anchor.multiplayer
	if not mp or mp.multiplayer_peer == null:
		return false
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
	if peer_id == mp.get_unique_id():
		return true
	return peer_id in mp.get_peers()


func _anchor() -> MultiplayerSynchronizer:
	return _anchor_ref.get_ref() as MultiplayerSynchronizer
