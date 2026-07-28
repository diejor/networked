## The declaration of one replicated property group, session-independent and
## shared by every instance that declares it.
##
## A set is the ordered, typed field list two peers must agree on to read each
## other's bytes. The order is the wire order, so [method keys] and
## [method quantizers] are parallel arrays a positional codec walks in lockstep,
## and a receiver decodes by position with no per-field tag on the wire.
## [codeblock]
## NetwSyncSet
##  ┠╴ fields     ordered [NetwSyncSet.Field], the wire order
##  ┃    ┠╴ key        StringName   payload name, stable across peers
##  ┃    ┠╴ quantizer  NetwQuantize bit-packer, null = self-describing raw
##  ┃    ┠╴ lane       VOLATILE freshest-wins vs RETAINED reliable-on-change
##  ┃    ┖╴ watch      bool         the lane axis as a flag, kept for derivation
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
## The per-set delivery knobs are [member trigger], [member stamp],
## [member record], [member window], and [member audience]. Three of them are
## the axis form of a coarser flag kept for derivation.
## [codeblock]
## axis        coarse flag  both answer
## trigger     cadence      when a send fires
## stamp       profile      what framing the payload carries
## Field.lane  Field.watch  which delivery lane the field rides
## [/codeblock]
##
## The field order and the codec choice are the compatibility contract. Two
## declarations that disagree on either read each other's bytes wrong, so a set
## is the unit a schema check validates.
## [br][br][b]Granularity[/b][br]
## The knobs [member trigger], [member stamp], [member window], [member audience],
## and [member masked] each bind the whole set, never one field, because the set
## is one datagram frame that fires, reaches a recipient, and carries redundancy
## as a unit. A field that needs different delivery leaves the frame rather than
## reshaping it, so finer control means another set, not a per-field flag. A set
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
class_name NetwSyncSet
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

## The [member Field.lane] axis, a field's delivery lane.
##
## [constant VOLATILE] fields ride the [constant NetwFrameEnvelope.Channel.SYNC]
## frame freshest-wins, [constant RETAINED] fields ride the reliable
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA] lane only when they change.
enum Lane {
	VOLATILE,
	RETAINED,
}

## The [member Field.property_class] axis, what a field's value does in the
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

## One replicated field, positioned in wire order inside [member fields].
class Field extends RefCounted:
	## Payload name, stable across peers because both derive it from the same
	## declaration.
	var key: StringName

	## Bit-packer for this field, or [code]null[/code] for a self-describing
	## raw [Variant] on the wire.
	var quantizer: NetwQuantize

	## When true the field replicates reliably on change, otherwise it is
	## volatile and freshest-wins.
	var watch: bool = false

	## The delivery lane, the axis form of [member watch]:
	## [constant Lane.VOLATILE] when the field is freshest-wins,
	## [constant Lane.RETAINED] when it replicates reliably on change.
	var lane: Lane = Lane.VOLATILE

	## What the field's value does in the simulation, which decides whether a
	## reconciliation compares it, restores it, or leaves it to display. Set by
	## [method NetwScriptModel.PropertyConfig.causal] and its siblings.
	var property_class: PropertyClass = PropertyClass.CAUSAL

	## How firmly a restored value is pulled toward the authoritative one instead
	## of being written to it, or [code]0.0[/code] to write it outright. Set by
	## [method NetwScriptModel.PropertyConfig.converge].
	var converge_stiffness: float = 0.0

	## The sibling field a recovery advances this value along, from the transition
	## it acknowledged to the present, or empty when the acknowledged value is
	## written as it stands. Set by
	## [method NetwScriptModel.PropertyConfig.carry_along].
	var carry_channel: StringName = &""


	## When true the field is restored only by a teleport-tier recovery, never by
	## an ordinary one. Set by
	## [method NetwScriptModel.PropertyConfig.teleport_only].
	var explicit_teleport_only: bool = false

	## When true the field never triggers a correction on its own, while a
	## correction another field triggers still restores it. Set by
	## [method NetwScriptModel.PropertyConfig.reconcile_only].
	var explicit_reconcile_only: bool = false

	## The field's own divergence threshold, or a negative value to inherit the
	## entity's default. Set by [method NetwScriptModel.PropertyConfig.epsilon].
	var epsilon_override: float = -1.0


	func _init(
			field_key: StringName,
			field_quantizer: NetwQuantize = null,
			field_watch: bool = false,
	) -> void:
		key = field_key
		quantizer = field_quantizer
		watch = field_watch
		lane = Lane.RETAINED if field_watch else Lane.VOLATILE


