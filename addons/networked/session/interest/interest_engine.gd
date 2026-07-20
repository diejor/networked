## Pure interest verdict core over packed peer rows.
##
## Entity keys are opaque. This class stores and compares them but never
## dereferences them. Peer bits are assigned outside the engine and remain
## stable for the session. Mutations change intake state. [method recompute]
## produces a value delta, and [method commit] advances the immutable read
## matrix.
class_name InterestEngine
extends RefCounted

enum Policy {
	HIDE_FROM_OUTSIDERS,
	HIDE_FROM_INSIDERS,
}

var _layers: Dictionary = { }
var _entities: Dictionary = { }
var _layer_rows: Dictionary = { }
var _committed: Dictionary = { }
var _committed_memberships: Dictionary = { }
var _dirty_layers: Dictionary = { }
var _dirty_entities: Dictionary = { }
var _removed: Dictionary = { }
var _removed_order: Dictionary = { }
var _live_peers := PackedInt64Array()
var _last_stats := InterestStats.new()
var _revision: int = 0


## Replaces one layer's viewer row and [enum Policy].
func set_layer(
		id: StringName,
		viewers: PackedInt64Array,
		policy: int,
) -> void:
	assert(not id.is_empty(), "InterestEngine.set_layer: id is empty")
	assert(
		policy == Policy.HIDE_FROM_OUTSIDERS
		or policy == Policy.HIDE_FROM_INSIDERS,
		"InterestEngine.set_layer: invalid policy",
	)
	var previous: Dictionary = _layers.get(id, { })
	if not previous.is_empty() \
			and int(previous[&"policy"]) == policy \
			and InterestBitSet.equals(previous[&"viewers"], viewers):
		return
	_layers[id] = {
		&"viewers": viewers.duplicate(),
		&"policy": policy,
	}
	_dirty_layers[id] = true
	_mark_layer_members_dirty(id)
	_touch()


## Removes [param id] and recomputes entities that referenced it.
func remove_layer(id: StringName) -> void:
	if not _layers.has(id):
		return
	_layers.erase(id)
	_dirty_layers[id] = true
	_mark_layer_members_dirty(id)
	_touch()


## Replaces the layer memberships for [param key].
func set_membership(key, layer_ids: Array[StringName]) -> void:
	var record := _entity_record(key)
	var normalized: Array[StringName] = []
	for id in layer_ids:
		if id.is_empty() or id in normalized:
			continue
		normalized.append(id)
		_ensure_layer(id)
	var previous: Array[StringName] = record[&"layers"]
	if previous == normalized:
		return
	record[&"layers"] = normalized
	_mark_entity_tree_dirty(key)
	_touch()


## Replaces the opaque parent key for [param key].
func set_parent(key, parent_key) -> void:
	assert(key != parent_key, "InterestEngine.set_parent: entity is its parent")
	var record := _entity_record(key)
	if record[&"parent"] == parent_key:
		return
	_assert_no_parent_cycle(key, parent_key)
	record[&"parent"] = parent_key
	_mark_entity_tree_dirty(key)
	_touch()


## Replaces the synchronizer intent row for [param key].
func set_intent(key, mask: PackedInt64Array) -> void:
	var record := _entity_record(key)
	if not bool(record[&"intent_all"]) \
			and InterestBitSet.equals(record[&"intent"], mask):
		return
	record[&"intent_all"] = false
	record[&"intent"] = mask.duplicate()
	_mark_entity_tree_dirty(key)
	_touch()


## Makes synchronizer intent admit every live peer for [param key].
func set_intent_all(key) -> void:
	var record := _entity_record(key)
	if bool(record[&"intent_all"]):
		return
	record[&"intent_all"] = true
	record[&"intent"] = PackedInt64Array()
	_mark_entity_tree_dirty(key)
	_touch()


## Replaces the deterministic [code](depth, route)[/code] order key.
func set_order_key(key, depth: int, route: int) -> void:
	assert(depth >= 0, "InterestEngine.set_order_key: depth is negative")
	assert(route >= 0, "InterestEngine.set_order_key: route is negative")
	var record := _entity_record(key)
	if int(record[&"depth"]) == depth and int(record[&"route"]) == route:
		return
	record[&"depth"] = depth
	record[&"route"] = route
	_dirty_entities[key] = true
	_touch()


## Replaces the row of currently live peer bits.
func set_live_peers(bits: PackedInt64Array) -> void:
	if InterestBitSet.equals(_live_peers, bits):
		return
	_live_peers = bits.duplicate()
	for id in _layers:
		_dirty_layers[id] = true
	for key in _entities:
		_dirty_entities[key] = true
	_touch()


