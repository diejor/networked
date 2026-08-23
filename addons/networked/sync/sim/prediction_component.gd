@tool
## Scene-first policy for one entity's prediction boundary and reconciliation.
## [codeblock]
## PlayerRoot
## `-- PredictionComponent
## [/codeblock]
## The node publishes its entity-level policy through
## [member NetwEntity.prediction] and registers one engine record with
## [method NetwMultiplayer.predict_declare]. The predicted
## state is compared with the authority row named by
## [member NetwPropertySetBinding.reconcile_ack], never with the newest received
## tick. This keeps the comparison aligned without requiring equal peer clocks
## or deterministic physics.
##
## [br][br]A per-field tolerance, restore restriction, trigger exclusion, or
## convergence rate belongs to its property mark through
## ([method NetwScriptModel.PropertyConfig.epsilon],
## [method NetwScriptModel.PropertyConfig.teleport_only],
## [method NetwScriptModel.PropertyConfig.reconcile_only],
## [method NetwScriptModel.PropertyConfig.converge]). Entity-level code writes
## the properties of [member NetwEntity.prediction] directly.
##
## Each fact has one source. This component applies its [member archetype]
## first and then pushes only the exports the scene moved off their defaults,
## so a scene value overrides the preset it refines and an export left alone
## does not. Between the scene and code, the later write wins.
## [codeblock]
## var prediction := NetwEntity.of(self).prediction
## prediction.sensors[&"ground"] = sample_ground
## prediction.witness_contacts = sample_contacts
## prediction.breach_response = NetwPredict.BreachResponse.DEMOTE
## prediction.island.from_interest()
## prediction.island.simulate_nearest(1)
## [/codeblock]
## [br][br][NetwPredictIsland] names the local prediction boundary. A promoted remote publishes predicted
## input with speculative simulation, runs through the same schedule, and
## independently rebases on each authority row. A witnessed contact outside
## the boundary follows the configured
## [enum NetwPredict.BreachResponse].
class_name PredictionComponent
extends NetwComponent

## Cadence that applies the entity's simulation drive.
enum Schedule {
	## Apply once for every network tick. Use this for kinematic prediction.
	TICK,
	## Apply once after every physics frame's network tick loop. Use this for
	## solver-driven bodies whose physics integrates once per frame.
	FRAME,
}

## What the server does for a missing input tick.
enum MissingInput {
	## No input, no movement. The honest cs-style default.
	STALL,
	## Carry the last input forward over the gap.
	REPEAT_LAST,
}

## How a reconciliation correction is applied to the body.
enum CorrectionMode {
	## Resolve from the body type: [constant REPLAY] for a kinematic body,
	## [constant SNAP] for a dynamic one. The tier emerges from the body.
	AUTO,
	## Restore authoritative state, then replay every unacked input over it. For
	## kinematic bodies, whose step is a plain callable (move_and_slide), so N
	## ticks can be re-run in one frame.
	REPLAY,
	## Restore authoritative state and stop, with no replay. For dynamic bodies,
	## whose solver cannot be stepped per input without a physics fork, so the
	## predicted body resumes forward from truth and the display chase absorbs the
	## snap.
	SNAP,
}

## How a [constant CorrectionMode.SNAP] restore places the authoritative state on
## the body.
enum RestoreMode {
	## Restore the authoritative state verbatim at its own tick.
	EXACT,
	## Project each field that names a [member NetwInterpolate.project_channel]
	## velocity sibling forward to the present tick before restoring, so a dynamic
	## body lands near where it is instead of snapping back to a stale tick. The
	## velocity must be replicated in the same state set.
	EXTRAPOLATED,
}

## What kind of body the entity predicts. Mirrors
## [enum NetwPredict.Archetype] by value.
enum Archetype {
	## No preset. Every knob keeps its own default until declared.
	NONE,
	## A body whose step is a plain callable, re-runnable within one frame.
	KINEMATIC,
	## A solver-integrated body whose step cannot be re-run per input.
	SOLVER_BODY,
}

## The one decision the schedule and recovery presets derive from, applied
## through [member NetwPredictionHandle.archetype]
## on tree entry. The preset is a floor: the bundle lands first and any export
## below moved off its default is written over it.
@export var archetype: Archetype = Archetype.NONE

