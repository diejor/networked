## One row-major binding of a [SchemaRecord], the ordered subset of its
## columns a script replicates under one [enum Record] kind.
##
## The set owns no shape. A column's key, type, stride, and quantizer belong to
## the schema, which is also what the column-major table and [NetwDatabase]
## read, so the three consumers cannot drift. What the set owns is the binding:
## which columns are members, in what wire order, on which lane, and under which
## delivery knobs.
## [codeblock]
## NetwPropertySet
##  ┠╴ schema     the SchemaRecord every column's shape is read from
##  ┠╴ columns    ordered [NetwPropertySet.Column], the wire order
##  ┃    ┠╴ schema_column  int   the address in the schema, the one address
##  ┃    ┠╴ key            StringName   read from the schema column
##  ┃    ┠╴ quantizer      NetwQuantize read from the schema column
##  ┃    ┠╴ lane           VOLATILE freshest-wins vs RETAINED reliable-on-change
##  ┃    ┖╴ watch          bool  the lane axis as a flag, kept for derivation
##  ┠╴ trigger    TICK pump vs ON_CHANGE dirty send vs ON_DEMAND request
##  ┠╴ stamp      NONE, TICK, or TICK_ACK framing on the payload
##  ┠╴ record     NONE, STATE/INPUT timeline, or BROADCAST display kind
##  ┠╴ window     0 = none, N = redundant volatile samples
##  ┠╴ audience   PUBLIC vs SERVER_ONLY recipient narrowing
##  ┠╴ cadence    TICK sender pump vs ON_DEMAND request
##  ┠╴ profile    PLAIN payload vs STAMPED tick-framed payload
##  ┠╴ policy     who may author the stream (NetwScriptModel.Policy)
##  ┠╴ channel    the NetwFrameEnvelope.Channel the set rides
##  ┖╴ reliable   ON_DEMAND transmit reliability (TICK decides per send)
## [/codeblock]
## Membership is explicit because a script's schema holds every configured
## property, including the ones that only persist. A column that joins no set
## rides no lane, which is what keeps a persisted-only value off the wire by
## construction rather than by a negation mark.
##
## [br][br]The order is the wire order, so [method keys] and
## [method quantizers] are parallel arrays a positional codec walks in lockstep,
## and a receiver decodes by position with no per-column tag on the wire.
## The per-set delivery knobs are [member trigger], [member stamp],
## [member record], [member window], and [member audience]. Three of them are
## the axis form of a coarser flag kept for derivation.
## [codeblock]
## axis         coarse flag   both answer
## trigger      cadence       when a send fires
## stamp        profile       what framing the payload carries
## Column.lane  Column.watch  which delivery lane the column rides
## [/codeblock]
##
## Membership, order, lane, and the schema's shape are the compatibility
## contract, and [method wire_hash] folds all four. Two declarations that
## disagree on any of them read each other's bytes wrong, so the set is the unit
## a spawn-time skew check validates.
## [br][br][b]Granularity[/b][br]
## The knobs [member trigger], [member stamp], [member window], [member audience],
## and [member masked] each bind the whole set, never one column, because the set
## is one datagram frame that fires, reaches a recipient, and carries redundancy
## as a unit. A column that needs different delivery leaves the frame rather than
## reshaping it, so finer control means another set, not a per-column flag. A set
## derives per script from one [enum Record] kind ([method from_script]), so the
## two doors to a second set are a different kind on the same node or the same
## kind on a child node with its own script.
## [codeblock]
## # different delivery on one node: split by record kind
## Netw.configure_property(self, &"position").state()     # rewound, public
## Netw.configure_property(self, &"aim_dir").broadcast()  # trusted, no history
##
## # same kind, different window: the child node's script owns a second input set
## Netw.configure_property(self, &"motion").input().windowed(3)  # on the body
## Netw.configure_property(self, &"look").input().windowed(6)    # on a child
## [/codeblock]
## Two members of one set that name the same knob differently never split it. The
## last one wins and [method from_property_configs] warns.
class_name NetwPropertySet
extends RefCounted

