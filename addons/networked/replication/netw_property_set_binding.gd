## One live binding of a [NetwPropertySet] to the node that declares it through
## [method Netw.configure_property], the route-keyed gather source for a state or
## input group.
##
## The shell captures [member route], [member comp], and [member order_key] at
## registration. The set carries the wire contract, while the binding carries
## the live gather state: the volatile row, the windowed input ring, and the
## retained lane's [NetwWatchBook].
## [codeblock]
## per pass, authority side:
##   encode_volatile(ordinal, tick, ack) ─▶ one SYNC frame
##   poll_retained()                        gather changed retained fields
##   retained_delta(ordinal, peer)      ─▶ one SYNC_DELTA per recipient
## [/codeblock]
## A set's [constant NetwPropertySet.Lane.VOLATILE] fields ride the SYNC frame
## freshest-wins, and its [constant NetwPropertySet.Lane.RETAINED] fields ride the
## reliable [constant NetwFrameEnvelope.Channel.SYNC_DELTA] lane only when they
## change, so the two lanes of one set never re-send each other.
class_name NetwPropertySetBinding
extends RefCounted

## The declaration this binding is a live instance of.
var set: NetwPropertySet

## Cached entity route. A zero value has not bound to liveness yet.
var route: int

## Registered component address under the entity root.
var comp: int

## Stable registration key used by [NetwSyncModel].
var order_key: StringName

# The declaring node. A weakref so a freed node never keeps the binding alive.
var _node_ref: WeakRef

# The retained lane's single-stream dirty-poll engine.
const _WATCH_KEY := 0

var _watch_book: NetwWatchBook = NetwWatchBook.new()

# Sender side of the masked volatile lane, unused unless
# [member NetwPropertySet.masked] is set. peer -> the last full row that peer is
# provably confirmed to hold; absent means never confirmed, the gain edge that
# heals with a full row.
var _masked_confirmed: Dictionary = { }

# peer -> {seq: row}, full rows staged for a masked send not yet known to have
# landed. A datagram ack promotes the largest entry at or under the acked seq
# to _masked_confirmed and drops everything at or before it.
var _masked_inflight: Dictionary = { }

# peer -> the fields masked in since that peer's baseline last advanced, used as
# a set. A field stays sticky-sent until the ack that promotes a baseline the
# receiver provably holds, which is what closes the revert race: a field that
# changes away from the confirmed baseline and back before its send is acked
# would otherwise diff clean against the stale baseline and strand the interim
# value on the receiver. Cleared whole on promotion, since the new baseline is a
# row the receiver reconstructed and diff-against-it is sufficient afterward.
var _masked_dirty: Dictionary = { }

# The volatile row poll_masked() last read off the node, shared by every
# recipient of one pump. Immutable once polled: poll_masked() builds a fresh
# Dictionary each time and nothing downstream writes into a staged row, which is
# what makes handing one instance to every peer's in-flight ring safe.
var _masked_row: Dictionary = { }
var _masked_row_fields: Array = []
var _masked_row_ok: bool = false

# Receiver side of the masked volatile lane: the last row this binding decoded,
# merged target for a masked frame's partial subset. Kept independent of
# write_gate so a reconciling client (write_gate false) merges against its own
# last-decoded authoritative row rather than the live node, which may hold a
# diverging prediction.
var _masked_last_row: Dictionary = { }

# Windowed input samples, newest last: [[tick, values], ...]. Only a windowed
# input set fills this, trimmed to NetwPropertySet.window every gather.
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

## When true, another carrier owns this set's volatile bytes and the pump sends
## nothing on the volatile lane. The retained lane still pumps, so a reliable
## field lands on schedule either way.
##
## The set survives as declaration and codec while its wire path lives
## elsewhere. A prediction engine sets this on its input binding, whose samples
## ride [constant NetwFrameEnvelope.Channel.PREDICT_COMMAND] with the
## transition each one drove.
var volatile_external: bool = false


## Installs [param feed]'s state half on this binding, replacing whatever the
## previous role left. Pass a fresh [NetwPredict.Feed] to clear it.
##
## The feed fields are wiring rather than state, so they are written as one unit
## by whoever owns the role decision, never poked field by field from wherever
## the decision happened to be made.
func apply_state_feed(feed: NetwPredict.Feed) -> void:
	on_applied = feed.state_on_applied
	write_gate = feed.state_write_gate


