## Default-deny binding for [InterestSynchronizer]. The anchor uses
## [code]public_visibility = false[/code] and is admitted per peer via
## [method MultiplayerSynchronizer.set_visibility_for]. Entity
## synchronizers still receive a per-peer visibility filter so deltas
## are gated even when a peer is admitted at the anchor level.
##
## [b]When this binding is correct:[/b] the anchor is a sibling of a
## subtree that contains its own [MultiplayerSpawner] (the
## [SceneSynchronizer] case). Hiding the anchor on outsider peers
## prevents the engine from ever telling those peers about the
## subtree's spawner, which is what the
## [code]Node not found "MultiplayerSpawner"[/code] errors point at
## when a default-allow binding is used instead.
##
## [b]Engine ordering (godot issue #68508):[/b] [method apply] runs
## entity hides deep-first, then anchor hides per newly-outsider peer,
## then anchor shows per newly-viewer peer, then entity shows
## shallow-first.
class_name InterestPublicVisibilityBinding
extends RefCounted


var _anchor_ref: WeakRef
var _entity_filters: Dictionary[NetwEntity, Callable] = {}
var _anchor_visibility: Dictionary[int, bool] = {}


func _init(anchor: MultiplayerSynchronizer) -> void:
	_anchor_ref = weakref(anchor)


## Configures the anchor for default-deny visibility. The
## [param self_filter] argument is accepted for parity with
## [InterestFilterBinding] but unused: visibility is governed by
## [method MultiplayerSynchronizer.set_visibility_for].
func install_anchor(_self_filter: Callable) -> void:
	var anchor := _anchor()
	if not anchor:
		return
	anchor.public_visibility = false


func install_entity(entity: NetwEntity, filter: Callable) -> void:
	if entity == null or not is_instance_valid(entity.owner):
		return
	if _entity_filters.has(entity):
		return
	_entity_filters[entity] = filter
	for sync in entity.synchronizers():
		if is_instance_valid(sync):
			sync.add_visibility_filter(filter)


func uninstall_entity(entity: NetwEntity) -> void:
	var filter: Callable = _entity_filters.get(entity, Callable())
	if not filter.is_valid():
		return
	if is_instance_valid(entity) and is_instance_valid(entity.owner):
		for sync in entity.synchronizers():
			if is_instance_valid(sync):
				sync.remove_visibility_filter(filter)
	_entity_filters.erase(entity)


## Applies a drive pass. Entity transitions come from the driver;
## anchor transitions are computed here from the live
## [code](kind, viewers, peers)[/code] triple compared against the
## binding's anchor visibility cache.
func apply(
		sync_hides: Array,
		sync_shows: Array,
		peers: Array[int],
		kind: int,
		viewers: Dictionary) -> void:
	var anchor := _anchor()
	for t in sync_hides:
		var sync: MultiplayerSynchronizer = t[0]
		if is_instance_valid(sync):
			sync.update_visibility(t[1])

	if anchor:
		var new_state: Dictionary[int, bool] = {}
		for peer: int in peers:
			var verdict := InterestPolicy.verdict(kind, viewers, peer)
			new_state[peer] = verdict
			var was: bool = _anchor_visibility.get(peer, false)
			if not verdict and was:
				anchor.set_visibility_for(peer, false)
		for peer: int in peers:
			var verdict: bool = new_state[peer]
			var was: bool = _anchor_visibility.get(peer, false)
			if verdict and not was:
				anchor.set_visibility_for(peer, true)
		_anchor_visibility = new_state

	for t in sync_shows:
		var sync: MultiplayerSynchronizer = t[0]
		if is_instance_valid(sync):
			sync.update_visibility(t[1])


## Builds the [member MultiplayerSynchronizer.replication_config] for
## [member InterestSynchronizer.viewers] and
## [member InterestSynchronizer.policy]. See
## [InterestFilterBinding.build_replication_config].
func build_replication_config() -> bool:
	var anchor := _anchor()
	if not anchor or not anchor.owner:
		return false
	anchor.root_path = anchor.get_path_to(anchor.owner)
	var config := SceneReplicationConfig.new()
	_add_spawn_property(config, anchor, "viewers")
	_add_spawn_property(config, anchor, "policy")
	anchor.replication_config = config
	return true


static func _add_spawn_property(
		config: SceneReplicationConfig,
		anchor: MultiplayerSynchronizer,
		property: String) -> void:
	var path := NodePath(
			str(anchor.owner.get_path_to(anchor)) + ":" + property)
	config.add_property(path)
	config.property_set_spawn(path, true)
	config.property_set_replication_mode(
			path,
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)


func installed_entities() -> Array[NetwEntity]:
	var out: Array[NetwEntity] = []
	out.assign(_entity_filters.keys())
	return out


## Returns a snapshot of binding state for [param peer_id]. Mirrors
## [method InterestFilterBinding.dump_state] and adds
## [code]anchor_admitted[/code] = current
## [method MultiplayerSynchronizer.get_visibility_for] verdict.
func dump_state(
		kind: int,
		viewers: Dictionary,
		peer_id: int) -> Dictionary:
	var anchor := _anchor()
	var out: Dictionary = {
		"anchor_path": String(anchor.get_path()) if anchor else "<null>",
		"public_visibility": anchor.public_visibility if anchor else false,
		"anchor_admitted": (anchor.get_visibility_for(peer_id)
				if anchor else false),
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