## When the set's values leave this peer.
##
## [constant TICK] rides the per-tick sender pump on [NetwSyncPipeline].
## [constant ON_CHANGE] rides the same pump but only when a field changed.
## [constant ON_DEMAND] leaves only when [method Netw.sync_property] or
## [method Netw.emit_entity_signal] is called.
enum Cadence {
	TICK,
	ON_DEMAND,
	ON_CHANGE,
}

## How the payload bytes are framed.
##
## [constant PLAIN] is the bare positional payload. [constant STAMPED] prepends
## the authoring-tick framing the interpolation and prediction clocks read.
enum Profile {
	PLAIN,
	STAMPED,
}

## The [member trigger] axis, the send condition for the set's fields.
##
## [constant TRIGGER_TICK] sends every eligible pass. [constant TRIGGER_ON_CHANGE]
## sends only when a field changed. [constant TRIGGER_ON_DEMAND] sends only on an
## explicit request. It is the finer-grained restatement of [enum Cadence].
enum Trigger {
	TRIGGER_TICK,
	TRIGGER_ON_CHANGE,
	TRIGGER_ON_DEMAND,
}

## The [member stamp] axis, the tick framing carried on the payload.
##
## [constant STAMP_NONE] is the bare payload. [constant STAMP_TICK] frames the
## authoring tick the interpolation and prediction clocks read.
## [constant STAMP_TICK_ACK] additionally carries the reconciliation ack, the
## state stream's framing.
enum Stamp {
	STAMP_NONE,
	STAMP_TICK,
	STAMP_TICK_ACK,
}

## The kind of a per-tick synced field: who owns the value, who sees it, and
## whether the server polices it. One kind per field.
## [codeblock]
## # the server owns the body, everyone sees it, hit detection can rewind it
## Netw.configure_property(self, &"position").state()
##
## # the client owns its controls, only the server sees them and re-runs them to verify
## Netw.configure_property(self, &"move_dir").input()
##
## # the client owns its aim arrow, everyone sees it, nobody checks it
## Netw.configure_property(self, &"aim_dir").broadcast()
## [/codeblock]
## Three questions tell them apart.
## [codeblock]
## question       state()         input()          broadcast()
## authored by    the server      the controller   the controller
## reaches        everyone        the server only  everyone
## the server     rewinds it      verifies it      trusts it, no check
## example        body pose       movement keys    aim arrow
## [/codeblock]
## The last row is the only rule that matters: the server keeps history for a
## stream exactly when it must be able to second-guess the author.
## [constant RECORD_STATE] is the server's own truth and [constant RECORD_INPUT] a
## claim it verifies, both kept in a [NetwEntity] timeline for rewind.
## [constant RECORD_BROADCAST] is trusted display and keeps nothing.
## [constant RECORD_NONE] is a field synced on demand, in no per-tick set.
enum Record {
	RECORD_NONE,
	RECORD_STATE,
	RECORD_INPUT,
	RECORD_BROADCAST,
}

## The [member Column.lane] axis, a column's delivery lane.
##
## [constant VOLATILE] columns ride the [constant NetwFrameEnvelope.Channel.SYNC]
## frame freshest-wins, [constant RETAINED] columns ride the reliable
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA] lane only when they change.
enum Lane {
	VOLATILE,
	RETAINED,
}

## The [member Column.property_class] axis, what a column's value does in the
## simulation.
##
## A reconciliation has to restore the values the next step reads from and may
## not touch the ones it recomputes, so the class is what tells the two apart.
## [constant CAUSAL] fields are antecedents of the recurrence, so they are
## compared and restored. [constant DERIVED] fields are recomputed by the body
## from causal ones, so restoring them writes a value the next step overwrites.
## [constant COSMETIC] fields reach display only, so comparing them would
## correct a simulation over a value no simulation reads.
## [br][br]The class therefore decides the scope of a state fingerprint as well
## as the scope of a restore. A non-causal field is computed from the raw values
## behind the canonical ones, so two peers reach it from inputs that differ below
## the causal grid, and one of them left in the compare would make bit-equality
## unreachable however well the causal fields are sized. Only a causal field
## decides whether two peers reproduced a transition.
## [codeblock]
##             simulation reads   compared   restored
## CAUSAL      yes                yes        yes
## DERIVED     no, recomputed     no         yes, overwritten next step
## COSMETIC    no, display only   no         yes
## [/codeblock]
enum PropertyClass {
	CAUSAL,
	DERIVED,
	COSMETIC,
}