## Removes [param key] and makes its previous committed row hide on recompute.
func remove_entity(key) -> void:
	if not _entities.has(key) and not _committed.has(key):
		return
	if _entities.has(key):
		var record: Dictionary = _entities[key]
		_removed_order[key] = [record[&"depth"], record[&"route"]]
	_entities.erase(key)
	_removed[key] = true
	_dirty_entities.erase(key)
	for child in _entities:
		var child_record: Dictionary = _entities[child]
		if child_record[&"parent"] == key:
			child_record[&"parent"] = null
			_mark_entity_tree_dirty(child)
	_touch()


## Computes row changes without advancing the committed matrix.
func recompute() -> InterestDelta:
	var delta := InterestDelta.new()
	delta.commit_revision = _revision
	var layer_rows := _layer_rows.duplicate()
	_recompute_layers(layer_rows, delta.stats)
	var desired_rows: Dictionary = { }
	var changed_rows: Dictionary = { }
	var transition_rows: Array = []
	var layer_transition_rows: Array = []
	var ordered := _ordered_keys(_dirty_entities.keys(), false)
	# TODO: Fan this fold out per root forest only behind a profile gate. Each
	# forest reads its own records and immutable layer masks. Parent-before-child
	# order inside one forest must remain sequential.
	for key in ordered:
		if not _entities.has(key):
			continue
		var desired := _compute_entity_row(key, layer_rows, desired_rows)
		desired_rows[key] = desired
		delta.stats.entities_recomputed += 1
		_append_change(key, desired, delta, changed_rows, transition_rows)
		_append_layer_changes(key, layer_rows, layer_transition_rows)
	var removed := _ordered_removed_keys()
	for key in removed:
		_append_removed_change(key, delta, changed_rows, transition_rows)
		_append_removed_layer_changes(key, layer_transition_rows)
	_finalize_transitions(transition_rows, delta)
	_finalize_layer_transitions(layer_transition_rows, delta)
	delta.layer_rows = layer_rows
	delta.memberships = _membership_snapshot()
	delta.removed_keys = removed
	delta.stats.words_per_row = _live_peers.size()
	delta.stats.edges = _edge_count_after(changed_rows, removed)
	delta.stats.shows = delta.shows.size()
	delta.stats.hides = delta.hides.size()
	return delta


## Commits [param delta] as the matrix visible to row readers.
func commit(delta: InterestDelta) -> void:
	for index in delta.keys.size():
		_committed[delta.keys[index]] = delta.new_rows[index].duplicate()
	for key in delta.removed_keys:
		_committed.erase(key)
	_layer_rows = delta.layer_rows.duplicate()
	_committed_memberships = delta.memberships.duplicate(true)
	_last_stats = delta.stats
	if _revision == delta.commit_revision:
		_dirty_layers.clear()
		_dirty_entities.clear()
		_removed.clear()
		_removed_order.clear()


## Returns the committed row for [param key].
func row_of(key) -> PackedInt64Array:
	var row: PackedInt64Array = _committed.get(key, PackedInt64Array())
	return InterestBitSet.resized(row, _live_peers.size())


## Returns the row [param key] will have after [param delta] commits.
func row_after(key, delta: InterestDelta) -> PackedInt64Array:
	var index := delta.keys.find(key)
	if index >= 0:
		return delta.new_rows[index].duplicate()
	if key in delta.removed_keys:
		return InterestBitSet.empty(_live_peers.size() * 63)
	return row_of(key)


## Returns whether committed [param key] admits [param bit].
func test(key, bit: int) -> bool:
	return InterestBitSet.test(row_of(key), bit)


## Returns a copy of every committed row.
func rows() -> Dictionary:
	var out: Dictionary = { }
	for key in _committed:
		out[key] = row_of(key)
	return out


## Returns whether intake state contains [param key].
func has_entity(key) -> bool:
	return _entities.has(key)


## Returns committed admitted edges attributed to [param layer_id].
func layer_edge_count(layer_id: StringName) -> int:
	if not _layer_rows.has(layer_id):
		return 0
	var row: PackedInt64Array = _layer_rows[layer_id]
	var members := 0
	for key in _committed_memberships:
		var memberships: Array = _committed_memberships[key]
		if layer_id in memberships:
			members += 1
	return members * InterestBitSet.popcount(row)