@export_group("Schedule")

## Cadence that invokes [member simulate]. FRAME scheduling keeps a dynamic
## body at one drive application per physics frame while ticks label input.
@export var schedule: Schedule = Schedule.TICK

## Server policy for a missing input tick. See [enum MissingInput].
@export var missing_policy: MissingInput = MissingInput.STALL

## How many queued input ticks the server may consume in one tick when it has
## fallen behind. [code]1[/code] holds strict lockstep; a higher value lets the
## consume cursor recover from a hitch instead of ratcheting behind forever.
## Mirrors [member NetwPredictionHandle.max_consume_per_tick].
@export_range(1, 8) var max_consume_per_tick: int = 1

## How far the server's consume cursor may fall behind the freshest input before
## it re-opens at the live edge instead of walking there.
##
## The cursor heals a lost tick by stepping over it one per server tick, which
## never catches up to a controller authoring one per tick. A client that joins a
## running session and re-anchors its clock opens exactly such a gap, and without
## a ceiling its entity simulates forever without reconciling. [code]0[/code]
## disables the recovery. Mirrors
## [member NetwPredictionHandle.max_consume_lag_ticks].
@export_range(0, 600) var max_consume_lag_ticks: int = 60

## Queued input ticks the server keeps standing as a de-jitter buffer instead of
## consuming to empty. Input arrival phase drifts against the server's tick, and
## with no slack every late packet starves a tick and every early one bursts,
## which the authoritative body shows as slow-then-fast stutter. The depth is
## maintained rather than warmed up once: a tick that would drop below it holds
## and rebuilds the slack, and a drain trims a burst back down to it.
## [code]1[/code] or [code]2[/code] absorbs the drift at the cost of that much
## input latency.
## Mirrors [member NetwPredictionHandle.consume_buffer_ticks].
@export_range(0, 8) var consume_buffer_ticks: int = 0

## [constant Schedule.FRAME] tape transitions authority leaves standing instead
## of replaying, a fixed latency on every command that buys no jitter absorption
## back. Mirrors
## [member NetwPredictionHandle.replay_buffer_depth],
## whose documentation states why the default is zero.
@export_range(0, 8) var replay_buffer_depth: int = 0

@export_group("Recovery")

## Divergence above which a state receive triggers a correction.
##
## A single scalar mixes units badly for a 3D body, whose state spans meters,
## a unit quaternion, and meters per second. A field that needs its own
## threshold declares it in its own units with
## [method NetwScriptModel.PropertyConfig.epsilon], which overrides this
## default for that field alone.
@export var divergence_epsilon: float = 0.01

## How a correction is applied. See [enum CorrectionMode]. [constant CorrectionMode.AUTO]
## resolves [constant CorrectionMode.REPLAY] for a kinematic body and
## [constant CorrectionMode.SNAP] for a dynamic one.
@export var correction_mode: CorrectionMode = CorrectionMode.AUTO

## How a [constant CorrectionMode.SNAP] restore lands on the body. See
## [enum RestoreMode]. [constant RestoreMode.EXTRAPOLATED] carries a dynamic body
## forward to the present tick through its replicated velocity, so it holds for a
## body whose state set replicates velocity alongside the transform. Ignored under
## [constant CorrectionMode.REPLAY].
@export var snap_restore: RestoreMode = RestoreMode.EXACT

## Ceiling in ticks on the age a [constant RestoreMode.EXTRAPOLATED] restore
## projects across, so a server input cursor that falls behind never launches the
## body along a huge extrapolation. Mirrors
## [member NetwPredictionHandle.max_restore_ticks].
@export var max_restore_ticks: int = 6

## Pose error above which a recovery restores the whole closure instead of
## withholding the fields declared
## [method NetwScriptModel.PropertyConfig.teleport_only], in the pose field's
## own units. Mirrors
## [member NetwPredictionHandle.teleport_threshold].
@export var teleport_threshold: float = 2.0

## Ticks a [method notify_contact] pauses non-teleport corrections for. Mirrors
## [member NetwPredictionHandle.collision_cooldown_ticks].
@export_range(0, 30) var collision_cooldown_ticks: int = 6

