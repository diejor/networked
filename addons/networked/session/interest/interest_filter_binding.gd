## Engine-touching binding for [InterestSynchronizer] using
## per-synchronizer visibility filters.
##
## This is the only class in the interest stack that mutates Godot
## replication state: it installs the anchor's self filter, installs
## one filter per enrolled entity across every sibling
## [MultiplayerSynchronizer], calls
## [method MultiplayerSynchronizer.update_visibility] in engine-safe
## order, and builds the [member MultiplayerSynchronizer.replication_config]
## that spawn-syncs the layer's viewers and policy.
##
## [b]When this binding is correct:[/b] entities live in a subtree
## whose spawner is shared with every peer. The filter modulates
## per-entity delta visibility and triggers spawn/despawn via
## [method MultiplayerSynchronizer.update_visibility] as filters flip.
##
## [b]When this binding is the wrong tool:[/b] gating an entire scene
## that contains its own [MultiplayerSpawner]. Hiding individual
## entity filters does not hide the spawner, so outsider clients
## receive a spawn packet for a subtree they will then fail to find
## ([code]Node not found "MultiplayerSpawner"[/code]). That case
## belongs to a future [code]InterestPublicVisibilityBinding[/code]
## that drives [member MultiplayerSynchronizer.public_visibility] and
## [method MultiplayerSynchronizer.set_visibility_for] on the anchor.
##
## [method dump_state] returns a snapshot suitable for printing when a
## visibility error fires.
class_name InterestFilterBinding
extends RefCounted


var _anchor_ref: WeakRef
var _entity_filters: Dictionary[NetwEntity, Callable] = {}


func _init(anchor: MultiplayerSynchronizer) -> void:
	_anchor_ref = weakref(anchor)


## Configures the anchor for filter-driven visibility and installs
## [param self_filter] as its anchor-side filter.
func install_anchor(self_filter: Callable) -> void:
	var anchor := _anchor()
	if not anchor:
		return
	anchor.public_visibility = true
	anchor.add_visibility_filter(self_filter)


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


## Applies the transitions returned by [method InterestDriver.compute].
## Hides run first (deep-first) so descendants hide before ancestors;
## shows run after (shallow-first) so ancestors appear before
## descendants. The [param peers], [param kind], [param viewers]
## arguments are accepted for parity with
## [InterestPublicVisibilityBinding] but unused here: the anchor's
## filter already gates visibility per peer.
func apply(
		sync_hides: Array,
		sync_shows: Array,
		_peers: Array[int],
		_kind: int,
		_viewers: Dictionary) -> void:
	for t in sync_hides:
		var sync: MultiplayerSynchronizer = t[0]
		if is_instance_valid(sync):
			sync.update_visibility(t[1])
	for t in sync_shows:
		var sync: MultiplayerSynchronizer = t[0]
		if is_instance_valid(sync):
			sync.update_visibility(t[1])


## Builds the [member MultiplayerSynchronizer.replication_config] that
## spawn-syncs [member InterestSynchronizer.viewers] and
## [member InterestSynchronizer.policy]. Idempotent against the
## anchor: callers guard re-entry.
##
## Uses [member Node.owner] when available, falling back to [method
## Node.get_parent]. The fallback lets the config build during
## [constant Node.NOTIFICATION_PARENTED] (when [member Node.owner] is
## not yet assigned by the packed-scene loader) so the config is in
## place before the engine's
## [code]on_replication_start[/code] fires at [constant
## Node.NOTIFICATION_ENTER_TREE]. Assumes the IS placement convention
## where the anchor is a direct child of the node it gates.
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


## Returns the set of entities currently filter-installed.
func installed_entities() -> Array[NetwEntity]:
	var out: Array[NetwEntity] = []
	out.assign(_entity_filters.keys())
	return out


## Returns a snapshot of binding state for [param peer_id]. Structure:
## [codeblock]
##     {
##         "anchor_path": "<NodePath>",
##         "public_visibility": true,
##         "peer_id": 5,
##         "self_verdict": false,
##         "self_explanation": "REJECT peer=5 not in viewers ...",
##         "entities": [
##             {
##                 "entity": "<NetwEntity>",
##                 "owner_path": "<NodePath>",
##                 "syncs": ["<sync path>", ...],
##                 "filter_installed": true,
##             }, ...
##         ]
##     }
## [/codeblock]
## Pass the live [param kind] and [param viewers] so the explanation
## reflects current policy state.
func dump_state(
		kind: int,
		viewers: Dictionary,
		peer_id: int) -> Dictionary:
	var anchor := _anchor()
	var out: Dictionary = {
		"anchor_path": String(anchor.get_path()) if anchor else "<null>",
		"public_visibility": anchor.public_visibility if anchor else false,
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


func _anchor() -> MultiplayerSynchronizer:
	return _anchor_ref.get_ref() as MultiplayerSynchronizer
