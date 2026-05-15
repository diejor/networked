## Push-based visibility registry for a [MultiplayerTree].
##
## Owns the set of active [NetwInterestLayer]s and computes, for every
## [code](peer, entity)[/code] pair, whether the peer should receive
## that entity's synchronizers. Access via [member NetwContext.interest].
## [codeblock]
##     var arena := Netw.ctx(self).interest.create_layer(
##             &"combat:%d" % battle_id,
##             NetwInterestLayer.Policy.ISOLATE)
##     arena.add_participant(player_a)
##     arena.add_participant(player_b)
## [/codeblock]
##
## A [code]ROOT[/code] GRANT layer ([member root]) is created
## automatically; new peers and new entities are added to it so the
## default behaviour is "everyone sees everyone." Scenes, bubbles, and
## combat overlays restrict this default by adding [code]ISOLATE[/code]
## layers that override the ROOT visibility for their members.
##
## Mutations on layers are coalesced into a single deferred flush per
## frame. The flush recomputes only dirty (peer, entity) pairs, orders
## visibility deltas so parents apply before children on show and
## after children on hide ([url]https://github.com/godotengine/godot/issues/68508[/url]),
## then pushes the result to each entity's
## [MultiplayerSynchronizer]s.
class_name NetwInterest
extends RefCounted


## Identifier for the always-present default-visible layer.
const ROOT_ID := &"root"


## Emitted after each flush completes. [param delta_count] is the
## number of visibility transitions applied. Useful for tests and
## debug UIs.
signal flushed(delta_count: int)


## The implicit default-visible layer ([code]GRANT[/code] policy).
## Every spawned entity is added as a subject; every connected peer
## is added as a member. Removing a peer from [member root] makes
## them invisible-to-default; useful when paired with a more
## restrictive layer.
var root: NetwInterestLayer


var _tree_ref: WeakRef
var _layers: Dictionary[StringName, NetwInterestLayer] = {}

# Inverted indexes used to bound the work performed during a flush.
var _layers_by_member: Dictionary[int, Array] = {}
var _layers_by_subject: Dictionary[NetwEntity, Array] = {}

# Cached visibility: peer_id -> Dictionary[NetwEntity, bool]. Filter
# callbacks read directly from these dictionaries; mutating them
# outside [method _flush] would desync the engine.
var _visible: Dictionary[int, Dictionary] = {}

# Entities the interest system is tracking. Used to know which
# synchronizer filters to install/refresh on flush.
var _known_entities: Dictionary[NetwEntity, bool] = {}

var _dirty_peers: Dictionary[int, bool] = {}
var _dirty_entities: Dictionary[NetwEntity, bool] = {}
var _flush_scheduled: bool = false
var _flushing: bool = false


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)
	root = NetwInterestLayer.new(
			ROOT_ID, NetwInterestLayer.Policy.GRANT, self)
	_layers[ROOT_ID] = root


## Creates and registers a new layer under [param id]. Returns the
## layer, or [code]null[/code] if [param id] is already taken.
func create_layer(
		id: StringName,
		policy: NetwInterestLayer.Policy = NetwInterestLayer.Policy.GRANT,
) -> NetwInterestLayer:
	if _layers.has(id):
		Netw.dbg.warn(
				"NetwInterest.create_layer: id `%s` already exists.",
				[id], func(m): push_warning(m))
		return null
	var layer := NetwInterestLayer.new(id, policy, self)
	_layers[id] = layer
	return layer


## Returns the layer registered under [param id], or [code]null[/code].
func layer(id: StringName) -> NetwInterestLayer:
	return _layers.get(id)


## Returns every registered layer, including [member root].
func all_layers() -> Array[NetwInterestLayer]:
	var out: Array[NetwInterestLayer] = []
	out.assign(_layers.values())
	return out


## Snapshot of the entities currently visible to [param peer_id]
## under the last flushed state.
func visible_subjects_for(peer_id: int) -> Array[NetwEntity]:
	var view: Dictionary = _visible.get(peer_id, {})
	var out: Array[NetwEntity] = []
	out.assign(view.keys())
	return out


## Snapshot of the peers that currently see [param entity] under the
## last flushed state.
func viewers_of(entity: NetwEntity) -> Array[int]:
	var out: Array[int] = []
	for peer_id in _visible:
		var view: Dictionary = _visible[peer_id]
		if view.has(entity):
			out.append(peer_id)
	return out


## Synchronously flushes pending mutations. Tests and interactive
## tooling can call this to observe the result without yielding; game
## code should let the deferred flush run on its own.
func flush_now() -> void:
	_flush()


# ---------------------------------------------------------------------------
# Internal hooks invoked by NetwInterestLayer and NetwEntity.
# ---------------------------------------------------------------------------

func _on_member_added(layer_: NetwInterestLayer, peer_id: int) -> void:
	var arr: Array = _layers_by_member.get_or_add(peer_id, [])
	if layer_ not in arr:
		arr.append(layer_)
	_dirty_peers[peer_id] = true
	_schedule_flush()