## The simulation step, defaulting to the entity root's
## [code]_network_tick(delta, tick, is_fresh)[/code]. A delegating node may
## set its own callable. It is a single callable, never a fan-out, so exactly one
## authoritative step runs per entity per tick. Pushed to
## [member NetwPredictionHandle.simulate] on tree entry.
var simulate: Callable = Callable()

var _entity: NetwEntity
var _iface: NetwMultiplayer


func _init() -> void:
	name = "PredictionComponent"
	unique_name_in_owner = true


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	var root := get_parent()
	if root is RigidBody2D or root is RigidBody3D:
		warnings.append(
			"Predicting a RigidBody (Tier 2, SNAP correction). Replicate linear "
			+ "and angular velocity in the state set, not just the "
			+ "transform, or the solver re-derives momentum wrong after every "
			+ "correction and the post-collision settle repeats.",
		)
	warnings.append_array(_quantization_deadzone_warnings())
	return warnings


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	var entity := NetwEntity.of(self)
	if not entity:
		return
	_entity = entity
	_push_config(entity.prediction)
	# Prediction is meaningless without the tree's rewind substrate, so resolve it
	# through the required guard: it logs a clear error when this component sits
	# under a MultiplayerTree with no LagCompensation node, yet stays quiet for a
	# scene run standalone (no enclosing tree, e.g. pressing F6 to test in isolation).
	_iface = NetwMultiplayer.resolve_required(self)
	if _iface:
		var api := _iface
		if api:
			api.predict_declare(api.entity_of(entity.owner))


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	if is_instance_valid(_iface) and _entity:
		var api := _iface
		if api:
			api.predict_undeclare(api.entity_of(_entity.owner))
	_iface = null


# The exports the scene actually moved, pushed onto the entity's handle over the
# archetype bundle they refine.
#
# A default export is a valid choice, not a declaration, so a key left alone
# must not answer for the scene. This used to be settled by sending the handle a
# scene_declared map and having the bundle skip the keys it named, which meant
# every default was written twice -- once as the @export initializer, once as a
# literal in the comparison beside it -- and changing one silently moved the
# arbitration. The defaults now have one source, a pristine handle, and the
# ordering carries what the map used to: the preset lands first, and only the
# exports that differ are written over it.
#
# The arbitration is the component's, because the component is the only party
# that knows which source spoke. Nothing about it reaches the handle any more.
func _push_config(handle: NetwPredictionHandle) -> void:
	if archetype != Archetype.NONE:
		handle.archetype = archetype as NetwPredict.Archetype
	var pristine := NetwPredictionHandle.new()
	if schedule != pristine.schedule:
		handle.schedule = schedule as NetwPredict.Schedule
	if correction_mode != pristine.correction_mode:
		# The mechanism and the strategy are one fact at two altitudes, and a
		# preset names the strategy. A scene naming the mechanism directly has
		# overridden that choice, so the preset's policy is un-named as well --
		# otherwise resolved_recovery_policy() would keep answering with the
		# strategy the scene just outranked. This is the ordering doing what the
		# scene_declared map used to do for this pair.
		handle.recovery_policy = -1
		handle.correction_mode = correction_mode
	if snap_restore != pristine.snap_restore:
		handle.snap_restore = snap_restore
	if max_restore_ticks != pristine.max_restore_ticks:
		handle.max_restore_ticks = max_restore_ticks
	if missing_policy != pristine.missing_policy:
		handle.missing_policy = missing_policy
	if max_consume_per_tick != pristine.max_consume_per_tick:
		handle.max_consume_per_tick = max_consume_per_tick
	if consume_buffer_ticks != pristine.consume_buffer_ticks:
		handle.consume_buffer_ticks = consume_buffer_ticks
	if replay_buffer_depth != pristine.replay_buffer_depth:
		handle.replay_buffer_depth = replay_buffer_depth
	if max_consume_lag_ticks != pristine.max_consume_lag_ticks:
		handle.max_consume_lag_ticks = max_consume_lag_ticks
	if not is_equal_approx(teleport_threshold, pristine.teleport_threshold):
		handle.teleport_threshold = teleport_threshold
	if collision_cooldown_ticks != pristine.collision_cooldown_ticks:
		handle.collision_cooldown_ticks = collision_cooldown_ticks
	if not is_equal_approx(divergence_epsilon, pristine.divergence_epsilon):
		handle.divergence_epsilon = divergence_epsilon
	if simulate.is_valid():
		handle.simulate = simulate