## The [member audience] axis, which peers a set reaches.
##
## [constant AUDIENCE_PUBLIC] reaches every admitted recipient.
## [constant AUDIENCE_SERVER_ONLY] narrows the set to the server, the input
## stream's reach.
enum Audience {
	AUDIENCE_PUBLIC,
	AUDIENCE_SERVER_ONLY,
}


## One member column of a set, positioned in wire order inside
## [member columns].
##
## The shape half is read from [member shape], the schema's own declaration of
## this column, so a set never carries a second copy of a key or a quantizer
## that could disagree with the table and the database. Everything else here is
## binding: which lane the column rides and what a reconciliation does with it.
class Column extends RefCounted:
	## The address this column has in [member NetwPropertySet.schema], the one
	## address it has anywhere.
	var schema_column: int = -1

	## The schema's declaration of this column, shared rather than copied.
	var shape: SchemaColumn

	## Payload name, stable across peers because both read it off the same
	## declaration.
	var key: StringName:
		get:
			return shape.key if shape else &""

	## Declared [enum SchemaCore.ColumnType], the shape the schema fixed.
	## [constant SchemaCore.ColumnType.VARIANT] is the self-describing tier a
	## [String] or an untyped [code]var[/code] reaches.
	var type: int:
		get:
			return shape.type if shape else SchemaCore.ColumnType.VARIANT

	## Bit-packer for this column, or [code]null[/code] for a self-describing
	## raw [Variant] on the wire. Part of the schema's shape, so two peers that
	## packed it differently disagree on [method NetwPropertySet.wire_hash].
	var quantizer: NetwQuantize:
		get:
			return shape.quantizer if shape else null
		set(value):
			if shape:
				shape.quantizer = value

	## When true the column replicates reliably on change, otherwise it is
	## volatile and freshest-wins.
	var watch: bool = false

	## The delivery lane, the axis form of [member watch]:
	## [constant Lane.VOLATILE] when the column is freshest-wins,
	## [constant Lane.RETAINED] when it replicates reliably on change.
	var lane: Lane = Lane.VOLATILE

	## What the column's value does in the simulation, which decides whether a
	## reconciliation compares it, restores it, or leaves it to display. Set by
	## [method NetwScriptModel.PropertyConfig.causal] and its siblings.
	var property_class: PropertyClass = PropertyClass.CAUSAL

	## How firmly a restored value is pulled toward the authoritative one instead
	## of being written to it, or [code]0.0[/code] to write it outright. Set by
	## [method NetwScriptModel.PropertyConfig.converge].
	var converge_stiffness: float = 0.0

	## The sibling column a recovery advances this value along, from the
	## transition it acknowledged to the present, or empty when the acknowledged
	## value is written as it stands. Set by
	## [method NetwScriptModel.PropertyConfig.carry_along].
	var carry_channel: StringName = &""

	## When true the column is restored only by a teleport-tier recovery, never
	## by an ordinary one. Set by
	## [method NetwScriptModel.PropertyConfig.teleport_only].
	var explicit_teleport_only: bool = false

	## When true the column never triggers a correction on its own, while a
	## correction another column triggers still restores it. Set by
	## [method NetwScriptModel.PropertyConfig.reconcile_only].
	var explicit_reconcile_only: bool = false

	## The column's own divergence threshold, or a negative value to inherit the
	## entity's default. Set by [method NetwScriptModel.PropertyConfig.epsilon].
	var epsilon_override: float = -1.0

	## The column's own teleport-tier distance, or a negative value to inherit
	## the entity's default. Set by
	## [method NetwScriptModel.PropertyConfig.teleport_at], which also enrols the
	## column in the tier measurement.
	var teleport_at_override: float = -1.0


	## Builds a member whose shape is a fresh single-column declaration.
	##
	## The compiled path passes a schema column through [member shape] instead,
	## so this form is for a set assembled by hand with no schema of its own.
	func _init(
			column_key: StringName = &"",
			column_quantizer: NetwQuantize = null,
			column_watch: bool = false,
			column_type: int = SchemaCore.ColumnType.VARIANT,
	) -> void:
		shape = SchemaColumn.create(column_key, column_type, 1)
		shape.quantizer = column_quantizer
		watch = column_watch
		lane = Lane.RETAINED if column_watch else Lane.VOLATILE

