## One live binding of a [NetwPropertySet] to the node that declares it through
## [method Netw.configure_property], the route-keyed gather source for a state or
## input group.
##
## The shell captures [member route], [member comp], and [member order_key] at
## registration. The set carries the wire contract, while the binding carries
## the live gather state: the two lanes' rows, the windowed input ring, and the
## row each lane last merged a frame onto.
## [codeblock]
## per pass, authority side:
##   volatile_row()  ─▶ one SYNC_ROW offer, diffed per recipient
##   retained_row()  ─▶ one SYNC_ROW_DELTA offer, sent when a column moved
## [/codeblock]
## A set's [constant NetwPropertySet.Lane.VOLATILE] fields ride
## [constant NetwFrameEnvelope.Channel.SYNC_ROW] freshest-wins, and its
## [constant NetwPropertySet.Lane.RETAINED] fields ride the reliable
## [constant NetwFrameEnvelope.Channel.SYNC_ROW_DELTA] lane only when they
## change, so the two lanes of one set never re-send each other.
##
## Both lanes offer the whole row every pass and neither decides what a
## recipient is owed. [NetwReplicationSend] holds one baseline per peer per
## lane and answers with the columns that moved, which is why a binding carries
## no per-peer book of its own.
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

var _held_row: PackedByteArray = PackedByteArray()

var _held_retained: PackedByteArray = PackedByteArray()

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
	var entity := api._native_core.wrapper_for_route(route) as NetwEntity \
			if route > 0 else null
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


## Returns whether this binding's volatile lane rides the redundant sample ring
## rather than a single row, which is what routes it onto
## [constant NetwFrameEnvelope.Channel.SYNC_ROW_WINDOW].
func is_windowed() -> bool:
	return set.window > 0 and set.stamp == NetwPropertySet.Stamp.STAMP_TICK


## Returns the [constant NetwPropertySet.Lane.VOLATILE] row read off the node in
## [member NetwPropertySet.volatile_schema] column order, the positional form
## [method NetwReplicationSend.run_deferred] grids into a code row. An empty
## array is a refusal: a field is unreadable, the node has freed, or the set
## declares no volatile field at all, and in every one of those a half row must
## not reach the wire.
func volatile_row() -> Array:
	var n := node()
	if not is_instance_valid(n):
		return []
	var fields := _volatile_fields()
	if fields.is_empty():
		return []
	var gathered := _gather_fields(n, fields)
	return gathered[&"values"] if gathered[&"ok"] else []


## Applies one [constant NetwFrameEnvelope.Channel.SYNC_ROW] [param frame]
## through [param send], merging its masked columns onto the row this binding
## already holds and writing the result onto the node when
## [member write_gate] allows. Returns the decoded header, or an empty
## dictionary when the frame does not read against the set's declaration.
##
## The held row is bytes rather than values because the merge target is the
## sender's own code row: a receiver that merged decoded values would compare
## against what its quantizers produced rather than against what the sender
## diffed, and the two disagree exactly where a column is lossy.
func apply_row_frame(
		send: NetwReplicationSend,
		frame: PackedByteArray,
) -> Dictionary:
	var n := node()
	if not is_instance_valid(n) or send == null:
		return { }
	var fields := _volatile_fields()
	if fields.is_empty():
		return { }
	var applied: Dictionary = send.apply(
		set.volatile_schema,
		_held_row,
		frame,
		set.masked,
	)
	if not applied.get("ok", false):
		return { }
	var values: Array = applied["values"]
	if values.size() != fields.size():
		return { }
	_held_row = applied["held"]

	var staged := NetwStagedWrites.new()
	staged.ordinal = int(applied.get("comp", 0))
	staged.tick = int(applied.get("tick", -1))
	staged.ack = int(applied.get("ack", -1))
	staged.whole = bool(applied.get("whole", true))
	staged.values = values
	var keys: Array[StringName] = []
	for field: NetwPropertySet.Column in fields:
		keys.append(field.key)
	staged.keys.assign(keys)
	for index: int in fields.size():
		staged.row[keys[index]] = values[index]
	if write_gate and _apply_values(n, keys, values) != OK:
		return { }
	var header := staged.header()
	if on_applied.is_valid():
		on_applied.call(header)
	return header


## Returns the [constant NetwPropertySet.Lane.RETAINED] row read off the node in
## [member NetwPropertySet.retained_schema] column order. An empty array is a
## refusal on the same terms as [method volatile_row].
##
## The whole row is offered every pass and the diff happens in the lane, which
## is what lets the reliable lane mask each recipient's changes without this
## binding holding a book of its own.
func retained_row() -> Array:
	var n := node()
	if not is_instance_valid(n):
		return []
	var fields := _retained_fields()
	if fields.is_empty():
		return []
	var gathered := _gather_fields(n, fields)
	return gathered[&"values"] if gathered[&"ok"] else []


