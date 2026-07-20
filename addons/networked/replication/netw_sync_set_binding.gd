## One live binding of a [NetwSyncSet] to the node that declares it through
## [method Netw.configure_property], the route-keyed gather source for a state or
## input group.
##
## The binding never stores its route. It resolves the declaring node's
## [NetwEntity] and route each pass, so a reparent is followed for free and a
## freed node drops out on the next prune. The set carries the wire contract, the
## binding carries the live gather state: the volatile row, the windowed input
## ring, and the retained lane's [NetwWatchBook].
## [codeblock]
## per pass, authority side:
##   encode_volatile(ordinal, tick, ack) ─▶ one SYNC frame
##   poll_retained()                        gather changed retained fields
##   retained_delta(ordinal, peer)      ─▶ one SYNC_DELTA per recipient
## [/codeblock]
## A set's [constant NetwSyncSet.Lane.VOLATILE] fields ride the SYNC frame
## freshest-wins, and its [constant NetwSyncSet.Lane.RETAINED] fields ride the
## reliable [constant NetwFrameEnvelope.Channel.SYNC_DELTA] lane only when they
## change, so the two lanes of one set never re-send each other.
class_name NetwSyncSetBinding
extends RefCounted

## The declaration this binding is a live instance of.
var set: NetwSyncSet

# The declaring node. A weakref so a freed node never keeps the binding alive.
var _node_ref: WeakRef

# The retained lane's single-stream dirty-poll engine.
const _WATCH_KEY := 0

var _watch_book: NetwWatchBook = NetwWatchBook.new()

# Sender side of the masked volatile lane, unused unless
# [member NetwSyncSet.masked] is set. peer -> the last full row that peer is
# provably confirmed to hold; absent means never confirmed, the gain edge that
# heals with a full row.
var _masked_confirmed: Dictionary = { }

# peer -> {seq: row}, full rows staged for a masked send not yet known to have
# landed. A datagram ack promotes the largest entry at or under the acked seq
# to _masked_confirmed and drops everything at or before it.
var _masked_inflight: Dictionary = { }

# Receiver side of the masked volatile lane: the last row this binding decoded,
# merged target for a masked frame's partial subset. Kept independent of
# write_gate so a reconciling client (write_gate false) merges against its own
# last-decoded authoritative row rather than the live node, which may hold a
# diverging prediction.
var _masked_last_row: Dictionary = { }

# Windowed input samples, newest last: [[tick, values], ...]. Only a windowed
# input set fills this, trimmed to NetwSyncSet.window every gather.
var _window_rows: Array = []

## Called after a frame applies, with the decoded header
## [code]{ordinal, tick, ack, payload, samples?}[/code]. A prediction engine
## subscribes here so a state receive drives reconciliation and an input receive
## opens the consume cursor, the way the synchronizer callbacks did. Unset for a
## plain display set, which just snaps the node.
var on_applied: Callable = Callable()

## When false, a received volatile frame decodes but does not snap the node, so a
## predicting client reconciles the authoritative row against its own prediction
## instead of overwriting the predicted body. The apply hook still fires with the
## decoded payload. Mirrors the synchronizer write-through gate.
var write_gate: bool = true

## The tick the pump stamps outgoing frames with, or [code]-1[/code] to stamp the
## pump's own tick. A prediction engine writes the input's authored tick here so
## the frame carries the tick it was gathered, not the tick it was sent.
var authored_tick: int = -1

## The reconciliation ack the pump frames on a state set's outgoing frame, or
## [code]-1[/code] for the no-input-consumed sentinel. A prediction engine writes
## the last input tick the server has processed here.
var reconcile_ack: int = -1

## When true, the pump holds this pass's volatile row and sends nothing on the
## volatile lane. The retained lane still pumps, so a reliable field lands on
## schedule either way.
##
## A volatile frame is a truthful [code](tick, ack, payload)[/code] triple: the
## payload is gathered live at send time, so it may only ship on a tick whose
## [member reconcile_ack] advanced to match it. A prediction engine sets this on a
## tick that consumed no input, where the body has coasted past the ack it would
## otherwise re-stamp and the owning client would read the difference as its own
## divergence.
var suppress_volatile: bool = false

## The predicted [NetwTimeline] a windowed input set sources its redundancy window
## from. When set, the window is the unacked tail of this timeline rather than the
## binding's own gathered ring, so a predicting client resends the same samples it
## recorded and the server heals a lost input tick from them. Null for a plain
## windowed set, which rings its own gathered values.
var window_timeline: NetwTimeline = null