## Installs [param feed]'s input half on this binding. See
## [method apply_state_feed].
func apply_input_feed(feed: NetwPredict.Feed) -> void:
	on_applied = feed.input_on_applied
	write_gate = feed.input_write_gate
	volatile_external = feed.input_volatile_external


func _init(binding_set: NetwPropertySet, node: Node) -> void:
	set = binding_set
	_node_ref = weakref(node)


## Returns the declaring node, or [code]null[/code] once it has freed.
func node() -> Node:
	return _node_ref.get_ref() as Node if _node_ref else null


# Returns the mounted flat api when this binding belongs to one.
func _api() -> NetwMultiplayer:
	var n := node()
	return n.multiplayer as NetwMultiplayer \
	if is_instance_valid(n) and n.is_inside_tree() else null


# Resolves the cached route to its entity RID without walking the scene tree.
func _entity_rid(api: NetwMultiplayer) -> RID:
	var entity := api._liveness.entity_of(route) if route > 0 else null
	return entity.rid if entity else RID()


# Reads one field row through the installed gather stage.
func _gather_fields(
		n: Node,
		fields: Array,
		allow_missing: bool = false,
) -> Dictionary:
	var readable: Array[bool] = []
	var gatherer := func() -> Array:
		var values: Array = []
		for column: NetwPropertySet.Column in fields:
			var ok := column.key in n
			readable.append(ok)
			if not ok and not allow_missing:
				return []
			values.append(n.get(column.key) if ok else null)
		return values
	var api := _api()
	var values: Array = api._run_gather_set(
		_entity_rid(api),
		comp,
		gatherer,
	) if api else gatherer.call()
	if readable.size() != fields.size() and values.size() == fields.size():
		readable.clear()
		for _field in fields:
			readable.append(true)
	return {
		&"ok": values.size() == fields.size(),
		&"values": values,
		&"readable": readable,
	}


# Applies one positional row through the installed writer stage.
func _apply_values(
		n: Node,
		keys: Array[StringName],
		values: Array,
) -> Error:
	var applier := func(staged: Array) -> Error:
		if staged.size() != keys.size():
			return ERR_INVALID_DATA
		for index: int in keys.size():
			n.set(keys[index], staged[index])
		return OK
	var api := _api()
	return api._run_apply_set(
		_entity_rid(api),
		comp,
		values,
		applier,
	) if api else applier.call(values)


## Returns [code]true[/code] while the node is alive, in the tree, and holds
## authority over the stream.
func is_active() -> bool:
	var n := node()
	if not is_instance_valid(n):
		return false
	return n.is_inside_tree() and n.is_multiplayer_authority()


## Encodes the volatile [constant NetwFrameEnvelope.Channel.SYNC] frame for
## [param ordinal], stamping [param tick] and [param ack] per the set's
## [member NetwPropertySet.stamp]. A windowed input set instead carries a trailing
## ring of up to [member NetwPropertySet.window] redundant samples so a lost input
## tick heals without a retransmit. Returns an empty array when a field is
## unreadable, so a half row never crosses the wire.
func encode_volatile(ordinal: int, tick: int, ack: int) -> PackedByteArray:
	if set.window > 0 and set.stamp == NetwPropertySet.Stamp.STAMP_TICK:
		return _encode_windowed(ordinal, tick)
	var n := node()
	if not is_instance_valid(n):
		return PackedByteArray()
	var fields := _volatile_fields()
	var gathered := _gather_fields(n, fields)
	if not gathered[&"ok"]:
		return PackedByteArray()
	var quantizers: Array = []
	var types: Array = []
	for index: int in fields.size():
		quantizers.append(fields[index].quantizer)
		types.append(typeof(gathered[&"values"][index]))
	return NetwSyncKernel.encode_volatile(
		ordinal,
		NetwSyncPipeline.volatile_flags(set),
		gathered[&"values"],
		quantizers,
		types,
		tick,
		ack,
	)


func _encode_windowed(ordinal: int, tick: int) -> PackedByteArray:
	var n := node()
	if not is_instance_valid(n):
		return PackedByteArray()
	var fields := _volatile_fields()
	var row := _gather_fields(n, fields)
	if not row[&"ok"]:
		return PackedByteArray()
	_window_rows.append([tick, row[&"values"]])
	while _window_rows.size() > set.window:
		_window_rows.remove_at(0)
	var samples: Array = []
	for i in range(_window_rows.size() - 1, -1, -1):
		var window_row: Array = _window_rows[i]
		samples.append([tick - int(window_row[0]), window_row[1]])
	var quantizers: Array = []
	var types: Array = []
	for index: int in fields.size():
		quantizers.append(fields[index].quantizer)
		types.append(typeof(row[&"values"][index]))
	return NetwSyncKernel.encode_windowed(
		ordinal,
		tick,
		samples,
		quantizers,
		types,
	)


