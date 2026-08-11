## Kernel-altitude rig: one prediction engine, no peers, no tree, no clock.
##
## The engine's laws each built their own engine -- five divergent
## [code]_engine()[/code] bodies, eleven [code]Engine_.new()[/code] pairs, one
## journal-forgery idiom per suite, and twelve hand-spelled calls into the four
## kernels. A signature restated in twelve places is not a signature the mass
## reads, it is one the mass copies, and the reshape that gives the kernels their
## crossing records would have been billed once per copy.
##
## This rig spells each of them exactly once. It hands back the raw engine and
## never forbids poking at it: a kernel-altitude law is representation-level by
## design, and a rig that hid the members would only move the reach somewhere
## less visible. What it does own is everything a law states the same way twice --
## construction, the declaration overrides, journal and episode forgery, the
## binding, signal capture, and the kernel argument lists.
##
## The scenario rig above it ([PredictionScenario]) owns two peers and a clock;
## this one owns one engine and neither. They are separate altitudes and stay
## separate rigs.
##
## [codeblock]
## var engine := EngineRig.engine()
## EngineRig.wire(engine, { &"pose": [&"position"], &"angles": [&"heading"] })
## EngineRig.append_row(engine, 0, { &"matched": false })
## var plan := EngineRig.recover({ &"payload": { &"pos": 3.0 } })
## [/codeblock]
class_name EngineRig
extends RefCounted

const PredictionCore := preload("res://addons/networked/replication/prediction_core.gd")

const Engine_ := PredictionCore._PredictionEngine
const Domain := NetwPredictJournal.Domain
const Attribution := NetwPredictJournal.Attribution
const _NATIVE_CORE_META := &"engine_rig_native_core"

# Wiring key -> the field of [NetwPredict.Wiring] it writes. Named here rather
# than in a chain of ifs so the list a caller may pass and the list the record
# holds are the same list, and a field the reshape renames breaks in one place.
# Phase B moved these from eleven engine members onto one record, and this map is
# all it cost the laws.
const _SET_WIRING := {
	&"withheld": &"withheld",
	&"pose": &"pose_fields",
	&"angles": &"angle_fields",
	&"causal": &"causal_fields",
	&"excludes": &"trigger_excludes",
	&"vote_excludes": &"vote_excludes",
}
const _MAP_WIRING := {
	&"epsilons": &"epsilon_overrides",
	&"teleports": &"teleport_thresholds",
	&"projection": &"projection",
	&"families": &"state_family_of",
	&"converge": &"converge_rules",
	&"carry": &"carry_rules",
}

#region Engines

## The bare pair: an engine holding a fresh handle, wired to nothing. Every
## [code]_engine()[/code] body in the mass started here.
static func engine() -> Engine_:
	var made := Engine_.new()
	var core := LagCompCore.new()
	var entity := NetwEntity.new()
	made._handle = NetwPredictionHandle.new()
	made._entity = entity
	made._iface_ref = weakref(core)
	made._kernel = PredictionImplementations.under_test()
	_open_native_slot(core, entity)
	made.set_meta(_NATIVE_CORE_META, core)
	return made


## The bare pair plus an entity, for the reports that key themselves to the
## config they judge and read the entity to do it.
static func entity_engine(suite: NetwTestSuite) -> Engine_:
	var made := engine()
	var root: Node2D = suite.auto_free(Node2D.new())
	suite.add_child(root)
	var core := _native_core(made)
	core._prediction_pool.close(core._prediction_slots[made._entity])
	core._prediction_slots.erase(made._entity)
	made._entity = NetwEntity.ensure(root)
	_open_native_slot(core, made._entity)
	return made


## An engine wired from a declaration, which is how the reachability report and
## the projection builder are asked what a game's own [NetwScriptModel] verbs
## reach. [param configs] is field -> [NetwScriptModel.PropertyConfig]; the set
## it builds stays readable at [code]engine._state_binding.set[/code].
static func declared(
		suite: NetwTestSuite,
		configs: Dictionary,
		record: NetwPropertySet.Record = NetwPropertySet.Record.RECORD_STATE,
) -> Engine_:
	var made := engine()
	bind_set(suite, made, NetwPropertySet.from_property_configs(configs, record))
	return made