## The last input tick the receiver has acknowledged, flooring the windowed input
## set so an acknowledged sample is never re-sent. Only consulted when
## [member window_timeline] drives the window.
var window_floor: int = -1


func _init(binding_set: NetwSyncSet, node: Node) -> void:
	set = binding_set
	_node_ref = weakref(node)


## Returns the declaring node, or [code]null[/code] once it has freed.
func node() -> Node:
	return _node_ref.get_ref() as Node if _node_ref else null


## Returns [code]true[/code] while the node is alive, in the tree, and holds
## authority over the stream.
func is_active() -> bool:
	var n := node()
	if not is_instance_valid(n):
		return false
	return n.is_inside_tree() and n.is_multiplayer_authority()


## Returns the declaring node's current route through [param liveness], or a
## non-positive value when the node has no live entity this pass.
func route(liveness: NetwLivenessInterface) -> int:
	var n := node()
	if not is_instance_valid(n):
		return -1
	var entity := NetwEntity.of(n)
	return liveness.route_of(entity) if entity else -1


## Encodes the volatile [constant NetwFrameEnvelope.Channel.SYNC] frame for
## [param ordinal], stamping [param tick] and [param ack] per the set's
## [member NetwSyncSet.stamp]. A windowed input set instead carries a trailing
## ring of up to [member NetwSyncSet.window] redundant samples so a lost input
## tick heals without a retransmit. Returns an empty array when a field is
## unreadable, so a half row never crosses the wire.
func encode_volatile(ordinal: int, tick: int, ack: int) -> PackedByteArray:
	if set.window > 0 and set.stamp == NetwSyncSet.Stamp.STAMP_TICK:
		return _encode_windowed(ordinal, tick)
	return NetwSyncPipeline.encode_volatile_frame(node(), set, ordinal, tick, ack)


func _encode_windowed(ordinal: int, tick: int) -> PackedByteArray:
	if window_timeline != null:
		return _encode_windowed_from_timeline(ordinal, tick)
	var n := node()
	if not is_instance_valid(n):
		return PackedByteArray()
	var gathered := NetwSyncPipeline.gather_volatile(n, set)
	if not gathered[0]:
		return PackedByteArray()
	_window_rows.append([tick, gathered[1]])
	while _window_rows.size() > set.window:
		_window_rows.remove_at(0)
	var samples: Array = []
	for i in range(_window_rows.size() - 1, -1, -1):
		var row: Array = _window_rows[i]
		samples.append([tick - int(row[0]), row[1]])
	return NetwFrameEnvelope.encode_sync_frame(
		{
			"ordinal": ordinal,
			"flags": NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_WINDOWED,
			"samples": samples,
			"quantizers": gathered[2],
			"types": gathered[3],
			"tick": tick,
		},
	)


# The prediction input window: the redundant samples are the unacked tail of the
# owning client's predicted timeline, floored by the acknowledged tick so a sample
# the server already consumed is never re-sent, the input carrier's rule. Falls
# back to one live sample so the window is always a sole carrier before any input
# has been recorded.
func _encode_windowed_from_timeline(ordinal: int, tick: int) -> PackedByteArray:
	var n := node()
	if not is_instance_valid(n):
		return PackedByteArray()
	var vfields := _volatile_fields()
	var samples: Array = []
	var from := maxi(window_floor + 1, tick - set.window + 1)
	var rows := window_timeline.inputs_in_range(from, tick)
	for i in range(rows.size() - 1, -1, -1):
		var row: Dictionary = rows[i]
		var input: Dictionary = row["input"]
		var values: Array = []
		for f in vfields:
			values.append(input.get(f.key, n.get(f.key)))
		samples.append([tick - int(row["tick"]), values])
	if samples.is_empty():
		var live: Array = []
		for f in vfields:
			if not (f.key in n):
				return PackedByteArray()
			live.append(n.get(f.key))
		samples.append([0, live])
	var quantizers: Array = []
	for f in vfields:
		quantizers.append(f.quantizer)
	var types: Array = []
	for v in samples[0][1]:
		types.append(typeof(v))
	return NetwFrameEnvelope.encode_sync_frame(
		{
			"ordinal": ordinal,
			"flags": NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_WINDOWED,
			"samples": samples,
			"quantizers": quantizers,
			"types": types,
			"tick": tick,
		},
	)