## The declaration every member's shape is read from. A set assembled by hand
## builds one as it goes, and a compiled set binds the script's own.
var schema := SchemaRecord.new()

## The sealed [SchemaRecord] of the [constant Lane.VOLATILE] members alone, in
## the same order [member schema] declares them.
##
## A retained member rides its own reliable lane, so the freshest-wins row is
## this subsequence rather than the whole declaration. It is a subsequence and
## not a re-ordering, which is what keeps one column order rather than two.
## [codeblock]
##     NetwPredictCommandFrame.create(set.volatile_schema)
## [/codeblock]
var volatile_schema: SchemaRecord:
	get:
		if _volatile_schema == null:
			_volatile_schema = _project_volatile_schema()
		return _volatile_schema

var _volatile_schema: SchemaRecord = null

## The sealed [SchemaRecord] of the [constant Lane.RETAINED] members alone, in
## the same order [member schema] declares them.
##
## The mirror of [member volatile_schema], and the two are disjoint
## subsequences of one declaration. A set with no retained member has an empty
## record here, which compiles to no plan and offers nothing.
var retained_schema: SchemaRecord:
	get:
		if _retained_schema == null:
			_retained_schema = _project_retained_schema()
		return _retained_schema

var _retained_schema: SchemaRecord = null

## Ordered member columns, the wire order both peers walk positionally.
var columns: Array[Column] = []

## When the set's values leave this peer.
var cadence: Cadence = Cadence.TICK

## How the payload bytes are framed.
var profile: Profile = Profile.PLAIN

## The send condition for the set, the axis form of [member cadence].
var trigger: Trigger = Trigger.TRIGGER_TICK

## The tick framing on the payload, the axis form of [member profile].
var stamp: Stamp = Stamp.STAMP_NONE

## The timeline a received set captures into, always explicit.
var record: Record = Record.RECORD_NONE

## Redundant volatile sample count, [code]0[/code] for none. Only a volatile-lane
## set with a windowed input stream sets this.
var window: int = 0

## Which peers the set reaches.
var audience: Audience = Audience.AUDIENCE_PUBLIC

## Whether the volatile lane rides the masked per-recipient diff that
## [NetwReplicationSend] writes, instead of a shared broadcast row. Illegal combined with [member window], since a redundant sample
## already defeats masking. [method from_property_configs] warns and forces
## this back to [code]false[/code] when both are set.
var masked: bool = false

## Who may author the stream, checked on the receiver against the target's
## script.
var policy: NetwScriptModel.Policy = NetwScriptModel.Policy.AUTHORITY

## The [enum NetwFrameEnvelope.Channel] the set rides, unset until a factory
## derives it from the declaration.
@warning_ignore("int_as_enum_without_cast")
var channel: NetwFrameEnvelope.Channel = -1

## Whether an [constant Cadence.ON_DEMAND] send transmits reliably. A
## [constant Cadence.TICK] set's reliability is chosen per send by its sender,
## so this default governs only the [method Netw.sync_property] door.
var reliable: bool = true

## The flat property set handle, or an invalid RID for a compatibility-only set.
var rid: RID

## True after the flat compiler fixes the wire hash and freezes mutation.
var sealed: bool = false

var _sealed_wire_hash: int = 0


## Types every member column against [param node] and seals the set, the step
## [method from_script] performs for a compiled set and a set assembled from
## [method from_property_configs] alone still owes.
##
## A column's declared type is what a lane plans its row against, and half the
## properties a game replicates are engine properties of the node's native class
## that a [Script]'s own list cannot see, so the node is the only place the shape
## can be read from. An already sealed set is left alone, because its shape is
## what its peers agreed on.
## [codeblock]
##     var set := NetwPropertySet.from_property_configs(configs, record)
##     set.compile_against(node)      # now it carries a row shape
## [/codeblock]
func compile_against(node: Node) -> void:
	if sealed or not is_instance_valid(node):
		return
	_stamp_column_types(self, node.get_script() as Script, node)
	_seal()