# The VOLATILE fields in set order, the wire order the windowed lane encodes.
func _volatile_fields() -> Array:
	var out: Array = []
	for field in set.columns:
		if field.lane == NetwPropertySet.Lane.VOLATILE:
			out.append(field)
	return out


## Decodes one [constant NetwFrameEnvelope.Channel.SYNC] [param payload] and
## writes its [constant NetwPropertySet.Lane.VOLATILE] values onto the node, returning
## the decoded header [code]{ordinal, tick, ack}[/code] for the timeline and
## prediction feed, or an empty dictionary when the frame is malformed. A
## windowed input frame carries a ring of redundant samples newest first, so the
## freshest sample (age [code]0[/code]) is the one applied and the older rows heal
## only a receiver that missed a tick. A taped frame also returns
## [code]tape_epoch[/code] and its decoded [code]entries[/code].
func apply_volatile(payload: PackedByteArray) -> Dictionary:
	var n := node()
	if not is_instance_valid(n):
		return { }
	var header: Dictionary
	if set.window > 0 and set.stamp == NetwPropertySet.Stamp.STAMP_TICK:
		header = _apply_windowed(n, payload)
	else:
		header = NetwSyncPipeline.apply_volatile_frame(
			n,
			set,
			payload,
			false,
			_masked_last_row,
		)
		if not header.is_empty():
			_masked_last_row = header.get("payload", { })
	if header.is_empty():
		return header
	if write_gate:
		var row: Dictionary = header.get(&"payload", { })
		var keys: Array[StringName] = []
		var values: Array = []
		for key: StringName in row:
			keys.append(key)
			values.append(row[key])
		if _apply_values(n, keys, values) != OK:
			return { }
	if on_applied.is_valid():
		on_applied.call(header)
	return header


func _apply_windowed(n: Node, payload: PackedByteArray) -> Dictionary:
	var keys: Array[StringName] = []
	var quantizers: Array = []
	var types: Array = []
	for field in set.columns:
		if field.lane != NetwPropertySet.Lane.VOLATILE:
			continue
		keys.append(field.key)
		quantizers.append(field.quantizer)
		types.append(NetwScriptModel.get_node_property_type(n, field.key))
	var staged := NetwSyncKernel.decode_windowed(
		payload,
		keys,
		quantizers,
		types,
	)
	if staged == null:
		return { }
	# The freshest sample (age 0) snaps a display receiver. A gated receiver (the
	# server recording input) never snaps, its engine records the rows instead.
	if write_gate:
		if _apply_values(n, staged.keys, staged.values) != OK:
			return { }
	return staged.header()


## Reads every field of the bound [member set] off the declaring node into a
## [code]{key: value}[/code] [Dictionary], the plain-payload snapshot a prediction
## step records into a [NetwTimeline] and reconciles against. Empty once the node
## has freed. This is the set-handle counterpart of a synchronizer's payload
## snapshot, keyed by the fields the script declared.
func snapshot_payload() -> Dictionary:
	var n := node()
	if not is_instance_valid(n):
		return { }
	var gathered := _gather_fields(n, set.columns, true)
	if not gathered[&"ok"]:
		return { }
	var payload: Dictionary = { }
	for index: int in set.columns.size():
		if gathered[&"readable"][index]:
			payload[set.columns[index].key] = gathered[&"values"][index]
	return payload


## Writes a [param payload] produced by [method snapshot_payload] straight onto
## the declaring node, setting only the fields [member set] declares. A key the
## set does not name is ignored, so a reconciliation restore lands exactly the
## set's fields onto the body.
func apply_payload(payload: Dictionary) -> void:
	var n := node()
	if not is_instance_valid(n):
		return
	var keys: Array[StringName] = []
	var values: Array = []
	for column: NetwPropertySet.Column in set.columns:
		if payload.has(column.key) and column.key in n:
			keys.append(column.key)
			values.append(payload[column.key])
	_apply_values(n, keys, values)