# The VOLATILE fields in set order, the wire order the windowed lane encodes.
func _volatile_fields() -> Array:
	var out: Array = []
	for field in set.fields:
		if field.lane == NetwSyncSet.Lane.VOLATILE:
			out.append(field)
	return out


## Decodes one [constant NetwFrameEnvelope.Channel.SYNC] [param payload] and
## writes its [constant NetwSyncSet.Lane.VOLATILE] values onto the node, returning
## the decoded header [code]{ordinal, tick, ack}[/code] for the timeline and
## prediction feed, or an empty dictionary when the frame is malformed. A windowed
## input frame carries a ring of redundant samples newest first, so the freshest
## sample (age [code]0[/code]) is the one applied and the older rows heal only a
## receiver that missed a tick.
func apply_volatile(payload: PackedByteArray) -> Dictionary:
	var n := node()
	if not is_instance_valid(n):
		return { }
	var header: Dictionary
	if set.window > 0 and set.stamp == NetwSyncSet.Stamp.STAMP_TICK:
		header = _apply_windowed(n, payload)
	else:
		header = NetwSyncPipeline.apply_volatile_frame(
			n,
			set,
			payload,
			write_gate,
			_masked_last_row,
		)
		if not header.is_empty():
			_masked_last_row = header.get("payload", { })
	if header.is_empty():
		return header
	if on_applied.is_valid():
		on_applied.call(header)
	return header


func _apply_windowed(n: Node, payload: PackedByteArray) -> Dictionary:
	var keys: Array[StringName] = []
	var quantizers: Array = []
	var types: Array = []
	for field in set.fields:
		if field.lane != NetwSyncSet.Lane.VOLATILE:
			continue
		keys.append(field.key)
		quantizers.append(field.quantizer)
		types.append(NetwScriptModel.get_node_property_type(n, field.key))
	var frame := NetwFrameEnvelope.decode_sync_frame(payload, quantizers, types)
	var samples: Array = frame.get("samples", [])
	if samples.is_empty():
		return { }
	var tick: int = frame.get("tick", -1)
	# Decode every redundant sample into a {tick, payload} row so the input engine
	# records the whole window and heals a tick the receiver missed. Samples are
	# newest first, age 0 is the frame tick.
	var rows: Array = []
	for sample: Array in samples:
		var age: int = sample[0]
		var values: Array = sample[1]
		if values.size() != keys.size():
			continue
		var row: Dictionary = { }
		for i in keys.size():
			row[keys[i]] = values[i]
		rows.append({ "tick": tick - age, "payload": row })
	if rows.is_empty():
		return { }
	# The freshest sample (age 0) snaps a display receiver. A gated receiver (the
	# server recording input) never snaps, its engine records the rows instead.
	if write_gate:
		var freshest: Dictionary = rows[0]["payload"]
		for k: StringName in freshest:
			n.set(k, freshest[k])
	return {
		"ordinal": frame.get("ordinal", 0),
		"tick": tick,
		"ack": frame.get("ack", -1),
		"payload": rows[0]["payload"],
		"samples": rows,
	}


## Reads every field of the bound [member set] off the declaring node into a
## [code]{key: value}[/code] [Dictionary], the plain-payload snapshot a prediction
## step records into a [NetwTimeline] and reconciles against. Empty once the node
## has freed. This is the set-handle counterpart of a synchronizer's payload
## snapshot, keyed by the fields the script declared.
func snapshot_payload() -> Dictionary:
	return NetwSyncPipeline.gather_payload(node(), set)


## Writes a [param payload] produced by [method snapshot_payload] straight onto
## the declaring node, setting only the fields [member set] declares. A key the
## set does not name is ignored, so a reconciliation restore lands exactly the
## set's fields onto the body.
func apply_payload(payload: Dictionary) -> void:
	NetwSyncPipeline.apply_payload(node(), set, payload)


## Polls the set's [constant NetwSyncSet.Lane.RETAINED] fields off the node into
## the [NetwWatchBook], stamping every field that changed since the last poll so
## a following [method retained_delta] can mask each recipient's changes.
func poll_retained() -> void:
	var n := node()
	if not is_instance_valid(n):
		return
	var values: Array = []
	var readable: Array = []
	for field in set.fields:
		if field.lane != NetwSyncSet.Lane.RETAINED:
			continue
		var ok: bool = field.key in n
		readable.append(ok)
		values.append(n.get(field.key) if ok else null)
	if values.is_empty():
		return
	_watch_book.poll(_WATCH_KEY, values, readable)


