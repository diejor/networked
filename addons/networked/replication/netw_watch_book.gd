## The dirty-poll engine behind every on-change retained stream: values in,
## per-recipient masks out, and nothing about nodes.
##
## A retained field replicates reliably only when it changes, and a peer that
## just gained the stream must heal with the full row. This book owns that
## compare, stamp, and per-recipient baseline machinery so both front doors
## share one implementation. [NetwSyncCompat] hands it a consumed
## synchronizer's watched values every poll, and the sync pipeline hands it a
## script set's retained fields, both keyed by their own binding identity. The
## book never reads a node, which keeps it route-agnostic and trivially
## portable.
## [codeblock]
## authority, each delta pass          per recipient
##   poll(key, values, readable)          gather changed fields into a stamp
##   mask_for(key, peer) ─▶ [mask, vals]  fields newer than the peer baseline
##   commit(key, peer)                    advance the peer baseline
## [/codeblock]
## An absent baseline is the gain edge: [method mask_for] heals it with the
## full row, exactly the native zero-baseline mechanism. The 64-field ceiling,
## the container aliasing rule, and the absent-baseline heal all live here so a
## caller only reads values and hands them in.
class_name NetwWatchBook
extends RefCounted

## Watched fields ride a 64-bit change mask, the native replicator's ceiling.
## A caller asserts the field count against this before it ever polls.
const FIELD_LIMIT := 64

# key -> _Stream. The key is any hashable binding identity the caller owns.
var _streams: Dictionary = { }


# One keyed stream's dirty-poll state: the last seen copies, the per-field
# change stamps, the monotonic change counter, and the per-peer baselines.
class _Stream:
	extends RefCounted

	var inited: bool = false
	var values: Array = []
	var stamps: Array[int] = []
	var change_counter: int = 0
	# peer id -> change_counter at last commit. Absent means the peer just
	# gained the stream and heals with the full row.
	var baselines: Dictionary = { }


func _stream_for(key: Variant, create: bool) -> _Stream:
	var s: _Stream = _streams.get(key)
	if s == null and create:
		s = _Stream.new()
		_streams[key] = s
	return s


## Polls [param key]'s row of current field [param values], stamping every
## field that changed since the last poll. [param readable] is a parallel
## bool array, and a field marked unreadable keeps its previous stamp so a
## momentarily unresolvable target never reads as a change. An empty
## [param readable] treats every field as readable.
##
## The first poll seeds the baseline row: every readable field stamps as
## changed so the first recipient heals fully.
func poll(key: Variant, values: Array, readable: Array = [ ]) -> void:
	var s := _stream_for(key, true)
	if not s.inited:
		s.change_counter = 1
		for i in values.size():
			var ok: bool = readable[i] if i < readable.size() else true
			s.values.append(_stable_copy(values[i]) if ok else null)
			s.stamps.append(1 if ok else 0)
		s.inited = true
		return
	for i in values.size():
		var ok: bool = readable[i] if i < readable.size() else true
		if not ok:
			continue
		var cur: Variant = values[i]
		var prev: Variant = s.values[i]
		if typeof(cur) == typeof(prev) and cur == prev:
			continue
		s.change_counter += 1
		s.values[i] = _stable_copy(cur)
		s.stamps[i] = s.change_counter


## Returns [code][mask, values][/code] for [param peer]: the bitmask over the
## stream's field order of every field stamped newer than the peer's baseline,
## and those fields' current values in set-bit order. An absent baseline heals
## the peer with the full row. Returns a zero mask and empty values when the
## stream has never been polled.
func mask_for(key: Variant, peer: int) -> Array:
	var s := _stream_for(key, false)
	if s == null or not s.inited:
		return [0, [ ]]
	var baseline := int(s.baselines.get(peer, 0))
	var mask := 0
	var out: Array = [ ]
	for i in s.stamps.size():
		if s.stamps[i] > baseline:
			mask |= 1 << i
			out.append(s.values[i])
	return [mask, out]


## Advances [param peer]'s baseline to the stream's current change counter, so
## its next [method mask_for] carries only fields that change after this send.
## Called once per recipient per delta pass, matching the native per-peer
## baseline advance.
func commit(key: Variant, peer: int) -> void:
	var s := _stream_for(key, false)
	if s:
		s.baselines[peer] = s.change_counter


## Resets [param key]'s dirty-poll state, dropping its stamps and every peer
## baseline. Called when the watched field set changes so the next poll reseeds
## and the next delta heals every recipient with the full new row.
func reset(key: Variant) -> void:
	_streams.erase(key)


## Clears [param key]'s peer baselines while keeping its polled row, so every
## recipient re-heals on the next delta without re-seeding the change stamps.
## The session-restart counterpart of [method reset].
func clear_baselines(key: Variant) -> void:
	var s := _stream_for(key, false)
	if s:
		s.baselines.clear()


## Erases [param peer]'s baseline across every stream, so a reconnecting peer
## heals with the full row on each.
func clear_peer(peer: int) -> void:
	for key: Variant in _streams:
		(_streams[key] as _Stream).baselines.erase(peer)


## Drops the baselines [param key] holds against any peer not in
## [param recipients], because a baseline that outlives a peer's visibility
## must not suppress its full-row heal on re-admission.
func retain_baselines(key: Variant, recipients: Array) -> void:
	var s := _stream_for(key, false)
	if s == null:
		return
	for peer: int in s.baselines.keys():
		if peer not in recipients:
			s.baselines.erase(peer)


## Whether [param key]'s stream has been polled at least once.
func is_inited(key: Variant) -> bool:
	var s := _stream_for(key, false)
	return s != null and s.inited


# A stored copy must not alias a container the game keeps mutating, or the
# dirty poll compares the value against itself forever.
static func _stable_copy(value: Variant) -> Variant:
	match typeof(value):
		TYPE_ARRAY:
			return (value as Array).duplicate(true)
		TYPE_DICTIONARY:
			return (value as Dictionary).duplicate(true)
		TYPE_PACKED_BYTE_ARRAY, TYPE_PACKED_INT32_ARRAY, TYPE_PACKED_INT64_ARRAY, \
		TYPE_PACKED_FLOAT32_ARRAY, TYPE_PACKED_FLOAT64_ARRAY, TYPE_PACKED_STRING_ARRAY, \
		TYPE_PACKED_VECTOR2_ARRAY, TYPE_PACKED_VECTOR3_ARRAY, TYPE_PACKED_COLOR_ARRAY:
			return value.duplicate()
	return value