## Returns [param payload] with every value the [member set] declares
## round-tripped through that field's [NetwQuantize], the one form both peers
## can agree on.
##
## A predicting client and its authority never see the same float twice: one
## reads a live property, the other reads what survived the wire. Recording the
## round trip instead of the live value makes the two comparable exactly rather
## than approximately. A field with no quantizer round-trips through its raw wire
## form, which is the canonical form for that field: a float rides as 32 bits, so
## a live double comes back rounded to what the receiver will hold.
## [codeblock]
## var recorded := binding.canonicalize_payload(binding.snapshot_payload())
## timeline.record_state(tick + 1, recorded)
## [/codeblock]
func canonicalize_payload(payload: Dictionary) -> Dictionary:
	var plan := _canonical_plan(payload)
	if plan.is_empty():
		return payload.duplicate()
	var writer := NetwBitBufferWriter.new()
	NetwScriptModel.write_values(
		writer,
		plan["values"],
		plan["quantizers"],
		plan["types"],
	)
	var reader := NetwBitBufferReader.create(writer.to_bytes())
	var canonical := NetwScriptModel.read_values(
		reader,
		plan["quantizers"],
		plan["types"],
	)
	var keys: Array = plan["keys"]
	var out := payload.duplicate()
	for i in mini(keys.size(), canonical.size()):
		out[keys[i]] = canonical[i]
	return out


## Returns the wire bytes of [param payload], encoded in [member set] field
## order through the same codecs the frame uses.
##
## The bytes that ship are the bytes that hash, so a fingerprint taken here and
## a fingerprint taken by a peer decoding the frame describe the same values.
## Only the fields [param payload] carries are encoded, so two payloads are
## comparable exactly when they name the same fields.
func canonical_bytes(payload: Dictionary) -> PackedByteArray:
	var plan := _canonical_plan(payload)
	if plan.is_empty():
		return PackedByteArray()
	var writer := NetwBitBufferWriter.new()
	NetwScriptModel.write_values(
		writer,
		plan["values"],
		plan["quantizers"],
		plan["types"],
	)
	return writer.to_bytes()


## Returns what [param property] does in the simulation, or
## [constant NetwPropertySet.PropertyClass.CAUSAL] when the set does not declare it.
##
## A reconciliation reads this to decide which values it may compare and
## restore. Undeclared properties answer causal because that is the class whose
## mistake is a correction rather than a silent divergence.
func property_class_of(property: StringName) -> NetwPropertySet.PropertyClass:
	var field := field_of(property)
	return field.property_class if field else NetwPropertySet.PropertyClass.CAUSAL


## Returns how firmly a recovery pulls [param property] toward the authoritative
## value, or [code]0.0[/code] when it is restored outright.
func converge_stiffness_of(property: StringName) -> float:
	var field := field_of(property)
	return field.converge_stiffness if field else 0.0


## Returns the sibling channel a recovery advances [param property] along, or an
## empty [StringName] when it is restored at the acknowledged value.
func carry_channel_of(property: StringName) -> StringName:
	var field := field_of(property)
	return field.carry_channel if field else &""


## Returns whether [param property] is restored only by a teleport-tier
## recovery.
func teleport_only_of(property: StringName) -> bool:
	var field := field_of(property)
	return field.explicit_teleport_only if field else false


## Returns whether [param property] is excluded from triggering a correction
## on its own.
func reconcile_only_of(property: StringName) -> bool:
	var field := field_of(property)
	return field.explicit_reconcile_only if field else false


## Returns [param property]'s own divergence threshold, or a negative value
## when it inherits the entity's default.
func epsilon_override_of(property: StringName) -> float:
	var field := field_of(property)
	return field.epsilon_override if field else -1.0


## Returns [param property]'s own teleport-tier distance, or a negative value
## when it inherits the entity's default.
func teleport_at_of(property: StringName) -> float:
	var field := field_of(property)
	return field.teleport_at_override if field else -1.0


## Returns the declared [NetwPropertySet.Column] for [param property], or
## [code]null[/code] when this set does not carry it.
func field_of(property: StringName) -> NetwPropertySet.Column:
	if not set:
		return null
	for column: NetwPropertySet.Column in set.columns:
		if column.key == property:
			return column
	return null