func _on_member_removed(layer_: NetwInterestLayer, peer_id: int) -> void:
	var arr: Array = _layers_by_member.get(peer_id, [])
	arr.erase(layer_)
	if arr.is_empty():
		_layers_by_member.erase(peer_id)
	_dirty_peers[peer_id] = true
	_schedule_flush()


func _on_subject_added(
		layer_: NetwInterestLayer, entity: NetwEntity) -> void:
	var arr: Array = _layers_by_subject.get_or_add(entity, [])
	if layer_ not in arr:
		arr.append(layer_)
	_track_entity(entity)
	_dirty_entities[entity] = true
	_schedule_flush()


func _on_subject_removed(
		layer_: NetwInterestLayer, entity: NetwEntity) -> void:
	var arr: Array = _layers_by_subject.get(entity, [])
	arr.erase(layer_)
	if arr.is_empty():
		_layers_by_subject.erase(entity)
	_dirty_entities[entity] = true
	_schedule_flush()


func _on_layer_disposed(layer_: NetwInterestLayer) -> void:
	if layer_ == root:
		Netw.dbg.error(
				"NetwInterest: root layer cannot be disposed.", [],
				func(m): push_error(m))
		return
	for peer_id in layer_.members():
		var arr: Array = _layers_by_member.get(peer_id, [])
		arr.erase(layer_)
		if arr.is_empty():
			_layers_by_member.erase(peer_id)
		_dirty_peers[peer_id] = true
	for entity in layer_.subjects():
		var arr: Array = _layers_by_subject.get(entity, [])
		arr.erase(layer_)
		if arr.is_empty():
			_layers_by_subject.erase(entity)
		_dirty_entities[entity] = true
	_layers.erase(layer_.id)
	_schedule_flush()


# Auto-attach default participation for a freshly connected peer.
func _on_peer_connected(peer_id: int) -> void:
	root.add_member(peer_id)


# Remove a disconnected peer from every layer and clear its cached
# view so stale entries don't leak.
func _on_peer_disconnected(peer_id: int) -> void:
	var layers_for_peer: Array = _layers_by_member.get(
			peer_id, []).duplicate()
	for layer_ in layers_for_peer:
		(layer_ as NetwInterestLayer).remove_member(peer_id)
	_visible.erase(peer_id)


# Auto-add an entity to ROOT once it enters the tree. Called by
# NetwEntity from owner_tree_entered.
func _on_entity_ready(entity: NetwEntity) -> void:
	root.add_subject(entity)


func _track_entity(entity: NetwEntity) -> void:
	if _known_entities.has(entity):
		return
	_known_entities[entity] = true
	entity._install_interest_filter(self)


# ---------------------------------------------------------------------------
# Flush.
# ---------------------------------------------------------------------------

func _schedule_flush() -> void:
	if _flush_scheduled or _flushing:
		return
	_flush_scheduled = true
	var tree := _tree_ref.get_ref() as MultiplayerTree
	if not tree or not tree.is_inside_tree():
		return
	tree.get_tree().process_frame.connect(
			_flush, CONNECT_ONE_SHOT | CONNECT_DEFERRED)


func _flush() -> void:
	if _flushing:
		return
	_flushing = true
	_flush_scheduled = false

	var deltas: Array[Delta] = []
	var seen_pairs: Dictionary = {}

	for peer_id in _dirty_peers:
		var new_view := _resolve_visible_set(peer_id)
		var old_view: Dictionary = _visible.get(peer_id, {})
		for entity in new_view:
			seen_pairs[_pair_key(peer_id, entity)] = true
			if not old_view.has(entity):
				deltas.append(Delta.new(peer_id, entity, true))
		for entity in old_view:
			seen_pairs[_pair_key(peer_id, entity)] = true
			if not new_view.has(entity):
				deltas.append(Delta.new(peer_id, entity, false))
		_visible[peer_id] = new_view

	for entity in _dirty_entities:
		var affected_peers := _affected_peers_for(entity)
		for peer_id in affected_peers:
			var key := _pair_key(peer_id, entity)
			if seen_pairs.has(key):
				continue
			seen_pairs[key] = true
			var view: Dictionary = _visible.get_or_add(peer_id, {})
			var was_visible := view.has(entity)
			var is_visible := _resolve_pair(peer_id, entity)
			if is_visible == was_visible:
				continue
			if is_visible:
				view[entity] = true
			else:
				view.erase(entity)
			deltas.append(Delta.new(peer_id, entity, is_visible))

	_dirty_peers.clear()
	_dirty_entities.clear()

	_apply_ordered(deltas)
	_flushing = false
	flushed.emit(deltas.size())