## Names the first current term that denies [param bit] for [param key].
func explain(key, bit: int) -> String:
	if not _entities.has(key):
		return "entity is not registered"
	if not InterestBitSet.test(_live_peers, bit):
		return "peer bit is not live"
	var current = key
	while current != null and _entities.has(current):
		var record: Dictionary = _entities[current]
		if not bool(record[&"intent_all"]) \
				and not InterestBitSet.test(record[&"intent"], bit):
			return "synchronizer intent denies at route %d" % record[&"route"]
		var memberships: Array[StringName] = record[&"layers"]
		if not memberships.is_empty():
			var admitted := false
			for id in memberships:
				if _layer_admits_bit(id, bit):
					admitted = true
					break
			if not admitted:
				return "layer composition denies at route %d" % record[&"route"]
		current = record[&"parent"]
	return "admitted"


## Returns counters from the last committed recompute.
func stats() -> InterestStats:
	return _last_stats


# Recomputes dirty layer masks before any entity reads them.
func _recompute_layers(rows: Dictionary, out_stats: InterestStats) -> void:
	var ids: Array = _dirty_layers.keys()
	ids.sort()
	# TODO: Fan this loop out per layer only if profiling ever shows recompute
	# matters. A layer reads only its viewers, policy, and the live-peer set.
	for id in ids:
		if not _layers.has(id):
			rows.erase(id)
			continue
		var layer: Dictionary = _layers[id]
		var viewers: PackedInt64Array = layer[&"viewers"]
		if int(layer[&"policy"]) == Policy.HIDE_FROM_OUTSIDERS:
			rows[id] = InterestBitSet.intersect(viewers, _live_peers)
		else:
			rows[id] = InterestBitSet.subtract(_live_peers, viewers)
		out_stats.layers_recomputed += 1


# Computes one entity grant and applies its parent clamp.
func _compute_entity_row(
		key,
		layer_rows: Dictionary,
		desired_rows: Dictionary,
) -> PackedInt64Array:
	var record: Dictionary = _entities[key]
	var memberships: Array[StringName] = record[&"layers"]
	var grant: PackedInt64Array
	if memberships.is_empty():
		grant = _live_peers.duplicate()
	else:
		grant = InterestBitSet.empty(_live_peers.size() * 63)
		for id in memberships:
			var admit: PackedInt64Array = layer_rows.get(
				id,
				PackedInt64Array(),
			)
			grant = InterestBitSet.union(grant, admit)
	if not bool(record[&"intent_all"]):
		grant = InterestBitSet.intersect(grant, record[&"intent"])
	var parent = record[&"parent"]
	if parent != null:
		var parent_row: PackedInt64Array = desired_rows.get(parent, row_of(parent))
		grant = InterestBitSet.intersect(grant, parent_row)
	return InterestBitSet.intersect(grant, _live_peers)


# Adds one changed entity row and its bit transitions.
func _append_change(
		key,
		desired: PackedInt64Array,
		delta: InterestDelta,
		changed_rows: Dictionary,
		transitions: Array,
) -> void:
	var old := row_of(key)
	changed_rows[key] = desired
	if InterestBitSet.equals(old, desired):
		return
	var record: Dictionary = _entities[key]
	var order := [int(record[&"depth"]), int(record[&"route"])]
	delta.keys.append(key)
	delta.old_rows.append(old)
	delta.new_rows.append(desired)
	delta.order_keys.append(order)
	_append_transition_bits(key, old, desired, order, transitions)


# Adds the final hide row for one removed key.
func _append_removed_change(
		key,
		delta: InterestDelta,
		changed_rows: Dictionary,
		transitions: Array,
) -> void:
	var old := row_of(key)
	var desired := InterestBitSet.empty(_live_peers.size() * 63)
	changed_rows[key] = desired
	if InterestBitSet.equals(old, desired):
		return
	var order: Array = _removed_order.get(key, [0, 0])
	delta.keys.append(key)
	delta.old_rows.append(old)
	delta.new_rows.append(desired)
	delta.order_keys.append(order)
	_append_transition_bits(key, old, desired, order, transitions)


# Expands one row difference into sortable transition values.
func _append_transition_bits(
		key,
		old: PackedInt64Array,
		desired: PackedInt64Array,
		order: Array,
		out: Array,
) -> void:
	var shows := InterestBitSet.subtract(desired, old)
	var hides := InterestBitSet.subtract(old, desired)
	for bit in InterestBitSet.bits(shows):
		out.append([key, bit, order[0], order[1], true])
	for bit in InterestBitSet.bits(hides):
		out.append([key, bit, order[0], order[1], false])