## Binds [param fields] as plain state, for the laws that need a binding to exist
## rather than a declaration to mean something. [param quantize] is a factory
## rather than an instance because a quantizer is per field, and passing an empty
## Callable declares fields with none -- which is a different claim about what the
## meter can read, not a shorthand for the default.
static func bind_state(
		suite: NetwTestSuite,
		target: Engine_,
		fields: Array = [&"position"],
		quantize: Callable = NetwQuantizeFixed.new,
) -> Engine_:
	var set := NetwPropertySet.new()
	for field: StringName in fields:
		set.bind(
			NetwPropertySet.Column.new(
				field,
				quantize.call() if quantize.is_valid() else null,
			),
		)
	return bind_set(suite, target, set)


## Binds [param node]'s own declarations -- the set its script and its instance
## overlay actually declare -- and builds the wiring from them. This is the whole
## declaration path a game walks, so a law standing on it reads what the verbs
## reach rather than what a rig decided they should reach.
static func bind_node(
		target: Engine_,
		node: Node,
		record: NetwPropertySet.Record = NetwPropertySet.Record.RECORD_STATE,
) -> Engine_:
	target._state_binding = NetwPropertySetBinding.new(
		NetwPropertySet.from_property_configs(
			NetwScriptModel.get_node_property_configs(node),
			record,
		),
		node,
	)
	target._build_restore_projection(target._state_binding)
	return target


## Binds an already-built [param set] to a node this suite will free.
static func bind_set(
		suite: NetwTestSuite,
		target: Engine_,
		set: NetwPropertySet,
) -> Engine_:
	var node: Node2D = suite.auto_free(Node2D.new())
	target._state_binding = NetwPropertySetBinding.new(set, node)
	return target

#endregion

#region Declaration overrides

## Writes the wiring members a declaration would have produced, without standing
## a declaration up. Set-shaped keys ([code]withheld[/code], [code]pose[/code],
## [code]angles[/code], [code]causal[/code], [code]excludes[/code],
## [code]vote_excludes[/code]) take an Array
## of field names or a Dictionary already in member shape; map-shaped keys
## ([code]epsilons[/code], [code]teleports[/code], [code]projection[/code],
## [code]families[/code], [code]converge[/code], [code]carry[/code]) take the
## Dictionary itself.
##
## The record's fields are filled key by key rather than assigned, so each keeps
## the element types it declares and a caller cannot swap a typed field for an
## untyped literal.
static func wire(target: Engine_, overrides: Dictionary) -> Engine_:
	for key: StringName in overrides:
		if _SET_WIRING.has(key):
			var member: Dictionary = target._wiring.get(_SET_WIRING[key])
			member.clear()
			var value: Variant = overrides[key]
			if value is Array:
				for field: StringName in value:
					member[field] = true
			else:
				for field: StringName in (value as Dictionary):
					member[field] = bool((value as Dictionary)[field])
		elif _MAP_WIRING.has(key):
			var member: Dictionary = target._wiring.get(_MAP_WIRING[key])
			member.clear()
			for field: StringName in (overrides[key] as Dictionary):
				member[field] = overrides[key][field]
		else:
			_unknown(key, "wire", _SET_WIRING.keys() + _MAP_WIRING.keys())
	_hold_vote_exclude_invariant(target._wiring)
	return target


# trigger_excludes is a SUBSET of vote_excludes by construction: reconcile_only()
# writes both at wire time, and no declaration can produce a field that is
# reconcile-only and still votes. The rig holds that invariant rather than making
# every law restate it, so a law naming `excludes` keeps meaning what that mark
# means. `vote_excludes` alone is how a law spells the class half, which comes
# off the property class and has no mark of its own.
static func _hold_vote_exclude_invariant(wiring: NetwPredict.Wiring) -> void:
	for field: StringName in wiring.trigger_excludes:
		wiring.vote_excludes[field] = true