# Selects the payload's fields in set order with the codec inputs each needs.
# Empty when the node has freed or the payload names no declared field, which
# leaves both canonical verbs pass-through rather than lossy.
func _canonical_plan(payload: Dictionary) -> Dictionary:
	var n := node()
	if payload.is_empty() or not is_instance_valid(n) or not set:
		return { }
	var keys: Array[StringName] = []
	var values: Array = []
	var quantizers: Array = []
	var types: Array = []
	for field in set.columns:
		if not payload.has(field.key):
			continue
		keys.append(field.key)
		values.append(payload[field.key])
		quantizers.append(field.quantizer)
		types.append(NetwScriptModel.get_node_property_type(n, field.key))
	if keys.is_empty():
		return { }
	return {
		"keys": keys,
		"values": values,
		"quantizers": quantizers,
		"types": types,
	}


## Polls the set's [constant NetwPropertySet.Lane.RETAINED] fields off the node into
## the [NetwWatchBook], stamping every field that changed since the last poll so
## a following [method retained_delta] can mask each recipient's changes.
func poll_retained() -> void:
	var n := node()
	if not is_instance_valid(n):
		return
	var gathered := _gather_fields(n, _retained_fields(), true)
	var values: Array = gathered[&"values"]
	if values.is_empty():
		return
	_watch_book.poll(_WATCH_KEY, values, gathered[&"readable"])


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
			quantizers.append((retained[i] as NetwPropertySet.Column).quantizer)
			types.append(typeof(values[value_index]))
			value_index += 1
	return NetwSyncKernel.encode_retained(
		ordinal,
		mask,
		values,
		quantizers,
		types,
	)


## Applies one reliable [constant NetwFrameEnvelope.Channel.SYNC_DELTA]
## [param payload] to the node: the mask selects retained fields in set order and
## the values decode with each masked field's [NetwQuantize] exactly as
## [method retained_delta] encoded them. Returns [code]true[/code] when the frame
## applied, [code]false[/code] when the mask and value count disagree.
func apply_retained_delta(payload: PackedByteArray) -> bool:
	var n := node()
	if not is_instance_valid(n):
		return false
	var r := NetwBitBufferReader.create(payload)
	NetwCodec.get_safe_varint(r) # ordinal, already resolved to this binding
	var mask := NetwCodec.get_safe_varint(r)
	var retained := _retained_fields()
	var indexes: Array[int] = []
	var quantizers: Array = []
	var types: Array = []
	for i in retained.size():
		if mask & (1 << i):
			var field := retained[i] as NetwPropertySet.Column
			indexes.append(i)
			quantizers.append(field.quantizer)
			types.append(NetwScriptModel.get_node_property_type(n, field.key))
	var keys: Array[StringName] = []
	for column: NetwPropertySet.Column in retained:
		keys.append(column.key)
	var staged := NetwSyncKernel.decode_retained(
		payload,
		keys,
		quantizers,
		types,
	)
	if staged == null or staged.keys.size() != indexes.size():
		return false
	return _apply_values(n, staged.keys, staged.values) == OK


# The RETAINED fields in set order, the field order the watch book polls and
# masks, so a mask bit i names _retained_fields()[i].
func _retained_fields() -> Array:
	var out: Array = []
	for field in set.columns:
		if field.lane == NetwPropertySet.Lane.RETAINED:
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
	for peer: int in _masked_dirty.keys().duplicate():
		if peer not in recipients:
			_masked_dirty.erase(peer)


## Clears the retained peer baselines while keeping the polled row, so every
## recipient re-heals on the next delta after a session restart.
func clear_baselines() -> void:
	_watch_book.clear_baselines(_WATCH_KEY)


## Erases [param peer]'s retained baseline so a reconnecting peer heals fully.
func clear_peer(peer: int) -> void:
	_watch_book.clear_peer(peer)
	_masked_confirmed.erase(peer)
	_masked_inflight.erase(peer)
	_masked_dirty.erase(peer)


## Returns [code]{bytes, row, full}[/code] for a masked-lane volatile send to
## [param peer] under [param ordinal], stamping [param tick] and [param ack] per
## [member NetwPropertySet.stamp] exactly like [method encode_volatile]: [code]bytes[/code]
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
## [br][br]
## A field stays masked in until the ack that promotes a baseline carrying it,
## even once it no longer differs from the confirmed baseline. So a value that
## changes away from the baseline and back inside the in-flight window is still
## sent rather than silently going quiet, which is the sender's half of the
## reconstruction invariant [method NetwSyncPipeline.apply_volatile_frame] states.
## Without it the receiver would strand the interim value it was handed and no
## later frame would correct it.
func masked_delta(ordinal: int, peer: int, tick: int, ack: int) -> Dictionary:
	poll_masked()
	return masked_delta_for(ordinal, peer, tick, ack)