func _affected_peers_for(entity: NetwEntity) -> Array[int]:
	var peers: Dictionary[int, bool] = {}
	for layer_ in _layers_by_subject.get(entity, []):
		for peer_id in (layer_ as NetwInterestLayer)._members:
			peers[peer_id] = true
	# ISOLATE layers without this entity as subject still affect
	# their members' visibility on this entity (outward block).
	for peer_id in _layers_by_member:
		for layer_ in _layers_by_member[peer_id]:
			if (layer_ as NetwInterestLayer).policy \
					== NetwInterestLayer.Policy.ISOLATE:
				peers[peer_id] = true
				break
	var out: Array[int] = []
	out.assign(peers.keys())
	return out


func _resolve_visible_set(peer_id: int) -> Dictionary:
	var view: Dictionary = {}
	var member_layers: Array = _layers_by_member.get(peer_id, [])

	var isolates: Array[NetwInterestLayer] = []
	var grants: Array[NetwInterestLayer] = []
	var member_denies: Array[NetwInterestLayer] = []
	for layer_ in member_layers:
		var l := layer_ as NetwInterestLayer
		match l.policy:
			NetwInterestLayer.Policy.ISOLATE: isolates.append(l)
			NetwInterestLayer.Policy.GRANT: grants.append(l)
			NetwInterestLayer.Policy.DENY: member_denies.append(l)

	if isolates.is_empty():
		for l in grants:
			for entity in l._subjects:
				view[entity] = true
	else:
		# Intersection of isolate subject sets.
		var smallest := isolates[0]
		for l in isolates:
			if l._subjects.size() < smallest._subjects.size():
				smallest = l
		for entity in smallest._subjects:
			var in_all := true
			for l in isolates:
				if not l._subjects.has(entity):
					in_all = false
					break
			if in_all:
				view[entity] = true

	# DENY where the peer is a member: hide subjects co-located with
	# the peer in that layer. (DENY only fires when both sides are in
	# the layer; see resolution table.)
	for l in member_denies:
		for entity in l._subjects:
			view.erase(entity)

	return view


func _resolve_pair(peer_id: int, entity: NetwEntity) -> bool:
	# Slow path used when an entity's subject membership changed but
	# the peer wasn't otherwise dirty. Recomputes one cell.
	var member_layers: Array = _layers_by_member.get(peer_id, [])
	var has_isolate := false
	var visible := false

	for layer_ in member_layers:
		var l := layer_ as NetwInterestLayer
		match l.policy:
			NetwInterestLayer.Policy.ISOLATE:
				has_isolate = true
				if l._subjects.has(entity):
					visible = true
				else:
					return false  # outward block wins immediately.
			NetwInterestLayer.Policy.GRANT:
				if not has_isolate and l._subjects.has(entity):
					visible = true
			NetwInterestLayer.Policy.DENY:
				if l._subjects.has(entity):
					return false

	# When isolates exist, visibility is gated entirely by them.
	if has_isolate:
		return visible

	# No isolates, no grant hit yet -> not visible.
	return visible


func _pair_key(peer_id: int, entity: NetwEntity) -> String:
	return "%d|%d" % [peer_id, entity.get_instance_id()]


func _apply_ordered(deltas: Array[Delta]) -> void:
	if deltas.is_empty():
		return
	var shows: Array[Delta] = []
	var hides: Array[Delta] = []
	for d in deltas:
		if d.visible:
			shows.append(d)
		else:
			hides.append(d)
	shows.sort_custom(_by_depth_shallow_first)
	hides.sort_custom(_by_depth_deep_first)
	for d in shows:
		_apply_one(d)
	for d in hides:
		_apply_one(d)


func _by_depth_shallow_first(a: Delta, b: Delta) -> bool:
	return _entity_depth(a.entity) < _entity_depth(b.entity)


func _by_depth_deep_first(a: Delta, b: Delta) -> bool:
	return _entity_depth(a.entity) > _entity_depth(b.entity)


func _entity_depth(entity: NetwEntity) -> int:
	var d := 0
	var current := entity.parent_entity()
	while current:
		d += 1
		current = current.parent_entity()
	return d


func _apply_one(d: Delta) -> void:
	if not is_instance_valid(d.entity):
		return
	for sync in d.entity.synchronizers():
		if not is_instance_valid(sync):
			continue
		sync.set_visibility_for(d.peer_id, d.visible)
	for layer_ in _layers_by_subject.get(d.entity, []):
		var l := layer_ as NetwInterestLayer
		if not l.has_member(d.peer_id):
			continue
		if d.visible:
			l.interest_enter.emit(d.entity, d.peer_id)
		else:
			l.interest_exit.emit(d.entity, d.peer_id)


## Single visibility transition recorded during a flush. Public so
## tests and debug tooling can inspect [signal flushed] history.
class Delta extends RefCounted:
	var peer_id: int
	var entity: NetwEntity
	var visible: bool

	func _init(p: int, e: NetwEntity, v: bool) -> void:
		peer_id = p
		entity = e
		visible = v