## Returns the reliable [constant NetwFrameEnvelope.Channel.SYNC_DELTA] bytes for
## [param peer] under [param ordinal]: the mask of retained fields newer than the
## peer's baseline and their values, each bit-packed with its field's configured
## [NetwQuantize]. Returns an empty array when nothing changed, and advances the
## peer's baseline either way.
func retained_delta(ordinal: int, peer: int) -> PackedByteArray:
	if not _watch_book.is_inited(_WATCH_KEY):
		return PackedByteArray()
	var selected := _watch_book.mask_for(_WATCH_KEY, peer)
	var mask: int = selected[0]
	var values: Array = selected[1]
	_watch_book.commit(_WATCH_KEY, peer)
	if mask == 0:
		return PackedByteArray()
	# Select the codecs of the masked fields in set-bit order, so the reliable
	# lane quantizes exactly like the volatile lane instead of shipping raw.
	var retained := _retained_fields()
	var quantizers: Array = []
	var types: Array = []
	var value_index := 0
	for i in retained.size():
		if mask & (1 << i):
			quantizers.append((retained[i] as NetwSyncSet.Field).quantizer)
			types.append(typeof(values[value_index]))
			value_index += 1
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(w, ordinal)
	NetwCodec.put_varint(w, mask)
	NetwScriptModel.write_values(w, values, quantizers, types)
	return w.to_bytes()


## Applies one reliable [constant NetwFrameEnvelope.Channel.SYNC_DELTA]
## [param payload] to the node: the mask selects retained fields in set order and
## the values decode with each masked field's [NetwQuantize] exactly as
## [method retained_delta] encoded them. Returns [code]true[/code] when the frame
## applied, [code]false[/code] when the mask and value count disagree.
func apply_retained_delta(payload: PackedByteArray) -> bool:
	var n := node()
	if not is_instance_valid(n):
		return false
	var r := NetwBitBuffer.Reader.new(payload)
	NetwCodec.get_safe_varint(r) # ordinal, already resolved to this binding
	var mask := NetwCodec.get_safe_varint(r)
	var retained := _retained_fields()
	var indexes: Array[int] = []
	var quantizers: Array = []
	var types: Array = []
	for i in retained.size():
		if mask & (1 << i):
			var field := retained[i] as NetwSyncSet.Field
			indexes.append(i)
			quantizers.append(field.quantizer)
			types.append(NetwScriptModel.get_node_property_type(n, field.key))
	var values := NetwScriptModel.read_values(r, quantizers, types)
	if values.size() != indexes.size():
		return false
	for i in indexes.size():
		n.set((retained[indexes[i]] as NetwSyncSet.Field).key, values[i])
	return true


# The RETAINED fields in set order, the field order the watch book polls and
# masks, so a mask bit i names _retained_fields()[i].
func _retained_fields() -> Array:
	var out: Array = []
	for field in set.fields:
		if field.lane == NetwSyncSet.Lane.RETAINED:
			out.append(field)
	return out


## Drops the retained baselines held against any peer not in [param recipients],
## so a baseline never outlives a peer's visibility and suppresses its full-row
## heal on re-admission.
func retain_baselines(recipients: Array) -> void:
	_watch_book.retain_baselines(_WATCH_KEY, recipients)


## Drops the masked-lane confirmed baseline and in-flight ring held against any
## peer not in [param recipients], the masked lane's counterpart of
## [method retain_baselines]: a peer that regains the route or re-admits
## interest heals with a full row instead of diffing against a stale one
## under the keyframe-on-gain rule.
func retain_masked_baselines(recipients: Array) -> void:
	for peer: int in _masked_confirmed.keys().duplicate():
		if peer not in recipients:
			_masked_confirmed.erase(peer)
	for peer: int in _masked_inflight.keys().duplicate():
		if peer not in recipients:
			_masked_inflight.erase(peer)


## Clears the retained peer baselines while keeping the polled row, so every
## recipient re-heals on the next delta after a session restart.
func clear_baselines() -> void:
	_watch_book.clear_baselines(_WATCH_KEY)


## Erases [param peer]'s retained baseline so a reconnecting peer heals fully.
func clear_peer(peer: int) -> void:
	_watch_book.clear_peer(peer)
	_masked_confirmed.erase(peer)
	_masked_inflight.erase(peer)


