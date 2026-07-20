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
## └── PredictionComponent      # exports + deadzone rows; registers the engine
##
## # on tree entry:
## entity.prediction.correction_mode = correction_mode   # push exports
## Netw.of(self).lag_compensation.register_prediction(entity)
## [/codeblock]
##
## [br][b]The model the engine runs[/b]
## [br]The owning client predicts each tick and reconciles against the
## [member NetwSyncSetBinding.reconcile_ack] the server frames on its state stream,
## never against tick equality, so there is no clock lead and no determinism
## contract. Each entity has at most one input-owning peer
## ([member NetwEntity.controller]), so the [NetwTimeline] has exactly one writer
## per side and a correction replays only this entity. The input set ships the
## controlling peer's input toward the server stamped with
## [member NetwSyncSetBinding.authored_tick]; the state set ships authoritative
## state back stamped the same way and carrying the ack, the last input tick the
## server consumed. That ack lets the client compare the server against its own
## past prediction tick for tick, not against the body it shows now.
## [codeblock]
## # input set, client -> server   (authored_tick = the tick it was gathered)
## { tick: authored_tick, &"motion": ... }
## # state set, server -> client   (reconcile_ack = last consumed input tick)
## { tick: authored_tick, ack: reconcile_ack, &"position": ... }
## [/codeblock]
## The predicted body snaps to the authoritative payload only when it diverges past
## [member NetwLagCompensationInterface.PredictionHandle.divergence_epsilon] (or a per-property override), then
## every unacked input replays on top with [code]is_fresh = false[/code] so
## one-shot effects fire once. [member NetwLagCompensationInterface.PredictionHandle.is_reconciling] is true
## across the snap and replay, and [signal NetwLagCompensationInterface.PredictionHandle.reconciled] carries the
## divergence.
##
## [br][b]Reconciliation deadzones[/b]
## [br]The per-property thresholds are edited in the inspector under
## [code]Reconciliation Deadzones[/code], one row per state field, typed by the
## property so a rotation row reads in radians. Each row backs
## [member NetwLagCompensationInterface.PredictionHandle.divergence_epsilon_overrides]. A row left at
## [member divergence_epsilon] inherits it.
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
	## Ease a dynamic body onto the authoritative pose over several ticks with an
	## error-smoothing spring, hard-snapping only past [member teleport_threshold].
	## Pauses around contacts ([method notify_contact]) and while [member sleeping],
	## so a wall bounce or a settled body is not sprung. The industry-standard answer
	## to dynamic-body rubber banding.
	SNAP_BLEND,
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

## Divergence above which a state receive triggers a correction.
##
## A single scalar mixes units badly for a 3D body, whose state spans meters,
## a unit quaternion, and m·s⁻¹, so [member divergence_epsilon_overrides] sets a
## per-property threshold where one is needed.
@export var divergence_epsilon: float = 0.01

## Per-property divergence thresholds overriding [member divergence_epsilon].
##
## Edited per property in the inspector under [code]Reconciliation Deadzones[/code],
## one row per state-set field, typed by the property so a
## rotation row reads in radians and a position row in units. A row left at
## [member divergence_epsilon] inherits it (the inspector revert arrow clears the
## override). This dictionary is the backing store, also settable from code for
## programmatically registered state. Keyed by the virtual property name.
@export var divergence_epsilon_overrides: Dictionary[StringName, float] = { }

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

## State fields that a correction restores but that never trigger one on their own.
##
## A cosmetic scalar or a display-owned heading still reconciles to the
## authoritative value when another field corrects, yet its own drift never
## teleports the whole state set. Backs
## [member NetwLagCompensationInterface.PredictionHandle.correction_trigger_excludes].
@export var reconcile_only_fields: Array[StringName] = []

## State fields that only a teleport-tier correction restores.
##
## A sub-threshold [constant CorrectionMode.SNAP_BLEND] correction leaves them on
## the predicted body, so a contractive field that re-converges on its own (a
## damped velocity, a lerp-toward scalar) is never rewound to the stale ack tick,
## which under acceleration reads as losing speed on every correction. A
## correction past [member teleport_threshold] still restores them. Pair with
## [member reconcile_only_fields] so the field neither triggers nor rewinds below
## the teleport tier. Backs
## [member NetwLagCompensationInterface.PredictionHandle.teleport_only_restore].
@export var teleport_only_restore_fields: Array[StringName] = []