## Ordered fields, the wire order both peers walk positionally.
var fields: Array[Field] = []

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

## Whether the volatile lane rides the masked per-recipient diff
## ([constant NetwFrameEnvelope.SYNC_FLAG_MASKED]) instead of a shared broadcast
## row. Illegal combined with [member window], since a redundant sample
## already defeats masking; [method from_property_configs] warns and forces
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


## Derives the [constant Cadence.ON_DEMAND] set for a [method Netw.sync_property]
## send of [param property]. The single field carries the configured quantizer,
## and the set's reliability and policy come from [param config], defaulting to a
## reliable, authority-only raw send when the property was never configured.
static func from_property_config(
		property: StringName,
		config: NetwScriptModel.SyncConfig,
) -> NetwSyncSet:
	var set := NetwSyncSet.new()
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
	set.fields.append(Field.new(property, quantizer, false))
	return set


## Derives a script's state, input, or broadcast set from the
## [NetwScriptModel.PropertyConfig] rows declared through
## [method Netw.configure_property], keyed by [param record]:
## [constant Record.RECORD_STATE] collects the fields marked with
## [method NetwScriptModel.PropertyConfig.state], [constant Record.RECORD_INPUT]
## the fields marked with [method NetwScriptModel.PropertyConfig.input], and
## [constant Record.RECORD_BROADCAST] the fields marked with
## [method NetwScriptModel.PropertyConfig.broadcast]. Returns [code]null[/code]
## when no field carries the mark, so a script without a set of that kind derives
## nothing.
##
## The mark presets a set's axes, so the derivation stamps them from the kind:
## a state set carries [constant Stamp.STAMP_TICK_ACK] framing and
## [constant Audience.AUDIENCE_PUBLIC] reach, an input set carries
## [constant Stamp.STAMP_TICK] framing and [constant Audience.AUDIENCE_SERVER_ONLY]
## reach, a broadcast set carries [constant Stamp.STAMP_TICK] framing and
## [constant Audience.AUDIENCE_PUBLIC] reach outside the rewind boundary, and all
## stamp [constant Profile.STAMPED], [constant Cadence.TICK], and
## [constant Trigger.TRIGGER_ON_CHANGE]. A field routes onto its declared
## [member Field.lane]. The set-level knobs a member wrote through
## ([member NetwScriptModel.PropertyConfig.set_trigger],
## [member NetwScriptModel.PropertyConfig.set_window],
## [member NetwScriptModel.PropertyConfig.set_audience], and an explicit
## [member NetwScriptModel.SyncConfig.write_policy]) reconcile across the marked
## fields, so the last member to name a knob owns it and a genuine disagreement
## between two members warns.
static func from_script(script: Script, record: Record) -> NetwSyncSet:
	return from_property_configs(
			NetwScriptModel.get_property_configs(script),
			record,
	)