# Adds per-layer edge changes for one current entity.
func _append_layer_changes(
		key,
		layer_rows: Dictionary,
		out: Array,
) -> void:
	var record: Dictionary = _entities[key]
	var current: Array[StringName] = record[&"layers"]
	var previous: Array = _committed_memberships.get(key, [])
	var layer_ids: Array[StringName] = []
	for id in previous + current:
		if id not in layer_ids:
			layer_ids.append(id)
	layer_ids.sort()
	var order := [int(record[&"depth"]), int(record[&"route"])]
	for id in layer_ids:
		var old := (
				_layer_rows.get(id, PackedInt64Array())
				if id in previous
				else PackedInt64Array()
		)
		var desired := (
				layer_rows.get(id, PackedInt64Array())
				if id in current
				else PackedInt64Array()
		)
		_append_layer_transition_bits(id, key, old, desired, order, out)


# Adds per-layer hides for one removed entity.
func _append_removed_layer_changes(key, out: Array) -> void:
	var previous: Array = _committed_memberships.get(key, [])
	var order: Array = _removed_order.get(key, [0, 0])
	for id in previous:
		var old: PackedInt64Array = _layer_rows.get(
			id,
			PackedInt64Array(),
		)
		_append_layer_transition_bits(
			id,
			key,
			old,
			PackedInt64Array(),
			order,
			out,
		)


# Expands one attributed layer-row difference.
func _append_layer_transition_bits(
		id: StringName,
		key,
		old: PackedInt64Array,
		desired: PackedInt64Array,
		order: Array,
		out: Array,
) -> void:
	for bit in InterestBitSet.bits(InterestBitSet.subtract(desired, old)):
		out.append([id, key, bit, order[0], order[1], true])
	for bit in InterestBitSet.bits(InterestBitSet.subtract(old, desired)):
		out.append([id, key, bit, order[0], order[1], false])


# Sorts transition values and copies their public pair shape.
func _finalize_transitions(transitions: Array, delta: InterestDelta) -> void:
	var shows: Array = []
	var hides: Array = []
	for transition in transitions:
		if transition[4]:
			shows.append(transition)
		else:
			hides.append(transition)
	shows = _ordered_transitions(shows, false)
	hides = _ordered_transitions(hides, true)
	for transition in shows:
		delta.shows.append([transition[0], transition[1]])
	for transition in hides:
		delta.hides.append([transition[0], transition[1]])


# Sorts attributed layer transitions into their public tuple shape.
func _finalize_layer_transitions(
		transitions: Array,
		delta: InterestDelta,
) -> void:
	var shows: Array = []
	var hides: Array = []
	for transition in transitions:
		if transition[5]:
			shows.append(transition)
		else:
			hides.append(transition)
	shows = _ordered_layer_transitions(shows, false)
	hides = _ordered_layer_transitions(hides, true)
	for transition in shows:
		delta.layer_shows.append(
			[transition[0], transition[1], transition[2]],
		)
	for transition in hides:
		delta.layer_hides.append(
			[transition[0], transition[1], transition[2]],
		)


# Returns keys ordered by their snapshot order key.
func _ordered_keys(source: Array, deeper_first: bool) -> Array:
	var out: Array = []
	for key in source:
		var inserted := false
		for index in out.size():
			if _key_before(key, out[index], deeper_first):
				out.insert(index, key)
				inserted = true
				break
		if not inserted:
			out.append(key)
	return out


# Returns removed keys ordered by their captured snapshot key.
func _ordered_removed_keys() -> Array:
	var out: Array = []
	for key in _removed:
		var inserted := false
		for index in out.size():
			if _removed_key_before(key, out[index]):
				out.insert(index, key)
				inserted = true
				break
		if not inserted:
			out.append(key)
	return out


# Returns transitions in the required ancestry order.
func _ordered_transitions(source: Array, deeper_first: bool) -> Array:
	var out: Array = []
	for transition in source:
		var inserted := false
		for index in out.size():
			if _transition_before(transition, out[index], deeper_first):
				out.insert(index, transition)
				inserted = true
				break
		if not inserted:
			out.append(transition)
	return out


# Returns attributed transitions in deterministic ancestry order.
func _ordered_layer_transitions(source: Array, deeper_first: bool) -> Array:
	var out: Array = []
	for transition in source:
		var inserted := false
		for index in out.size():
			if _layer_transition_before(
				transition,
				out[index],
				deeper_first,
			):
				out.insert(index, transition)
				inserted = true
				break
		if not inserted:
			out.append(transition)
	return out