## Per-tick fraction of its remaining error that a field is eased toward
## authority by, keyed by state field. Such a field is never written by a
## sub-teleport correction, only pulled a little every tick.
##
## Use it for a field that drives the simulation but has no
## [member NetwInterpolate.project_channel] derivative to project with, a
## velocity above all. Restoring one writes the value it held at the ack tick,
## which mid-manoeuvre forks the body again, while leaving it alone lets a fork
## outlive every correction and keep regenerating the error. Around
## [code]0.05[/code] converges over roughly twenty ticks without a visible step.
## Backs [member NetwLagCompensationInterface.PredictionHandle.soft_restore_stiffness].
@export var soft_restore_stiffness: Dictionary[StringName, float] = { }

@export_group("Blend Correction")

## Ticks a [constant CorrectionMode.SNAP_BLEND] correction eases the body onto the
## authoritative pose over. Mirrors
## [member NetwLagCompensationInterface.PredictionHandle.blend_ticks].
@export_range(1, 30) var blend_ticks: int = 8

## Per-tick fraction of the remaining [constant CorrectionMode.SNAP_BLEND] error
## applied each tick, the spring stiffness. Mirrors
## [member NetwLagCompensationInterface.PredictionHandle.blend_stiffness].
@export_range(0.01, 1.0, 0.01) var blend_stiffness: float = 0.25

## Pose error above which a [constant CorrectionMode.SNAP_BLEND] correction abandons
## the spring and hard-teleports, in the pose field's own units. Mirrors
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
# reads its config from one place. The overrides dictionary is shared by
# reference, so a code-side edit and the inspector rows stay in sync.
func _push_config(handle: NetwLagCompensationInterface.PredictionHandle) -> void:
	handle.correction_mode = correction_mode
	handle.snap_restore = snap_restore
	handle.max_restore_ticks = max_restore_ticks
	handle.missing_policy = missing_policy
	handle.max_consume_per_tick = max_consume_per_tick
	handle.consume_buffer_ticks = consume_buffer_ticks
	handle.max_consume_lag_ticks = max_consume_lag_ticks
	handle.blend_ticks = blend_ticks
	handle.blend_stiffness = blend_stiffness
	handle.teleport_threshold = teleport_threshold
	handle.collision_cooldown_ticks = collision_cooldown_ticks
	handle.divergence_epsilon = divergence_epsilon
	handle.divergence_epsilon_overrides = divergence_epsilon_overrides
	var excludes: Dictionary[StringName, bool] = { }
	for field: StringName in reconcile_only_fields:
		excludes[field] = true
	handle.correction_trigger_excludes = excludes
	var teleport_only: Dictionary[StringName, bool] = { }
	for field: StringName in teleport_only_restore_fields:
		teleport_only[field] = true
	handle.teleport_only_restore = teleport_only
	handle.soft_restore_stiffness = soft_restore_stiffness
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
	for field in set.fields:
		if field.lane != NetwSyncSet.Lane.VOLATILE:
			continue
		var codec: NetwQuantize = field.quantizer
		if not codec:
			continue
		var floor_error := codec._max_error(typeof(owner.get(field.key)) as Variant.Type)
		var epsilon := _epsilon_for(field.key)
		if epsilon < floor_error:
			out.append(
				(
						"Reconciliation deadzone for \"%s\" (%.4f) is below its codec's "
						+ "quantization error (%.4f). The predicted body corrects every "
						+ "packet from quantization noise alone. Raise the deadzone to at "
						+ "least %.4f."
				) % [field.key, epsilon, floor_error, floor_error],
			)
	return out


# The owner's derived state set, resolved statically from its script, so the
# editor lint and the deadzone rows read the same fields and quantizers the
# runtime gathers. Null when the owner is scriptless or marks no state set.
func _owner_state_set() -> NetwSyncSet:
	if not owner:
		return null
	var script := owner.get_script() as Script
	if not script:
		return null
	return NetwSyncSet.from_script(script, NetwSyncSet.Record.RECORD_STATE)