# The one reconciliation invariant that is always wrong: a deadzone below a
# property's quantization error corrects on quant noise every packet. Both sides
# are static config (the quantizer on the state-set field, the threshold
# here), so this stays editor-only with no runtime cost.
func _quantization_deadzone_warnings() -> PackedStringArray:
	var out := PackedStringArray()
	var set := _owner_state_set()
	if not set:
		return out
	for field in set.columns:
		if field.lane != NetwPropertySet.Lane.VOLATILE:
			continue
		# The warning's whole premise is that the field corrects on noise, and no
		# class but causal can raise a correction at all. On any other class the
		# threshold bounds nothing, so demanding it clear the codec floor would
		# ask for a number that decides nothing.
		if field.property_class != NetwPropertySet.PropertyClass.CAUSAL:
			continue
		var codec: NetwQuantize = field.quantizer
		if not codec:
			continue
		var floor_error := codec.max_error(typeof(owner.get(field.key)) as Variant.Type)
		var epsilon := field.epsilon_override \
		if field.epsilon_override >= 0.0 else divergence_epsilon
		if epsilon < floor_error:
			out.append(
				(
						"Divergence epsilon for \"%s\" (%.4f) is below its codec's "
						+ "quantization error (%.4f). The predicted body corrects every "
						+ "packet from quantization noise alone. Raise the epsilon to at "
						+ "least %.4f."
				) % [field.key, epsilon, floor_error, floor_error],
			)
	return out


# The owner's derived state set, resolved statically from its script, so the
# editor lint reads the same fields, quantizers, and epsilon marks the runtime
# gathers. Null when the owner is scriptless or marks no state set.
func _owner_state_set() -> NetwPropertySet:
	if not owner:
		return null
	var script := owner.get_script() as Script
	if not script:
		return null
	return NetwPropertySet.from_script(script, NetwPropertySet.Record.RECORD_STATE)


## Opens a [member collision_cooldown_ticks] window pausing sub-teleport
## recoveries, so a contact transient is not corrected through. Call it when the
## predicted body registers a collision.
func notify_contact() -> void:
	if _entity and is_instance_valid(_iface):
		var api := _iface
		if api:
			api.predict_notify_contact(api.entity_of(_entity.owner))


## Sets whether the authoritative body is asleep. Corrections pause while asleep so
## reconciliation never nudges a sleeping body awake. Drive it from the dynamic
## body's sleep state.
func set_sleeping(value: bool) -> void:
	if _entity:
		_entity.prediction.sleeping = value


## Resolves [param mode] against [param body]'s type.
##
## [constant CorrectionMode.AUTO] picks [constant CorrectionMode.SNAP] for a
## dynamic body (a [RigidBody2D] or [RigidBody3D], whose solver cannot be stepped
## per input) and [constant CorrectionMode.REPLAY] otherwise (a kinematic body's
## step is a plain callable). An explicit mode passes through. Static so tooling
## and tests can resolve without a wired entity. Delegates to the engine's
## resolver so the rule lives in one place.
static func resolve_correction_mode_for(body: Node, mode: int) -> CorrectionMode:
	return NetwPredictionHandle.resolve_correction_mode_for(
		body,
		mode,
	) as CorrectionMode


## Returns true when any property in [param authoritative] exceeds its own
## threshold, reading [param epsilon] refined by the per-property [param overrides].
## Delegates to the engine's reconciliation math.
static func diverged(
		predicted: Dictionary,
		authoritative: Dictionary,
		epsilon: float,
		overrides: Dictionary,
) -> bool:
	return NetwPredictionHandle.diverged(
		predicted,
		authoritative,
		epsilon,
		overrides,
	)


## Returns the per-property error between two values, radians for a rotation.
## Delegates to the engine's reconciliation math.
static func value_error(a: Variant, b: Variant) -> float:
	return NetwPredictionHandle.value_error(a, b)