func _key_before(left, right, deeper_first: bool) -> bool:
	var left_record: Dictionary = _entities[left]
	var right_record: Dictionary = _entities[right]
	return _order_before(
		[int(left_record[&"depth"]), int(left_record[&"route"])],
		[int(right_record[&"depth"]), int(right_record[&"route"])],
		deeper_first,
	)


func _removed_key_before(left, right) -> bool:
	return _order_before(_removed_order[left], _removed_order[right], false)


func _transition_before(left: Array, right: Array, deeper_first: bool) -> bool:
	return _order_before([left[2], left[3]], [right[2], right[3]], deeper_first)


func _layer_transition_before(
		left: Array,
		right: Array,
		deeper_first: bool,
) -> bool:
	var left_order := [left[3], left[4]]
	var right_order := [right[3], right[4]]
	if left_order != right_order:
		return _order_before(left_order, right_order, deeper_first)
	return String(left[0]) < String(right[0])


func _order_before(left: Array, right: Array, deeper_first: bool) -> bool:
	if left[0] != right[0]:
		return left[0] > right[0] if deeper_first else left[0] < right[0]
	return left[1] < right[1]


# Returns or creates one entity intake record.
func _entity_record(key) -> Dictionary:
	assert(key != null, "InterestEngine: entity key is null")
	_removed.erase(key)
	_removed_order.erase(key)
	if _entities.has(key):
		return _entities[key]
	var record := {
		&"layers": [] as Array[StringName],
		&"parent": null,
		&"intent": PackedInt64Array(),
		&"intent_all": true,
		&"depth": 0,
		&"route": 0,
	}
	_entities[key] = record
	_dirty_entities[key] = true
	return record


# Creates a default-deny layer during mutation intake.
func _ensure_layer(id: StringName) -> void:
	if _layers.has(id):
		return
	_layers[id] = {
		&"viewers": PackedInt64Array(),
		&"policy": Policy.HIDE_FROM_OUTSIDERS,
	}
	_dirty_layers[id] = true


# Marks every member of one layer dirty.
func _mark_layer_members_dirty(id: StringName) -> void:
	for key in _entities:
		var memberships: Array[StringName] = _entities[key][&"layers"]
		if id in memberships:
			_mark_entity_tree_dirty(key)


# Marks one entity and all descendants dirty.
func _mark_entity_tree_dirty(root) -> void:
	_dirty_entities[root] = true
	var changed := true
	while changed:
		changed = false
		for key in _entities:
			if _dirty_entities.has(key):
				continue
			var parent = _entities[key][&"parent"]
			if parent != null and _dirty_entities.has(parent):
				_dirty_entities[key] = true
				changed = true


# Rejects a parent link that would create a cycle.
func _assert_no_parent_cycle(key, parent_key) -> void:
	var current = parent_key
	while current != null and _entities.has(current):
		assert(current != key, "InterestEngine.set_parent: parent cycle")
		current = _entities[current][&"parent"]


# Returns whether one current layer admits a peer bit.
func _layer_admits_bit(id: StringName, bit: int) -> bool:
	if not _layers.has(id):
		return false
	var layer: Dictionary = _layers[id]
	var viewer := InterestBitSet.test(layer[&"viewers"], bit)
	if int(layer[&"policy"]) == Policy.HIDE_FROM_OUTSIDERS:
		return viewer
	return not viewer


# Counts edges after applying this recompute over the committed matrix.
func _edge_count_after(changed: Dictionary, removed: Array) -> int:
	var seen: Dictionary = { }
	var out := 0
	for key in _committed:
		if key in removed:
			continue
		var row: PackedInt64Array = changed.get(key, _committed[key])
		out += InterestBitSet.popcount(row)
		seen[key] = true
	for key in changed:
		if seen.has(key) or key in removed:
			continue
		out += InterestBitSet.popcount(changed[key])
	return out


# Captures current memberships for the next attributed diff.
func _membership_snapshot() -> Dictionary:
	var out: Dictionary = { }
	for key in _entities:
		var memberships: Array[StringName] = _entities[key][&"layers"]
		out[key] = memberships.duplicate()
	return out


func _touch() -> void:
	_revision += 1

# TODO: Accept batched membership commands once a spatial driver produces
# membership columns away from the main thread.
# TODO: Re-key opaque entity keys on RID at the native construction swap.
# TODO: Hand committed rows to encode tasks as captured inputs. Rows remain
# stable between flushes, so encode work must never re-enter this engine.