## Returns [code]{bytes, row, full}[/code] for a masked-lane volatile send to
## [param peer] under [param ordinal], stamping [param tick] and [param ack] per
## [member NetwSyncSet.stamp] exactly like [method encode_volatile]: [code]bytes[/code]
## is the [constant NetwFrameEnvelope.SYNC_FLAG_MASKED] frame carrying only the
## volatile fields that differ from [param peer]'s confirmed baseline (empty
## when nothing differs, so a caught-up peer costs nothing this pass), and
## [code]row[/code] is the current full row regardless, for the caller to stage
## pending this send's eventual ack via [method commit_masked_pending].
## [code]full[/code] is [code]true[/code] when every field masked in, the gain
## edge (an absent baseline heals [param peer] with every field, mirroring
## [NetwWatchBook]'s absent-baseline rule) or a coincidental all-fields-changed
## pass, for a caller counting how often the lane falls back to a full row.
## Returns an empty dictionary when the node has freed or a field is
## unreadable, so a half row never crosses the wire.
func masked_delta(ordinal: int, peer: int, tick: int, ack: int) -> Dictionary:
	var n := node()
	if not is_instance_valid(n):
		return { }
	var fields := _volatile_fields()
	var row: Dictionary = { }
	for f in fields:
		if not (f.key in n):
			return { }
		row[f.key] = n.get(f.key)
	var base: Variant = _masked_confirmed.get(peer)
	var mask := 0
	var values: Array = []
	var quantizers: Array = []
	var types: Array = []
	for i in fields.size():
		var f: NetwSyncSet.Field = fields[i]
		var cur = row[f.key]
		if base == null or not (base as Dictionary).has(f.key) or (base as Dictionary)[f.key] != cur:
			mask |= 1 << i
			values.append(cur)
			quantizers.append(f.quantizer)
			types.append(typeof(cur))
	if mask == 0:
		return { "bytes": PackedByteArray(), "row": row, "full": false }
	var full := fields.is_empty() or mask == (1 << fields.size()) - 1
	var bytes := NetwFrameEnvelope.encode_sync_frame(
		{
			"ordinal": ordinal,
			"flags": NetwSyncPipeline.volatile_flags(set) | NetwFrameEnvelope.SYNC_FLAG_MASKED,
			"values": values,
			"quantizers": quantizers,
			"types": types,
			"tick": tick,
			"ack": ack,
			"mask": mask,
		},
	)
	return { "bytes": bytes, "row": row, "full": full }


## Moves [param row] into [param peer]'s in-flight ring under [param seq], the
## datagram seq [NetwReplicationInterface]'s flush just assigned to the send
## that carries it. Called by [method NetwSyncPipeline.commit_pending_masked]
## once per flush, never directly by the pump.
func commit_masked_pending(peer: int, seq: int, row: Dictionary) -> void:
	var inflight: Dictionary = _masked_inflight.get_or_add(peer, { })
	inflight[seq] = row


## Promotes [param peer]'s confirmed masked baseline to the largest in-flight
## row at or under [param acked_seq] (the u16 half-window ring, matching
## [method NetwMultiplayer.peer_state_ack]), dropping every in-flight entry at
## or before it. A stalled or never-acked peer keeps its older baseline rather
## than corrupting it, the same never-corrupts guarantee the retained lane's
## reliable delivery gives for free. No-op when [param peer] has nothing
## in-flight.
func advance_masked_ack(peer: int, acked_seq: int) -> void:
	var inflight: Dictionary = _masked_inflight.get(peer, { })
	if inflight.is_empty():
		return
	var best := -1
	for seq: int in inflight:
		if _seq_at_or_before(seq, acked_seq) and (best == -1 or _seq_is_fresher(seq, best)):
			best = seq
	if best == -1:
		return
	_masked_confirmed[peer] = inflight[best]
	for seq: int in inflight.keys().duplicate():
		if seq == best or _seq_at_or_before(seq, best):
			inflight.erase(seq)


# Half-window u16 freshness compare, duplicated from
# NetwMultiplayer._seq_is_fresher (private there) rather than exposed, since
# the masked lane is the only other consumer of seq-ring math.
static func _seq_is_fresher(a: int, b: int) -> bool:
	return a != b and ((a - b) & 0xFFFF) < 32768


static func _seq_at_or_before(seq: int, acked: int) -> bool:
	return seq == acked or _seq_is_fresher(acked, seq)