## The same declaration the engine compiles its wiring from, stated as the
## record the native pool takes.
##
## Read from [param target]'s own state binding through the same marks
## [code]_build_restore_projection[/code] reads, so a law comparing the two
## compilations is comparing compilers rather than comparing two readings of a
## declaration.
static func native_declaration(target: Engine_) -> NetwPredictDeclaration:
	var declaration := NetwPredictDeclaration.new()
	var binding: NetwPropertySetBinding = target._state_binding
	if binding == null or binding.set == null:
		return declaration
	var node := binding.node()
	for field in binding.set.columns:
		var spec: Variant = null
		if is_instance_valid(node):
			spec = NetwScriptModel.get_node_property_interpolator(node, field.key)
		declaration.append_field(
			field.key,
			field.property_class,
			binding.carry_channel_of(field.key),
			binding.converge_stiffness_of(field.key),
			binding.teleport_only_of(field.key),
			binding.reconcile_only_of(field.key),
			binding.epsilon_override_of(field.key),
			binding.teleport_at_of(field.key),
			spec != null and spec.mode == NetwInterpolate.MODE_ANGLE,
		)
	return declaration


## The wiring a declaration produced, read back under the same keys [method wire]
## writes it with.
##
## The read side matters more than the write side. Phase B folds these members
## into one Wiring record, so a law that names [code]_pose_fields[/code] has
## written down a representation the plan has already decided to change; naming
## it here instead means the reshape bills this file and the laws keep asserting
## what the declaration reached. [TestAssertionCensus] pins that no law names one
## directly.
static func wiring(target: Engine_) -> Dictionary:
	var out: Dictionary = { }
	for key: StringName in _SET_WIRING:
		out[key] = target._wiring.get(_SET_WIRING[key])
	for key: StringName in _MAP_WIRING:
		out[key] = target._wiring.get(_MAP_WIRING[key])
	return out

#endregion

#region Journal and episode forgery

## Appends one journal row for [param transition]. Every mark is opt-in: a rig
## that marked a row's domain or ack by default would decide, on behalf of laws
## that never asked, which branch of the compare they run under.
##
## Keys: [code]drive[/code], [code]c_hash[/code], [code]pre_fp[/code],
## [code]pre_families[/code], [code]provenance[/code], [code]post_fp[/code]
## (default [param transition] + 1, which is what every suite passed before this
## column had a name here), [code]post_families[/code], [code]matched[/code],
## [code]attribution[/code], [code]domain[/code], [code]solve[/code] (an Array of
## the [method NetwPredictJournal.mark_solve] arguments after the transition),
## and [code]open_only[/code] to skip the close.
static func append_row(
		target: Engine_,
		transition: int,
		opts: Dictionary = { },
) -> void:
	_check(
		opts,
		"append_row",
		[
			&"drive",
			&"label",
			&"c_hash",
			&"pre_fp",
			&"pre_families",
			&"provenance",
			&"post_fp",
			&"post_families",
			&"matched",
			&"witness_matched",
			&"attribution",
			&"domain",
			&"solve",
			&"open_only",
		],
	)
	var pool := _native_core(target)._prediction_pool
	var slot := _native_slot(target)
	var pre_families: PackedInt32Array = opts.get(
		&"pre_families",
		PackedInt32Array([0, 0, 0]),
	)
	if pre_families.size() < 3:
		pre_families = PackedInt32Array([0, 0, 0])
	pool.record_input(slot, transition, int(opts.get(&"c_hash", 1)))
	pool.replay_drive(
		slot,
		{ },
		transition,
		int(opts.get(&"label", transition)),
		int(opts.get(&"drive", NetwPredict.DriveKind.FRESH)),
		transition,
		transition,
		1.0 / 60.0,
		1,
		int(opts.get(&"pre_fp", 0)),
		pre_families[0],
		pre_families[1],
		pre_families[2],
	)
	if opts.has(&"solve"):
		var solve: Array = opts[&"solve"]
		pool.mark_solve(
			slot,
			transition,
			solve[0],
			solve[1],
			solve[2],
			solve[4] if solve.size() > 4 else 0,
		)
		target._record_witness_detail(transition, solve[3] as Dictionary)
	if not bool(opts.get(&"open_only", false)):
		var post_families: PackedInt32Array = opts.get(
			&"post_families",
			PackedInt32Array([0, 0, 0]),
		)
		if post_families.size() < 3:
			post_families = PackedInt32Array([0, 0, 0])
		pool.close_drive(
			slot,
			transition,
			int(opts.get(&"post_fp", transition + 1)),
			post_families[0],
			post_families[1],
			post_families[2],
		)
	if opts.has(&"matched"):
		pool.acknowledge(slot, transition, bool(opts[&"matched"]))
	if opts.has(&"witness_matched"):
		pool.mark_witness_match(
			slot,
			transition,
			bool(opts[&"witness_matched"]),
		)
	if opts.has(&"attribution"):
		pool.mark_attribution(slot, transition, opts[&"attribution"])
	if opts.has(&"domain"):
		pool.mark_domain(slot, transition, opts[&"domain"])


