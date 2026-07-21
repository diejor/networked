@tool
## Scene-first configuration for one entity's client prediction and reconciliation.
##
## The node declares the prediction config and registers a per-entity engine
## record. The kernel that actually predicts, consumes, and reconciles lives in
## [NetwLagCompensationInterface], keyed by [NetwEntity], reached and configured
## through [member NetwEntity.prediction]. This node is to that engine what
## [MultiplayerSynchronizer] is to [NetwSyncCompat]: a config carrier that pushes
## its exports into the handle on tree entry, calls
## [method NetwLagCompensationInterface.register_prediction], and releases the
## engine on exit. At most one wires per entity, so
## [member NetwEntity.prediction] has a single publisher.
## [codeblock]
## PlayerRoot (declares .state()/.input() marks, defines _network_tick)
## `-- PredictionComponent      # entity-level policy; registers the engine
##
## # on tree entry:
## entity.prediction.correction_mode = correction_mode   # push exports
## Netw.of(self).lag_compensation.register_prediction(entity)
## [/codeblock]
## The component carries entity-level policy only. A per-field fact (a
## tolerance, a restore restriction, a trigger exclusion) is a property fact
## and lives on the property mark
## ([method NetwScriptModel.PropertyConfig.epsilon],
## [method NetwScriptModel.PropertyConfig.teleport_only],
## [method NetwScriptModel.PropertyConfig.reconcile_only],
## [method NetwScriptModel.PropertyConfig.converge]), never here. A key
## declared here with a non-default value and also configured from code is a
## configuration error the handle reports, so every fact keeps one source.
##
## [br][b]The model the engine runs[/b]
## [br]The owning client predicts each tick and reconciles against the
## [member NetwSyncSetBinding.reconcile_ack] the server frames on its state stream,
## never against tick equality, so there is no clock lead and no determinism
## contract. Each entity has at most one input-owning peer
## ([member NetwEntity.controller]), so the [NetwTimeline] has exactly one writer
## per side and a correction replays only this entity. The controlling peer's
## input rides [constant NetwFrameEnvelope.Channel.PREDICT_COMMAND], each
## sample beside the transition it drove; the state set ships authoritative
## state back stamped with [member NetwSyncSetBinding.authored_tick] and
## carrying the ack, the last input tick the server consumed. That ack lets the
## client compare the server against its own past prediction tick for tick, not
## against the body it shows now.
## [codeblock]
## # command lane, client -> server  (one transition per authored tick)
## { index, label, fresh, &"motion": ... }
## # state set, server -> client   (reconcile_ack = last consumed input tick)
## { tick: authored_tick, ack: reconcile_ack, &"position": ... }
## [/codeblock]
## The predicted body snaps to the authoritative payload only when it diverges past
## [member NetwLagCompensationInterface.PredictionHandle.divergence_epsilon] (or the field's own
## [method NetwScriptModel.PropertyConfig.epsilon] mark), then
## every unacked input replays on top with [code]is_fresh = false[/code] so
## one-shot effects fire once. [member NetwLagCompensationInterface.PredictionHandle.is_reconciling] is true
## across the snap and replay, and [signal NetwLagCompensationInterface.PredictionHandle.divergence_detected]
## reports the divergence, naming the transition and what it is charged to.
class_name PredictionComponent
extends NetwComponent

## Per-entity role, resolved from authority at spawn and on control transfer.
enum Role {
	## A remote client controls the entity. Predicts and reconciles on ack.
	PREDICT,
	## The server consumes a remote peer's received input into authoritative state.
	CONSUME,
	## A listen-server host controls the entity. Simulates authoritatively from
	## local input each tick, so it neither predicts nor reconciles against itself.
	HOST_LOCAL,
	## A remote display. Never simulates here, the interpolator shows it.
	REMOTE,
}

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
## [enum NetwLagCompensationInterface.PredictionHandle.Archetype] by value.
enum Archetype {
	## No preset. Every knob keeps its own default until declared.
	NONE,
	## A body whose step is a plain callable, re-runnable within one frame.
	KINEMATIC,
	## A solver-integrated body whose step cannot be re-run per input.
	SOLVER_BODY,
}

## The one decision the schedule and recovery presets derive from, applied
## through
## [method NetwLagCompensationInterface.PredictionHandle.configure_prediction]
## on tree entry. The preset is a floor: any export below declared away from
## its default overrides its part of the bundle.
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
## Mirrors [member NetwLagCompensationInterface.PredictionHandle.max_consume_per_tick].
@export_range(1, 8) var max_consume_per_tick: int = 1

## How far the server's consume cursor may fall behind the freshest input before
## it re-opens at the live edge instead of walking there.
##
## The cursor heals a lost tick by stepping over it one per server tick, which
## never catches up to a controller authoring one per tick. A client that joins a
## running session and re-anchors its clock opens exactly such a gap, and without
## a ceiling its entity simulates forever without reconciling. [code]0[/code]
## disables the recovery. Mirrors
## [member NetwLagCompensationInterface.PredictionHandle.max_consume_lag_ticks].
@export_range(0, 600) var max_consume_lag_ticks: int = 60

## Queued input ticks the server keeps standing as a de-jitter buffer instead of
## consuming to empty. Input arrival phase drifts against the server's tick, and
## with no slack every late packet starves a tick and every early one bursts,
## which the authoritative body shows as slow-then-fast stutter. The depth is
## maintained rather than warmed up once: a tick that would drop below it holds
## and rebuilds the slack, and a drain trims a burst back down to it.
## [code]1[/code] or [code]2[/code] absorbs the drift at the cost of that much
## input latency.
## Mirrors [member NetwLagCompensationInterface.PredictionHandle.consume_buffer_ticks].
@export_range(0, 8) var consume_buffer_ticks: int = 0