## Applies one [constant NetwFrameEnvelope.Channel.SYNC_ROW_WINDOW]
## [param frame] through [param send], writing the newest tick it carries onto
## the node and handing every tick it carries to [member on_applied]. Returns
## the decoded header, or an empty dictionary when the frame does not read
## against the set's declaration.
##
## Every repeated tick is delivered, not just the newest. A consumer that took
## the newest alone would discard exactly the ticks the window exists to heal.
func apply_window_frame(
		send: NetwReplicationSend,
		frame: PackedByteArray,
) -> Dictionary:
	var n := node()
	if not is_instance_valid(n) or send == null:
		return { }
	var fields := _volatile_fields()
	if fields.is_empty():
		return { }
	var applied: Dictionary = send.apply_window(set.volatile_schema, frame)
	if not applied.get("ok", false):
		return { }
	var samples: Array = applied["samples"]
	if samples.is_empty():
		return { }

	var keys: Array[StringName] = []
	for field: NetwPropertySet.Column in fields:
		keys.append(field.key)
	var rows: Array = []
	for sample: Dictionary in samples:
		var values: Array = sample["values"]
		if values.size() != fields.size():
			return { }
		var row: Dictionary = { }
		for index: int in keys.size():
			row[keys[index]] = values[index]
		rows.append({ &"tick": int(sample["tick"]), &"payload": row })

	var newest: Dictionary = rows[rows.size() - 1]
	var staged := NetwStagedWrites.new()
	staged.ordinal = int(applied.get("comp", 0))
	staged.tick = int(applied.get("tick", -1))
	staged.ack = int(applied.get("ack", -1))
	staged.keys.assign(keys)
	staged.values = (samples[samples.size() - 1] as Dictionary)["values"]
	staged.row = newest[&"payload"]
	staged.samples = rows
	if write_gate and _apply_values(n, keys, staged.values) != OK:
		return { }
	var header := staged.header()
	if on_applied.is_valid():
		on_applied.call(header)
	return header


## Applies one [constant NetwFrameEnvelope.Channel.SYNC_ROW_DELTA]
## [param frame] through [param send], merging its masked columns onto the
## retained row this binding holds and writing the result onto the node.
## Returns the decoded header, or an empty dictionary when the frame does not
## read against the set's declaration.
##
## The held row is a second one, not the volatile row: the two lanes carry
## disjoint columns under separate plans, and merging a retained frame onto the
## volatile row would apply its mask to the wrong columns.
func apply_retained_row(
		send: NetwReplicationSend,
		frame: PackedByteArray,
) -> Dictionary:
	var n := node()
	if not is_instance_valid(n) or send == null:
		return { }
	var fields := _retained_fields()
	if fields.is_empty():
		return { }
	var applied: Dictionary = send.apply(
		set.retained_schema,
		_held_retained,
		frame,
		true,
	)
	if not applied.get("ok", false):
		return { }
	var values: Array = applied["values"]
	if values.size() != fields.size():
		return { }
	_held_retained = applied["held"]

	var staged := NetwStagedWrites.new()
	staged.ordinal = int(applied.get("comp", 0))
	staged.tick = int(applied.get("tick", -1))
	staged.ack = int(applied.get("ack", -1))
	staged.whole = bool(applied.get("whole", true))
	staged.values = values
	var keys: Array[StringName] = []
	for field: NetwPropertySet.Column in fields:
		keys.append(field.key)
	staged.keys.assign(keys)
	for index: int in fields.size():
		staged.row[keys[index]] = values[index]
	if _apply_values(n, keys, values) != OK:
		return { }
	return staged.header()


# The VOLATILE fields in set order, the wire order the windowed lane encodes.
func _volatile_fields() -> Array:
	var out: Array = []
	for field in set.columns:
		if field.lane == NetwPropertySet.Lane.VOLATILE:
			out.append(field)
	return out


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
	NetwCodec.write_values(
		writer,
		plan["values"],
		plan["quantizers"],
		plan["types"],
	)
	var reader := NetwBitBufferReader.create(writer.to_bytes())
	var canonical := NetwCodec.read_values(
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
	NetwCodec.write_values(
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


# The RETAINED fields in set order, the order NetwPropertySet.retained_schema
# projects, so a mask bit i names _retained_fields()[i].
func _retained_fields() -> Array:
	var out: Array = []
	for field in set.columns:
		if field.lane == NetwPropertySet.Lane.RETAINED:
			out.append(field)
	return out


## Drops both lanes' held rows for [param peer], so a reconnecting peer merges
## the next frame onto nothing rather than onto what the last peer at that id
## held.
func clear_peer(_peer: int) -> void:
	_held_row = PackedByteArray()
	_held_retained = PackedByteArray()