func _epsilon_for(key: StringName) -> float:
	return divergence_epsilon_overrides.get(key, divergence_epsilon)


## Opens a [member collision_cooldown_ticks] window pausing non-teleport
## [constant CorrectionMode.SNAP_BLEND] corrections, so a contact transient is not
## corrected through. Call it when the predicted body registers a collision.
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
		body, mode,
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
		predicted, authoritative, epsilon, overrides,
	)


## Returns the per-property error between two values, radians for a rotation.
## Delegates to the engine's reconciliation math.
static func value_error(a: Variant, b: Variant) -> float:
	return NetwLagCompensationInterface.PredictionHandle.value_error(a, b)

# ---------------------------------------------------------------------------
# Inspector: per-property deadzone rows
# ---------------------------------------------------------------------------

# One editor row per state-set field, backed by
# divergence_epsilon_overrides. Mirrors MultiplayerInterpolator's per-property
# inspector so a deadzone is set by name, typed by the property (radians for a
# rotation, units for a position), instead of hand-editing an opaque dictionary.
const _DEADZONE_PREFIX := "deadzone/"


func _validate_property(property: Dictionary) -> void:
	# The dictionary is the backing store. The per-property rows are the surface.
	if property.name == "divergence_epsilon_overrides":
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE


func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	if not Engine.is_editor_hint() or not owner:
		return props

	var rows := _deadzone_rows()
	if rows.is_empty():
		return props

	props.append(
		{
			"name": "Reconciliation Deadzones",
			"type": TYPE_NIL,
			"usage": PROPERTY_USAGE_GROUP,
			"hint_string": _DEADZONE_PREFIX,
		},
	)
	for clean_name: StringName in rows:
		props.append(
			{
				"name": _DEADZONE_PREFIX + clean_name,
				"type": TYPE_FLOAT,
				"usage": PROPERTY_USAGE_EDITOR,
				"hint": PROPERTY_HINT_RANGE,
				"hint_string": _deadzone_hint(rows[clean_name]),
			},
		)
	return props


func _get(property: StringName) -> Variant:
	if property.begins_with(_DEADZONE_PREFIX):
		var key := StringName(property.trim_prefix(_DEADZONE_PREFIX))
		return divergence_epsilon_overrides.get(key, divergence_epsilon)
	return null


func _set(property: StringName, value: Variant) -> bool:
	if not property.begins_with(_DEADZONE_PREFIX):
		return false
	var key := StringName(property.trim_prefix(_DEADZONE_PREFIX))
	# Storing the global default means "inherit it", so drop the override. This is
	# also what the inspector revert arrow lands on.
	if is_equal_approx(float(value), divergence_epsilon):
		divergence_epsilon_overrides.erase(key)
	else:
		divergence_epsilon_overrides[key] = float(value)
	return true


func _property_can_revert(property: StringName) -> bool:
	if property.begins_with(_DEADZONE_PREFIX):
		return divergence_epsilon_overrides.has(
			StringName(property.trim_prefix(_DEADZONE_PREFIX)),
		)
	return false


func _property_get_revert(property: StringName) -> Variant:
	if property.begins_with(_DEADZONE_PREFIX):
		return divergence_epsilon
	return null


# Maps each state property name to its Variant type for the editor rows. State
# props only, since reconciliation compares the state-set payload. The type is
# read off the entity root (owner.get), which resolves at edit time. Existing
# overrides are unioned in so a code-registered threshold stays visible and
# revertable.
func _deadzone_rows() -> Dictionary:
	var rows: Dictionary = { }
	var set := _owner_state_set()
	if set:
		for field in set.fields:
			if field.lane != NetwSyncSet.Lane.VOLATILE:
				continue
			rows[field.key] = typeof(owner.get(field.key))
	for key: StringName in divergence_epsilon_overrides:
		if not rows.has(key):
			rows[key] = TYPE_NIL
	return rows


func _deadzone_hint(value_type: int) -> String:
	var suffix := ""
	match value_type:
		TYPE_QUATERNION, TYPE_BASIS, TYPE_TRANSFORM2D, TYPE_TRANSFORM3D:
			suffix = ",suffix:rad"
		TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
			suffix = ",suffix:u"
	return "0.0,100.0,0.0001,or_greater" + suffix