## Returns an empty set carrying the default axes for [param record].
static func for_record(record: Record) -> NetwPropertySet:
	var set := NetwPropertySet.new()
	set.record = record
	set.profile = Profile.STAMPED
	set.stamp = (
			Stamp.STAMP_TICK_ACK
			if record == Record.RECORD_STATE
			else Stamp.STAMP_TICK
	)
	set.cadence = Cadence.TICK
	set.channel = NetwFrameEnvelope.Channel.SYNC
	set.policy = (
			NetwScriptModel.Policy.AUTHORITY
			if record == Record.RECORD_STATE
			else NetwScriptModel.Policy.CONTROLLER
	)
	set.trigger = Trigger.TRIGGER_ON_CHANGE
	set.window = 2 if record == Record.RECORD_INPUT else 0
	set.audience = (
			Audience.AUDIENCE_SERVER_ONLY
			if record == Record.RECORD_INPUT
			else Audience.AUDIENCE_PUBLIC
	)
	return set


## Derives the [constant Cadence.ON_DEMAND] set for a [method Netw.sync_property]
## send of [param property]. The single column carries the configured quantizer,
## and the set's reliability and policy come from [param config], defaulting to a
## reliable, authority-only raw send when the property was never configured.
static func from_property_config(
		property: StringName,
		config: NetwScriptModel.SyncConfig,
) -> NetwPropertySet:
	var set := NetwPropertySet.new()
	set.cadence = Cadence.ON_DEMAND
	set.trigger = Trigger.TRIGGER_ON_DEMAND
	set.profile = Profile.PLAIN
	set.channel = NetwFrameEnvelope.Channel.PROPERTY_SYNC
	var quantizer: NetwQuantize = null
	if config:
		set.policy = config.write_policy
		set.reliable = config.transfer_mode == NetwScriptModel.TransferMode.RELIABLE
		if not config.quantizers.is_empty():
			quantizer = config.quantizers[0]
	set.bind(Column.new(property, quantizer, false))
	return set


## Derives a script's state, input, or broadcast set from the
## [NetwScriptModel.PropertyConfig] rows declared through
## [method Netw.configure_property], keyed by [param record]:
## [constant Record.RECORD_STATE] collects the properties marked with
## [method NetwScriptModel.PropertyConfig.state], [constant Record.RECORD_INPUT]
## the properties marked with [method NetwScriptModel.PropertyConfig.input], and
## [constant Record.RECORD_BROADCAST] the properties marked with
## [method NetwScriptModel.PropertyConfig.broadcast]. Returns [code]null[/code]
## when no property carries the mark, so a script without a set of that kind
## derives nothing.
##
## The mark presets a set's axes, so the derivation stamps them from the kind:
## a state set carries [constant Stamp.STAMP_TICK_ACK] framing and
## [constant Audience.AUDIENCE_PUBLIC] reach, an input set carries
## [constant Stamp.STAMP_TICK] framing and [constant Audience.AUDIENCE_SERVER_ONLY]
## reach, a broadcast set carries [constant Stamp.STAMP_TICK] framing and
## [constant Audience.AUDIENCE_PUBLIC] reach outside the rewind boundary, and all
## stamp [constant Profile.STAMPED], [constant Cadence.TICK], and
## [constant Trigger.TRIGGER_ON_CHANGE]. A column routes onto its declared
## [member Column.lane]. The set-level knobs a member wrote through
## ([member NetwScriptModel.PropertyConfig.set_trigger],
## [member NetwScriptModel.PropertyConfig.set_window],
## [member NetwScriptModel.PropertyConfig.set_audience], and an explicit
## [member NetwScriptModel.SyncConfig.write_policy]) reconcile across the marked
## properties, so the last member to name a knob owns it and a genuine
## disagreement between two members warns.
##
## [param node] is what the column types are reflected through. Half the
## properties a game replicates are engine properties of the node's native
## class, which a [Script]'s own property list cannot see, so a set compiled
## without a node types those columns
## [constant SchemaCore.ColumnType.VARIANT].
##
## When [param api] is supplied, the compiler emits the same record through
## [method NetwMultiplayer.property_set_create] and its mutation verbs, seals it,
## and returns the canonical record whose [member rid] names it. Omitting
## [param api] keeps the compatibility object form.
static func from_script(
		script: Script,
		record: Record,
		api: NetwMultiplayer = null,
		node: Node = null,
) -> NetwPropertySet:
	var set := from_property_configs(
		NetwScriptModel.get_property_configs(script),
		record,
	)
	if set == null:
		return null
	_stamp_column_types(set, script, node)
	set._seal()
	if api:
		var rid := api._adopt_property_set(script, record, set, node)
		return api._property_set_record(rid)
	return set