static func _native_core(target: Engine_) -> LagCompCore:
	return target.get_meta(_NATIVE_CORE_META) as LagCompCore


static func _native_slot(target: Engine_) -> int:
	return _native_core(target)._prediction_slots[target._entity]


static func _open_native_slot(core: LagCompCore, entity: NetwEntity) -> void:
	var slot := core._prediction_pool.open(null)
	core._prediction_slots[entity] = slot
	core._prediction_pool.configure(
		slot,
		NetwPredict.Schedule.TICK,
		NetwPredict.Role.PREDICT,
		NetwPredict.CorrectionMode.SNAP,
		NetwPredict.RestoreMode.EXACT,
	)


## Opens one episode at a forged transition.
##
## Keys: [code]transition[/code] (default 0), [code]attribution[/code] (default
## PRE_STATE), and [code]compare[/code] (an Array of the
## [method _record_episode_comparison] arguments after the transition, absent
## means record none).
static func open_episode(target: Engine_, opts: Dictionary = { }) -> void:
	_check(
		opts,
		"open_episode",
		[
			&"transition",
			&"attribution",
			&"compare",
		],
	)
	var transition := int(opts.get(&"transition", 0))
	target._open_episode(transition, opts.get(&"attribution", Attribution.PRE_STATE))
	if opts.has(&"compare"):
		var compare: Array = opts[&"compare"]
		target._record_episode_comparison(transition, compare[0], compare[1])


## The seven-line preamble the quarantine laws each spelled: a bound, opened
## episode already fallen back, with its stream reconstructed and a target run to
## serve. Named because seven laws spell it identically; the two-site probation
## preamble deliberately is not.
static func in_quarantine(
		suite: NetwTestSuite,
		target_run: int,
		opts: Dictionary = { },
) -> Engine_:
	_check(opts, "in_quarantine", [&"attribution", &"matched"])
	var made := engine()
	bind_state(suite, made)
	append_row(
		made,
		0,
		{
			&"matched": bool(opts.get(&"matched", false)),
			&"attribution": opts.get(&"attribution", Attribution.PRE_STATE),
		},
	)
	open_episode(
		made,
		{
			&"attribution": opts.get(&"attribution", Attribution.PRE_STATE),
			&"compare": [4, false],
		},
	)
	made._episode[&"state"] = NetwPredict.EpisodeState.FALLBACK
	made._fallback_latched = true
	made._quarantine_stream_reconstructed = true
	made._quarantine_target = target_run
	return made

#endregion

#region Signal capture

## Captures every emission of [param sig] as an Array of its arguments. The array
## it returns fills as the signal fires, so a law reads it after the act.
static func capture(sig: Signal) -> Array:
	var seen: Array = []
	match _arity(sig):
		0:
			sig.connect(func() -> void: seen.append([]))
		1:
			sig.connect(func(a: Variant) -> void: seen.append([a]))
		2:
			sig.connect(func(a: Variant, b: Variant) -> void: seen.append([a, b]))
		3:
			sig.connect(
				func(a: Variant, b: Variant, c: Variant) -> void:
					seen.append([a, b, c]),
			)
		4:
			sig.connect(
				func(a: Variant, b: Variant, c: Variant, d: Variant) -> void:
					seen.append([a, b, c, d]),
			)
		var other:
			push_error(
				"EngineRig.capture: %s carries %d arguments, which is more "
				% [sig.get_name(), other] + "than any capture shape here covers",
			)
	return seen


## The single-argument case, captured as the payload itself: the report signals
## every episode law reads.
static func capture_one(sig: Signal) -> Array:
	if _arity(sig) != 1:
		push_error(
			"EngineRig.capture_one: %s does not carry exactly one argument; "
			% sig.get_name() + "use capture() and read the argument array",
		)
		return []
	var seen: Array = []
	sig.connect(func(payload: Variant) -> void: seen.append(payload))
	return seen