## [constant Schedule.FRAME] tape transitions the server keeps standing before it
## replays one per physics frame. The one-transition default absorbs a single
## missed arrival at the cost of one frame of input latency. Mirrors
## [member NetwLagCompensationInterface.PredictionHandle.replay_buffer_depth].
@export_range(0, 8) var replay_buffer_depth: int = 1

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
## [member NetwLagCompensationInterface.PredictionHandle.max_restore_ticks].
@export var max_restore_ticks: int = 6

## Pose error above which a recovery restores the whole closure instead of
## withholding the fields declared
## [method NetwScriptModel.PropertyConfig.teleport_only], in the pose field's
## own units. Mirrors
## [member NetwLagCompensationInterface.PredictionHandle.teleport_threshold].
@export var teleport_threshold: float = 2.0

## Ticks a [method notify_contact] pauses non-teleport corrections for. Mirrors
## [member NetwLagCompensationInterface.PredictionHandle.collision_cooldown_ticks].
@export_range(0, 30) var collision_cooldown_ticks: int = 6

## The simulation step, defaulting to the entity root's
## [code]_network_tick(delta, tick, is_fresh)[/code]. A delegating node may
## set its own callable. It is a single callable, never a fan-out, so exactly one
## authoritative step runs per entity per tick. Pushed to
## [member NetwLagCompensationInterface.PredictionHandle.simulate] on tree entry.
var simulate: Callable = Callable()

var _entity: NetwEntity
var _iface: NetwLagCompensationInterface


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
	_iface = NetwLagCompensationInterface.resolve_required(self)
	if _iface:
		_iface.register_prediction(entity)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	if is_instance_valid(_iface) and _entity:
		_iface.unregister_prediction(_entity)
	_iface = null


# Pushes the node's exports into the entity's prediction handle so the engine
# reads its config from one place, and records which verb keys the scene
# declared away from their defaults so a code verb restating one is caught as
# the two-source error it is.
func _push_config(handle: NetwLagCompensationInterface.PredictionHandle) -> void:
	handle.schedule = schedule
	handle.correction_mode = correction_mode
	handle.snap_restore = snap_restore
	handle.max_restore_ticks = max_restore_ticks
	handle.missing_policy = missing_policy
	handle.max_consume_per_tick = max_consume_per_tick
	handle.consume_buffer_ticks = consume_buffer_ticks
	handle.replay_buffer_depth = replay_buffer_depth
	handle.max_consume_lag_ticks = max_consume_lag_ticks
	handle.teleport_threshold = teleport_threshold
	handle.collision_cooldown_ticks = collision_cooldown_ticks
	handle.divergence_epsilon = divergence_epsilon
	if simulate.is_valid():
		handle.simulate = simulate
	# A default export is a valid choice, not a declaration, so only a value
	# the scene moved off its default claims the key.
	var declared: Dictionary[StringName, bool] = { }
	if schedule != Schedule.TICK:
		declared[&"tier"] = true
	if missing_policy != MissingInput.STALL:
		declared[&"hold"] = true
	if replay_buffer_depth != 1:
		declared[&"buffer_depth"] = true
	if max_consume_lag_ticks != 60:
		declared[&"resync_ceiling"] = true
	if not is_equal_approx(divergence_epsilon, 0.01):
		declared[&"epsilon"] = true
	if not is_equal_approx(teleport_threshold, 2.0):
		declared[&"teleport_threshold"] = true
	if collision_cooldown_ticks != 6:
		declared[&"cooldown_ticks"] = true
	if snap_restore != RestoreMode.EXACT:
		declared[&"projection"] = true
	handle.scene_declared = declared
	# The bundle lands after the declared keys are known, so an export moved
	# off its default keeps outranking the preset it refines.
	if archetype != Archetype.NONE:
		handle.configure_prediction(
			archetype as NetwLagCompensationInterface.PredictionHandle.Archetype,
		)


# The one reconciliation invariant that is always wrong: a deadzone below a
# property's quantization error corrects on quant noise every packet. Both sides
# are static config (the quantizer on the state-set field, the threshold
# here), so this stays editor-only with no runtime cost.
func _quantization_deadzone_warnings() -> PackedStringArray:
	var out := PackedStringArray()
	var set := _owner_state_set()
	if not set:
		return out
	for field in set.fields:
		if field.lane != NetwSyncSet.Lane.VOLATILE:
			continue
		var codec: NetwQuantize = field.quantizer
		if not codec:
			continue
		var floor_error := codec._max_error(typeof(owner.get(field.key)) as Variant.Type)
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
func _owner_state_set() -> NetwSyncSet:
	if not owner:
		return null
	var script := owner.get_script() as Script
	if not script:
		return null
	return NetwSyncSet.from_script(script, NetwSyncSet.Record.RECORD_STATE)


## Opens a [member collision_cooldown_ticks] window pausing sub-teleport
## recoveries, so a contact transient is not corrected through. Call it when the
## predicted body registers a collision.
func notify_contact() -> void:
	if _entity:
		_entity.prediction.notify_contact()


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
	return NetwLagCompensationInterface.PredictionHandle.resolve_correction_mode_for(
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
	return NetwLagCompensationInterface.PredictionHandle.diverged(
		predicted,
		authoritative,
		epsilon,
		overrides,
	)


## Returns the per-property error between two values, radians for a rotation.
## Delegates to the engine's reconciliation math.
static func value_error(a: Variant, b: Variant) -> float:
	return NetwLagCompensationInterface.PredictionHandle.value_error(a, b)