## Derives one set from an already-resolved property config map, the
## script-independent core of [method from_script]. [param configs] maps a
## property [StringName] to its [NetwScriptModel.PropertyConfig], iterated in
## declaration order so the column order is the declaration order, and
## [param record] selects the state, input, or broadcast mark.
static func from_property_configs(
		configs: Dictionary,
		record: Record,
) -> NetwPropertySet:
	var want_input := record == Record.RECORD_INPUT
	var want_broadcast := record == Record.RECORD_BROADCAST
	var set := for_record(record)

	# Input and broadcast are the controlling player's stream, state the server's.
	var policy: NetwScriptModel.Policy = (
			NetwScriptModel.Policy.AUTHORITY if record == Record.RECORD_STATE
			else NetwScriptModel.Policy.CONTROLLER
	)
	var policy_owner := &""
	var trigger := Trigger.TRIGGER_ON_CHANGE
	var trigger_owner := &""
	# An input set is windowed by contract so a lost input tick heals from a
	# redundant sample rather than a retransmit. Two tolerates one lost datagram at
	# per-tick cadence. A member's windowed(n) overrides it. State and broadcast are
	# never windowed: the masked lane heals against the confirmed baseline instead.
	var window := 2 if want_input else 0
	var window_owner := &""
	# An input set is server-bound by contract, a state set narrows only when a
	# member asks for it, and a broadcast set is always public (a server-only
	# volatile stream is an input set wearing a costume).
	var audience := Audience.AUDIENCE_SERVER_ONLY if want_input else Audience.AUDIENCE_PUBLIC
	var masked := false

	for property: StringName in configs:
		var config = configs[property]
		if not (config is NetwScriptModel.PropertyConfig):
			continue
		var marked: bool
		match record:
			Record.RECORD_INPUT:
				marked = config.in_input_set
			Record.RECORD_BROADCAST:
				marked = config.in_broadcast_set
			_:
				marked = config.in_state_set
		if not marked:
			continue
		# The recorded kinds are correctness-critical, so a property marked broadcast
		# and also state or input drops its broadcast mark and rides the recorded
		# set alone. Two sets racing writes on the same observer property is never
		# what anyone meant.
		if want_broadcast and (config.in_state_set or config.in_input_set):
			_warn_broadcast_exclusivity(property)
			continue

		var quantizer: NetwQuantize = (
				config.quantizers[0] if not config.quantizers.is_empty() else null
		)
		var column := Column.new(
			property,
			quantizer,
			config.lane == Lane.RETAINED,
		)
		column.property_class = config.property_class
		column.converge_stiffness = config.converge_stiffness
		column.carry_channel = config.carry_channel
		column.explicit_teleport_only = config.explicit_teleport_only
		column.explicit_reconcile_only = config.explicit_reconcile_only
		column.epsilon_override = config.epsilon_override
		column.teleport_at_override = config.teleport_at_override
		set.bind(column)

		if config._policy_configured:
			if not policy_owner.is_empty() and config.write_policy != policy:
				_warn_set_conflict(record, "policy", policy_owner, property)
			policy = config.write_policy
			policy_owner = property
		if config.set_trigger != NetwScriptModel.PropertyConfig.UNSET:
			if not trigger_owner.is_empty() and config.set_trigger != trigger:
				_warn_set_conflict(record, "trigger", trigger_owner, property)
			trigger = config.set_trigger
			trigger_owner = property
		if config.set_window != NetwScriptModel.PropertyConfig.UNSET:
			if want_broadcast:
				_warn_broadcast_ignored("windowed", property)
			else:
				if not window_owner.is_empty() and config.set_window != window:
					_warn_set_conflict(record, "window", window_owner, property)
				window = config.set_window
				window_owner = property
		if config.set_audience == Audience.AUDIENCE_SERVER_ONLY:
			if want_broadcast:
				_warn_broadcast_ignored("audience", property)
			elif not want_input:
				audience = Audience.AUDIENCE_SERVER_ONLY
		if config.set_masked:
			masked = true

	if set.columns.is_empty():
		return null

	if masked and window > 0:
		Netw.dbg.warn(
			"NetwPropertySet: masked and windowed are mutually exclusive; " +
			"ignoring masked() since a redundant sample already defeats masking",
			[],
			func(m): push_warning(m)
		)
		masked = false

	set.policy = policy
	set.trigger = trigger
	set.window = window
	set.audience = audience
	set.masked = masked
	return set