#endregion

#region The kernels
#
# One spelling each. Every default here is the case the laws that are NOT about
# it want bound: a recovery that is neither suppressed nor past the teleport
# tier, with one enrolled pose field measured at zero, and a compare with no
# per-field declarations. A law about a tier, a policy or an override passes its
# own value and says so by naming it.
#
# Each takes the implementation it asks, defaulting to the one this run
# certifies. That default is what parameterizes the suite: a law written against
# no implementation in particular is a law the candidate must pass, and no law
# has to opt in. See [PredictionImplementations].

# The implementation a wrapper decides through when a law named none.
static func _impl(given: NetwMultiplayer) -> NetwMultiplayer:
	return given if given else PredictionImplementations.under_test()


const RECOVER_DEFAULTS := {
	&"payload": { },
	&"policy": NetwPredict.RecoveryPolicy.REBASE_RECOVER,
	&"correction": NetwPredict.CorrectionMode.SNAP,
	&"snap_restore": NetwPredict.RestoreMode.EXACT,
	&"projection": { },
	&"withheld": { },
	&"converge_rules": { },
	&"current": { },
	&"angles": { },
	&"pose_errors": { &"pos": 0.0 },
	&"teleport_thresholds": { },
	&"teleport_threshold": 10.0,
	&"pose_unmeasured": false,
	&"suppressed": false,
	&"ack_age_ticks": 0,
	&"max_restore_ticks": 8,
	&"tick_delta": 0.1,
	&"domain": Domain.IN_DOMAIN,
	&"attribution": Attribution.UNKNOWN,
	&"contact_window": false,
}

const EVALUATE_DEFAULTS := {
	&"domain": Domain.OUT_OF_DOMAIN,
	&"verdict": NetwPredict.ExactVerdict.UNJUDGED,
	&"predicted": { },
	&"payload": { },
	&"epsilon": 0.1,
	&"overrides": { },
	&"excludes": { },
	&"vote_excludes": { },
	&"angles": { },
	&"field_sink": { },
}

const PREDICT_DEFAULTS := {
	&"latest": 7,
	&"driven": 4,
	&"frame": 99,
}

const CONSUME_DEFAULTS := {
	&"depth": 0,
	&"buffer": 0,
	&"warmed": true,
}

const GUARD_DEFAULTS := {
	&"projection": { },
	&"field_divergence": { },
	&"epsilon": 0.01,
	&"overrides": { },
	&"ack_age_ticks": 10,
	&"max_restore_ticks": 10,
	&"tick_delta": 0.1,
}


## [method NetwMultiplayer._predict_recover], spelled once.
##
## The keyword surface is unchanged by Phase B's reshape: the laws still name
## twenty facts and this is where they are packed into the two records the kernel
## now takes. That was the whole argument for spelling the signature once.
static func recover(
		overrides: Dictionary = { },
		implementation: NetwMultiplayer = null,
) -> Dictionary:
	var a := _args(RECOVER_DEFAULTS, overrides, "recover")
	return _impl(implementation)._predict_recover(
		a[&"payload"],
		a[&"policy"],
		a[&"correction"],
		a[&"snap_restore"],
		a[&"projection"],
		a[&"current"],
		a[&"pose_errors"],
		_wiring_of(a),
		NetwPredict.Verdict.new().fill(
			a[&"domain"],
			a[&"attribution"],
			a[&"contact_window"],
			a[&"suppressed"],
			a[&"pose_unmeasured"],
			a[&"ack_age_ticks"],
		),
		a[&"tick_delta"],
	)