## Derives one set from an already-resolved property config map, the
## script-independent core of [method from_script]. [param configs] maps a
## property [StringName] to its [NetwScriptModel.PropertyConfig], iterated in
## declaration order so the field order is the declaration order, and
## [param record] selects the state, input, or broadcast mark.
static func from_property_configs(
		configs: Dictionary,
		record: Record,
) -> NetwSyncSet:
	var want_input := record == Record.RECORD_INPUT
	var want_broadcast := record == Record.RECORD_BROADCAST
	var set := NetwSyncSet.new()
	set.record = record
	set.profile = Profile.STAMPED
	# State frames the reconciliation ack, input and broadcast frame the bare tick:
	# a broadcast never reconciles, so an ack slot would be a lie.
	set.stamp = Stamp.STAMP_TICK_ACK if record == Record.RECORD_STATE else Stamp.STAMP_TICK
	set.cadence = Cadence.TICK
	set.channel = NetwFrameEnvelope.Channel.SYNC

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
		# The recorded kinds are correctness-critical, so a field marked broadcast
		# and also state or input drops its broadcast mark and rides the recorded
		# set alone. Two sets racing writes on the same observer property is never
		# what anyone meant.
		if want_broadcast and (config.in_state_set or config.in_input_set):
			_warn_broadcast_exclusivity(property)
			continue

		var quantizer: NetwQuantize = (
				config.quantizers[0] if not config.quantizers.is_empty() else null
		)
		var field := Field.new(property, quantizer, config.lane == Lane.RETAINED)
		field.property_class = config.property_class
		field.converge_stiffness = config.converge_stiffness
		field.carry_channel = config.carry_channel
		field.explicit_teleport_only = config.explicit_teleport_only
		field.explicit_reconcile_only = config.explicit_reconcile_only
		field.epsilon_override = config.epsilon_override
		set.fields.append(field)

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

	if set.fields.is_empty():
		return null

	if masked and window > 0:
		Netw.dbg.warn(
			"NetwSyncSet: masked and windowed are mutually exclusive; " +
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
		"NetwSyncSet: %s set-level %s set by both '%s' and '%s'",
		[kind, axis, first, second],
		func(m): push_warning(m)
	)


# Warns when a broadcast member names a knob a broadcast set forces to its own
# value: a broadcast is always public and never windowed, so the member's ask is
# ignored rather than silently reshaping the stream.
static func _warn_broadcast_ignored(axis: String, property: StringName) -> void:
	Netw.dbg.warn(
		"NetwSyncSet: broadcast set forces %s; ignoring the ask on '%s'",
		[axis, property],
		func(m): push_warning(m)
	)


# Warns when a property carries both the broadcast mark and a recorded mark. The
# recorded kind wins and the broadcast mark drops, so the field never rides two
# racing sets on the receiver.
static func _warn_broadcast_exclusivity(property: StringName) -> void:
	Netw.dbg.warn(
		"NetwSyncSet: '%s' marked broadcast() and state()/input(); " +
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


## Returns the field keys in wire order.
func keys() -> Array[StringName]:
	var out: Array[StringName] = []
	for field in fields:
		out.append(field.key)
	return out


## Returns the per-field quantizers in wire order, parallel to [method keys],
## [code]null[/code] where a field is self-describing.
func quantizers() -> Array:
	var out: Array = []
	for field in fields:
		out.append(field.quantizer)
	return out


## Returns the 16-bit schema fingerprint of this set: the
## [constant Lane.VOLATILE] field keys in wire order, then the
## [constant Lane.RETAINED] keys, each tagged by lane so two peers whose
## declarations disagree on membership, order, or lane poison the binding at
## spawn time instead of misreading every later frame. The two lanes are kept
## distinct because they ride separate channels, so a field moving between them
## is a schema change. The hash rides the
## [constant NetwFrameEnvelope.Channel.SPAWN] frame's sync-set descriptor
## section, one [code](ordinal, schema hash)[/code] per set, the same
## spawn-time validation [NetwSyncCompat] applies to a consumed set.
func schema_hash() -> int:
	var parts: Array[String] = []
	for field in fields:
		if field.lane == Lane.VOLATILE:
			parts.append("s" + String(field.key))
	for field in fields:
		if field.lane == Lane.RETAINED:
			parts.append("w" + String(field.key))
	return "|".join(parts).hash() & 0xFFFF