## Returns the [enum SchemaCore.ColumnType] [param property] compiles to on
## [param script], reflected through [param node] when the script's own
## property list does not carry it.
##
## Half the properties a game replicates are engine properties of the node's
## native class, which a [Script] cannot see, so a lookup without a node types
## those [constant SchemaCore.ColumnType.VARIANT].
static func column_type_for(
		script: Script,
		node: Node,
		property: StringName,
) -> int:
	var variant_type := TYPE_NIL
	if script:
		for entry: Dictionary in script.get_script_property_list():
			if int(entry.get(&"usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE \
					and StringName(entry.get(&"name", &"")) == property:
				variant_type = int(entry.get(&"type", TYPE_NIL))
				break
	if variant_type == TYPE_NIL and is_instance_valid(node):
		variant_type = NetwScriptModel.get_node_property_type(node, property)
	return SchemaCore.type_from_variant(variant_type)


# Types every member column from the node the set was compiled for.
#
# A node rather than the script, because position, velocity, and rotation are
# engine properties of the native class and a script's own property list does
# not carry them. Without a node the script list is all there is, which types
# those columns VARIANT.
static func _stamp_column_types(
		set: NetwPropertySet,
		script: Script,
		node: Node,
) -> void:
	var script_types: Dictionary[StringName, int] = { }
	if script:
		for entry: Dictionary in script.get_script_property_list():
			if int(entry.get(&"usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE:
				script_types[StringName(entry.get(&"name", &""))] = int(
					entry.get(&"type", TYPE_NIL),
				)
	for column: Column in set.columns:
		var variant_type := int(script_types.get(column.key, TYPE_NIL))
		if variant_type == TYPE_NIL and is_instance_valid(node):
			variant_type = NetwScriptModel.get_node_property_type(node, column.key)
		column.shape.type = SchemaCore.type_from_variant(variant_type)


# Warns when two members of a set disagree on a set-level knob, which the
# last-writer-wins reconciliation would otherwise resolve silently.
static func _warn_set_conflict(
		record: Record,
		axis: String,
		first: StringName,
		second: StringName,
) -> void:
	var kind := _record_kind_name(record)
	Netw.dbg.warn(
		"NetwPropertySet: %s set-level %s set by both '%s' and '%s'",
		[kind, axis, first, second],
		func(m): push_warning(m)
	)


# Warns when a broadcast member names a knob a broadcast set forces to its own
# value: a broadcast is always public and never windowed, so the member's ask is
# ignored rather than silently reshaping the stream.
static func _warn_broadcast_ignored(axis: String, property: StringName) -> void:
	Netw.dbg.warn(
		"NetwPropertySet: broadcast set forces %s; ignoring the ask on '%s'",
		[axis, property],
		func(m): push_warning(m)
	)


# Warns when a property carries both the broadcast mark and a recorded mark. The
# recorded kind wins and the broadcast mark drops, so the property never rides
# two racing sets on the receiver.
static func _warn_broadcast_exclusivity(property: StringName) -> void:
	Netw.dbg.warn(
		"NetwPropertySet: '%s' marked broadcast() and state()/input(); " +
		"dropping the broadcast mark in favor of the recorded set",
		[property],
		func(m): push_warning(m)
	)


static func _record_kind_name(record: Record) -> String:
	match record:
		Record.RECORD_INPUT:
			return "input"
		Record.RECORD_BROADCAST:
			return "broadcast"
		_:
			return "state"


## Appends [param column] as the next member in wire order and returns it.
##
## The member is added to [member schema] as well when it carries a shape of its
## own, which is what makes a hand-assembled set carry a real schema instead of
## a loose column list.
func bind(column: Column) -> Column:
	column.schema_column = schema.columns.size()
	schema.columns.append(column.shape)
	columns.append(column)
	reproject_lanes()
	return column


## Drops the cached [member volatile_schema] and [member retained_schema]
## projections so the next read splits the columns as they stand now.
##
## The two projections are subsequences of [member schema] selected by
## [member Column.lane], so anything that moves a column between lanes has to
## call this or the two lanes keep sending each other's columns.
func reproject_lanes() -> void:
	_volatile_schema = null
	_retained_schema = null


# The volatile members' shapes as their own sealed record, shared rather than
# copied so a column typed after the projection is built still types here.
func _project_volatile_schema() -> SchemaRecord:
	var projected := SchemaRecord.new()
	for column in columns:
		if column.lane != Lane.VOLATILE or column.shape == null:
			continue
		projected.columns.append(column.shape)
	SchemaCore.fix(projected)
	return projected


# The retained members' shapes as their own sealed record, shared rather than
# copied so a column typed after the projection is built still types here.
func _project_retained_schema() -> SchemaRecord:
	var projected := SchemaRecord.new()
	for column in columns:
		if column.lane != Lane.RETAINED or column.shape == null:
			continue
		projected.columns.append(column.shape)
	SchemaCore.fix(projected)
	return projected


## Returns the member column at [param schema_column], or [code]null[/code] when
## the schema declares that column but this set does not bind it.
func member(schema_column: int) -> Column:
	for column in columns:
		if column.schema_column == schema_column:
			return column
	return null


## Returns the member keys in wire order.
func keys() -> Array[StringName]:
	var out: Array[StringName] = []
	for column in columns:
		out.append(column.key)
	return out


## Returns the per-column quantizers in wire order, parallel to [method keys],
## [code]null[/code] where a column is self-describing.
func quantizers() -> Array:
	var out: Array = []
	for column in columns:
		out.append(column.quantizer)
	return out


## Returns the 16-bit fingerprint of this binding: each member's shape folded
## with its membership position and its lane.
##
## The shape half is what the schema fixed, so a peer that declared a different
## key, type, stride, or quantizer disagrees here. The binding half is
## membership, order, and lane, so a peer that bound a different subset, in a
## different order, or moved a column between lanes disagrees too. The two lanes
## ride separate channels, which is why a lane change is a wire change rather
## than a local one.
##
## [br][br]The schema's own name is deliberately absent. A schema is named by
## the script that declared it, and a script with no resource path has no name
## two peers can agree on, so folding one would poison a binding over a purely
## local fact.
##
## [br][br]The value rides the [constant NetwFrameEnvelope.Channel.SPAWN]
## frame's set descriptor section, one [code](ordinal, wire hash)[/code] per
## set, which is the same spawn-time validation [NetwSyncCompat] applies to a
## consumed set.
func wire_hash() -> int:
	if sealed:
		return _sealed_wire_hash
	var parts := PackedStringArray()
	for i in columns.size():
		var column := columns[i]
		parts.append(
			"%d:%s:%d:%d:%s:%d" % [
				i,
				column.key,
				column.type,
				column.shape.stride if column.shape else 1,
				SchemaCore.quantizer_tag(column.shape),
				column.lane,
			],
		)
	return "|".join(parts).hash() & 0xFFFF


# Freezes the wire fingerprint after every column and axis is compiled.
func _seal() -> void:
	if sealed:
		return
	SchemaCore.fix(schema)
	_sealed_wire_hash = wire_hash()
	sealed = true