# The wiring the kernels read, assembled from whichever of its facts this call
# named. One builder for all three, because one record now carries what three
# argument lists used to spell separately.
static func _wiring_of(a: Dictionary) -> NetwPredict.Wiring:
	var wiring := NetwPredict.Wiring.new()
	if a.has(&"withheld"):
		wiring.withheld.assign(a[&"withheld"])
	if a.has(&"converge_rules"):
		wiring.converge_rules.assign(a[&"converge_rules"])
	if a.has(&"angles"):
		wiring.angle_fields.assign(a[&"angles"])
	if a.has(&"teleport_thresholds"):
		wiring.teleport_thresholds.assign(a[&"teleport_thresholds"])
	if a.has(&"overrides"):
		wiring.epsilon_overrides.assign(a[&"overrides"])
	if a.has(&"excludes"):
		wiring.trigger_excludes.assign(a[&"excludes"])
	if a.has(&"vote_excludes"):
		wiring.vote_excludes.assign(a[&"vote_excludes"])
	_hold_vote_exclude_invariant(wiring)
	wiring.epsilon = float(a.get(&"epsilon", 0.0))
	wiring.teleport_threshold = float(a.get(&"teleport_threshold", 0.0))
	wiring.max_restore_ticks = int(a.get(&"max_restore_ticks", 0))
	return wiring


## [method NetwMultiplayer._predict_evaluate], spelled once.
## The field sink is an
## out-parameter, so a law that reads it passes its own.
static func evaluate(
		overrides: Dictionary = { },
		implementation: NetwMultiplayer = null,
) -> Dictionary:
	var a := _args(EVALUATE_DEFAULTS, overrides, "evaluate")
	return _impl(implementation)._predict_evaluate(
		a[&"domain"],
		a[&"verdict"],
		a[&"predicted"],
		a[&"payload"],
		_wiring_of(a),
		a[&"field_sink"],
	)


## [method NetwMultiplayer._predict_drive], spelled once.
##
## [param overrides] names [code]latest[/code], [code]driven[/code] and
## [code]frame[/code]. The defaults are a frame whose input is newer than the
## last one driven, which is the fresh drive every law that is not about
## repetition wants bound.
static func predict(
		overrides: Dictionary = { },
		implementation: NetwMultiplayer = null,
) -> Dictionary:
	var a := _args(PREDICT_DEFAULTS, overrides, "predict")
	return _impl(implementation)._predict_drive(
		a[&"latest"],
		a[&"driven"],
		a[&"frame"],
	)


## [method NetwMultiplayer._predict_consume], spelled once.
##
## [param overrides] names [code]depth[/code], [code]buffer[/code] and
## [code]warmed[/code]. The defaults are a warm queue holding nothing, which
## starves: a law about replaying or holding names the depth that makes it so.
static func consume_plan(
		overrides: Dictionary = { },
		implementation: NetwMultiplayer = null,
) -> Dictionary:
	var a := _args(CONSUME_DEFAULTS, overrides, "consume_plan")
	return _impl(implementation)._predict_consume(
		a[&"depth"],
		a[&"buffer"],
		a[&"warmed"],
	)


## [method _PredictionEngine.guard_projection], spelled once.
static func guard_projection(overrides: Dictionary = { }) -> Dictionary:
	var a := _args(GUARD_DEFAULTS, overrides, "guard_projection")
	return PredictionCore._PredictionEngine.guard_projection(
		a[&"projection"],
		a[&"field_divergence"],
		_wiring_of(a),
		a[&"ack_age_ticks"],
		a[&"tick_delta"],
	)

#endregion

#region Argument checking
#
# A misspelled key silently testing the default is the one failure mode a
# keyword-argument rig introduces that positional arguments do not have, and it
# fails as a passing law. So every entry point names its own keys and says which
# it accepts.

static func _args(
		defaults: Dictionary,
		overrides: Dictionary,
		verb: String,
) -> Dictionary:
	_check(overrides, verb, defaults.keys())
	var merged := defaults.duplicate(true)
	for key: StringName in overrides:
		merged[key] = overrides[key]
	return merged


static func _check(opts: Dictionary, verb: String, allowed: Array) -> void:
	for key: StringName in opts:
		if not allowed.has(key):
			_unknown(key, verb, allowed)


static func _unknown(key: StringName, verb: String, allowed: Array) -> void:
	var names := PackedStringArray()
	for allowed_key: StringName in allowed:
		names.append(String(allowed_key))
	names.sort()
	push_error(
		"EngineRig.%s: no argument named '%s'. It takes: %s."
		% [verb, key, ", ".join(names)],
	)


static func _arity(sig: Signal) -> int:
	var owner := sig.get_object()
	if owner == null:
		return -1
	for entry: Dictionary in owner.get_signal_list():
		if StringName(entry["name"]) == sig.get_name():
			return (entry["args"] as Array).size()
	return -1

#endregion