## Reads the masked lane's volatile row off the node once, for every recipient of
## one pump to share.
##
## The row a masked send carries is the same for every peer. Only the confirmed
## baseline it is diffed against differs, so reading the node per recipient costs
## one full gather per peer and returns the same values every time. This is the
## masked lane's half of the rule [method poll_retained] already follows.
## [codeblock]
## binding.poll_masked()                       # one read of the node
## for peer in recipients:
##     binding.masked_delta_for(ord, peer, t, ack)   # no node access
## [/codeblock]
## A node that has freed, or a field the node does not carry, leaves
## [method has_masked_row] false so no half row ever crosses the wire.
## [br][br]
## The polled row is canonical, per [method canonicalize_payload]. The baseline
## a peer confirms is what that peer decoded, so the row diffed against it has
## to be in the same domain: a live value that moved less than one grid step
## encodes to the byte the peer already holds, and diffing raw would mask it in
## to send those identical bytes again.
func poll_masked() -> void:
	_masked_row_ok = false
	var n := node()
	if not is_instance_valid(n):
		return
	_masked_row_fields = _volatile_fields()
	var gathered := _gather_fields(n, _masked_row_fields)
	if not gathered[&"ok"]:
		return
	var row: Dictionary = { }
	for index: int in _masked_row_fields.size():
		row[_masked_row_fields[index].key] = gathered[&"values"][index]
	_masked_row = canonicalize_payload(row)
	_masked_row_ok = true


## Returns whether the last [method poll_masked] read a complete volatile row, so
## a pump can skip its recipient loop whole instead of rediscovering the same
## verdict per peer.
func has_masked_row() -> bool:
	return _masked_row_ok


## Returns [param peer]'s masked frame against the row [method poll_masked] last
## read, in [method masked_delta]'s [code]{bytes, row, full}[/code] shape, or an
## empty [Dictionary] when that poll found no readable row.
##
## Touches no node, so the cost of a recipient is its baseline diff alone. The
## returned [code]row[/code] is the shared polled row, so every recipient's
## [method commit_masked_pending] stages one instance rather than a copy per peer.
func masked_delta_for(ordinal: int, peer: int, tick: int, ack: int) -> Dictionary:
	if not _masked_row_ok:
		return { }
	var fields := _masked_row_fields
	var row := _masked_row
	var base: Variant = _masked_confirmed.get(peer)
	var dirty: Dictionary = _masked_dirty.get_or_add(peer, { })
	var mask := 0
	var values: Array = []
	var quantizers: Array = []
	var types: Array = []
	for i in fields.size():
		var f: NetwPropertySet.Column = fields[i]
		var cur = row[f.key]
		var changed: bool = (base == null or not (base as Dictionary).has(f.key)
				or (base as Dictionary)[f.key] != cur)
		if changed or dirty.has(f.key):
			mask |= 1 << i
			values.append(cur)
			quantizers.append(f.quantizer)
			types.append(typeof(cur))
			dirty[f.key] = true
	if mask == 0:
		return { "bytes": PackedByteArray(), "row": row, "full": false }
	var full := fields.is_empty() or mask == (1 << fields.size()) - 1
	var bytes := NetwSyncKernel.encode_volatile(
		ordinal,
		NetwSyncPipeline.volatile_flags(set) \
				| NetwFrameEnvelope.SYNC_FLAG_MASKED,
		values,
		quantizers,
		types,
		tick,
		ack,
		mask,
	)
	return { "bytes": bytes, "row": row, "full": full }


## Moves [param row] into [param peer]'s in-flight ring under [param seq], the
## datagram seq [ReplicationCore]'s flush just assigned to the send
## that carries it. Called by [method NetwSyncPipeline.commit_pending_masked]
## once per flush, never directly by the pump.
func commit_masked_pending(peer: int, seq: int, row: Dictionary) -> void:
	var inflight: Dictionary = _masked_inflight.get_or_add(peer, { })
	inflight[seq] = row


## Promotes [param peer]'s confirmed masked baseline to the largest in-flight
## row at or under [param acked_seq] (the u16 half-window ring the session
## tracks per peer), dropping every in-flight entry at
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
	_masked_dirty.erase(peer)
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
