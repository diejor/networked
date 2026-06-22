## Smooths a networked entity's displayed motion between the sparse snapshots that
## arrive over the wire.
##
## Snapshots land every few ticks and jittery, so this component never displays the
## newest one. It plays the visual back behind the live state by a buffer delay, so
## there is always a later snapshot to interpolate toward and motion stays smooth
## across a gap. The trade is a fixed display latency of roughly that buffer depth.
## This is the display half of a networked entity. [PredictionComponent] owns the
## body, this owns only what the body looks like.
##
## [br][br][b]The playhead[/b]
## [br]Each tracked property keeps a [NetwRingBuffer] of recorded snapshots keyed by
## tick, exposed through [method get_buffer]. A playhead reads that buffer behind
## the newest entry, lands between two recorded ticks, and the displayed value is
## the interpolation between them. [member display_lag] is how far behind the newest
## entry the playhead sits.
## [codeblock]
## history (one per property), keyed by tick:
##    9       12       15       18       21        newest received = 21
##    ●        ●        ●        ●        ●
##                      |---- playhead ---|
##                      prev = 15   dt = 16.4   next = 18
##
## displayed = lerp(state[15], state[18], 0.4)
##
## the playhead trails the newest by display_lag, so a next snapshot always exists
## [/codeblock]
##
## [b]Three roles[/b]
## [br]The same node serves three display strategies, resolved from [NetwEntity]
## control and [member Node.multiplayer_authority] unless [member display_role]
## overrides it. The split mirrors [PredictionComponent]. An entity this peer
## simulates is chased live, an entity it only receives is played back, and an
## entity it has authority over needs no smoothing.
## [codeblock]
## func _resolve_display_role():
##     if display_role != DisplayRole.AUTO:
##         return display_role
##     if _has_prediction_component() and _is_controlled_locally():
##         return DisplayRole.PREDICTED   # we simulate it live; chase the predicted body
##     if owner.is_multiplayer_authority():
##         return DisplayRole.DISABLED    # we are the authority; the body is truth
##     return DisplayRole.REMOTE          # someone else's entity; play snapshots back
## [/codeblock]
##
## [b]Playing the playhead[/b]
## [br]Once per frame the playhead is placed in the history's tick space from the
## [MultiplayerClock] display clock, then every property lerps across the two ticks
## that bracket it. [member MultiplayerClock.display_tick] already trails the
## simulation by [member MultiplayerClock.display_offset], and [member display_lag]
## subtracts the extra jitter buffer on top.
## [codeblock]
## # once per frame, per remote entity
## var time := clock.display_tick + clock.tick_factor - display_lag
## var dt := floori(time)              # the playhead tick
## var factor := time - dt             # progress from dt toward dt + 1
## _display_tick = dt
## for state in _states:
##     var bracket := state.history.find_bracketing_ticks(dt)   # [prev, next]
##     state.apply(dt, factor)         # lerp(history[bracket.x], history[bracket.y], t)
## [/codeblock]
##
## [b]Smart dilation[/b]
## [br]The buffer depth is not fixed. [member display_lag] eases toward a floor
## derived from the expected snapshot interval and the clock display offset, low
## passed by [member floor_smoothing] and tracked at [member lag_adapt_rate]. When
## snapshots starve no recorded tick sits after the playhead and it would run off
## the end. After [member starvation_grace_frames] of starving the lag grows by
## [member starvation_growth] up to [member max_extra_dilation] to rebuild the
## buffer, and [member starvation_ticks] counts the current starving streak.
## [member enable_smart_dilation] turns the whole loop off for a fixed zero lag.
##
## [br][br][b]Naming the displayed tick[/b]
## [br]When exactly one stamped [StateSynchronizer] drives the entity, history is
## keyed by the packet's authoring [code]__tick[/code]
## ([member StampedSynchronizer.last_received_tick]) instead of the receive tick.
## That lets [method displayed_authoring_tick] name the exact server tick under the
## playhead, which a firing client sends to the server for lag-compensated rewind
## through [method NetwLagCompensation.sample] and [method NetwLagCompensation.rewind].
## The displayed tick is half a round trip behind the live server state, which is
## exactly the world the shooter saw.
## [codeblock]
## packet { __tick = 15, position = ... }   recorded at history key 15
## playhead lands under tick 15   ->   displayed_authoring_tick() == 15
## the shooter sends 15 to the server, which rewinds every target to tick 15
## [/codeblock]
##
## [b]Predicted display[/b]
## [br]A locally predicted entity has no jitter buffer to drain, because its body is
## simulated live every frame and races ahead of the network. [member predicted_mode]
## picks the filter. [constant PredictedMode.CHASE] eases the visual toward the live
## body with an exponential time from [member predicted_smooth_time].
## [constant PredictedMode.BRACKETED] records the predicted body each tick and
## interpolates the previous and current samples like a remote entity.
##
## [br][br][b]Writing the visual[/b]
## [br]Smoothing must not fight the physics engine, so [member visual_root] points at
## a child node that carries the smooth output while the owner body keeps the snapped
## network pose. [member visual_output_mode] decides how the value lands.
## [constant VisualOutputMode.OWNER_TRANSFORM_COMPENSATED] keeps the child at the
## smooth owner pose, [constant VisualOutputMode.SOURCE_DELTA] applies the smoothed
## delta to the child's initial offset, and [constant VisualOutputMode.PROPERTY_VALUE]
## writes the value straight. [method snap_property] and [method reset] bypass
## smoothing for teleports.
@tool
class_name MultiplayerInterpolator
extends NetwComponent

#region ── Enums ───────────────────────────────────────────────────────────────

## Defines the interpolation algorithm for a property.
enum Mode {
	NONE = 0, ## No interpolation.
	LERP = 1, ## Linear interpolation.
	ANGLE = 2, ## Angular interpolation (shortest path).
	SLERP = 3, ## Spherical interpolation for [Quaternion] rotations.
}

## Defines how interpolated values are written to [member visual_root].
enum VisualOutputMode {
	AUTO = 0, ## Choose the safest mode from source and target properties.
	PROPERTY_VALUE = 1, ## Write the interpolated value directly.
	SOURCE_DELTA = 2, ## Apply the smooth source delta to the initial value.
	OWNER_TRANSFORM_COMPENSATED = 3, ## Keep the visual at the smooth owner pose.
}

## Selects the display role when automatic role resolution is not desired.
enum DisplayRole {
	AUTO = 0, ## Resolve from [NetwEntity] control and authority.
	REMOTE = 1, ## Use the remote snapshot interpolation path.
	PREDICTED = 2, ## Use the local predicted display path.
	DISABLED = 3, ## Disable display smoothing.
}

## Selects how predicted entities drive their visual output.
enum PredictedMode {
	CHASE = 0, ## Ease the visual toward the live predicted body every frame.
	BRACKETED = 1, ## Interpolate between the previous and current tick samples.
}

#endregion

#region ── Configuration ───────────────────────────────────────────────────────

@export_group("Display")

## Role override for the display strategy.
##
## [constant DisplayRole.AUTO] derives the strategy from [NetwEntity] control
## and [member Node.multiplayer_authority]. Only set another value for tooling
## or special scenes whose display role is intentionally fixed.
@export var display_role: DisplayRole = DisplayRole.AUTO:
	set(v):
		display_role = v
		if is_inside_tree() and not Engine.is_editor_hint():
			_role_dirty = true
			_resolve_strategy(true)

## Dictionary mapping property names to their [enum MultiplayerInterpolator.Mode].
@export var property_modes: Dictionary[StringName, Mode] = { }:
	set(v):
		property_modes = v
		_refresh_property_states()
		update_configuration_warnings()
		notify_property_list_changed()

## If set, smooth output will be written to this child node instead of the
## parent node.
## This is recommended for physics bodies ([code]CharacterBody2D[/code], etc.)
## to prevent the interpolator from fighting with the physics engine.
@export var visual_root: NodePath = NodePath(""):
	set(v):
		visual_root = v
		_refresh_property_states()
		update_configuration_warnings()
		notify_property_list_changed()

## Maps source property names to target property names on the
## [member visual_root].
## [br]Use when the interpolated property on the owner has a different name
## than the property on the visual root.
## [br]Example: [code]{ "synced_position": "position" }[/code] writes
## smoothed values to the visual root's [code]position[/code] instead of
## [code]synced_position[/code].
@export var visual_root_property_map: Dictionary[StringName, StringName] = { }:
	set(v):
		visual_root_property_map = v
		_refresh_property_states()
		update_configuration_warnings()
		notify_property_list_changed()

## Controls how values are written when [member visual_root] is set.
## [br][br]
## [enum VisualOutputMode.AUTO] makes child [code]position[/code] targets
## compensate against the owner's actual transform, which is the usual
## server-authoritative character setup.
@export var visual_output_mode: VisualOutputMode = VisualOutputMode.AUTO:
	set(v):
		visual_output_mode = v
		_refresh_property_states()
		update_configuration_warnings()

## If [code]true[/code], the interpolator will locally slow down its playhead
## when snapshots are missing to prevent visual jitter.
@export var enable_smart_dilation: bool = true

## Exponential smoothing time constant in seconds, layered on top of the
## bracketed interpolation.
## [br]- [code]0.0[/code]: Crisp and instant (pure time-based interpolation).
## [br]- [code]> 0.0[/code]: Heavier, fluid motion that lags by roughly this
## many seconds. Frame-rate independent.
@export_custom(0, "suffix:s") var smoothing_time: float = 0.05

## Maximum distance allowed before the interpolator snaps to the target instead
## of lerping.
## Useful for teleports. Set to [code]0.0[/code] to disable.
@export_custom(0, "suffix:px") var max_lerp_distance: float = 0.0

## The maximum number of extra ticks the interpolator can dilate beyond its
## floor. Increasing this value might help when network jitters.
@export_custom(0, "suffix:ticks") var max_extra_dilation: float = 0.0

## Per-frame fraction [member display_lag] eases toward its resting floor.
## Higher tracks network changes faster, lower is steadier.
@export_range(0.0, 1.0) var lag_adapt_rate: float = 0.05

## Ticks per frame the buffer grows once starvation is sustained past
## [member starvation_grace_frames].
@export_range(0.0, 1.0) var starvation_growth: float = 0.95

## Per-frame fraction the measured floor is low-passed before
## [member display_lag] tracks it. Lower rejects more jitter.
@export_range(0.0, 1.0) var floor_smoothing: float = 0.05

## Consecutive starving frames tolerated before the buffer starts growing.
@export_custom(0, "suffix:frames") var starvation_grace_frames: int = 3

## If greater than [code]0[/code], the interpolator will log its internal state
## every N frames.
@export_custom(0, "suffix:frames") var trace_interval: int = 0

@export_group("Predicted Display")

## Exponential smoothing time for [constant PredictedMode.CHASE].
##
## [code]0.0[/code] derives the time from [member MultiplayerClock.ticktime] so
## low tick rates smooth one tick of motion without adding a full tick of input
## latency.
@export_custom(0, "suffix:s") var predicted_smooth_time: float = 0.0

## Display filter used when this node resolves to a predicted role.
@export var predicted_mode: PredictedMode = PredictedMode.CHASE

#endregion

#region ── Public Metrics ──────────────────────────────────────────────────────

## The current extra delay (in ticks) added by Smart Dilation.
var display_lag: float = 0.0

## How many consecutive frames the interpolator has been starving for snapshots.
var starvation_ticks: int = 0

#endregion

#region ── Public API ──────────────────────────────────────────────────────────

## Instantly snaps [param property] to [param value], bypassing interpolation.
## [codeblock]
## # Snap a base property
## interpolator.snap_property(&"position", Vector2(200, 100))
## 
## # Snap a nested child property (must match the exact key in property_modes)
## interpolator.snap_property(&"Sprite2D:position", Vector2.ZERO)
## [/codeblock]
func snap_property(property: StringName, value: Variant) -> void:
	if _strategy:
		_strategy.snap_property(self, property, value)


## Clears all recorded history and resets the visual state to match 
## the current raw positions. 
## Call this after manual teleports or significant state changes 
## to prevent the interpolator from "sliding" the node across the map.
func reset() -> void:
	if not _clock:
		_clock = get_multiplayer_clock()

	if _strategy:
		_strategy.reset(self)
		return

	_reset_display_states()


## Returns the server authoring tick currently displayed for this entity, or
## [code]-1[/code] when it cannot be named.
##
## A firing client carries this in its fire request so the server can rewind to
## the tick the shooter actually saw, the [param tick] argument of
## [method NetwLagCompensation.sample] and [method NetwLagCompensation.rewind]. It
## is the authoring [code]__tick[/code] of the snapshot under the interpolation
## playhead, which is half a round trip behind the live server state.
##
## [codeblock]
## var view_tick := interpolator.displayed_authoring_tick()
## if view_tick >= 0:
##     fire_at_server.rpc_id(1, aim, view_tick)
## [/codeblock]
##
## Returns [code]-1[/code] unless authoring-tick keying is active (exactly one
## stamped [StateSynchronizer] drives the entity), and on an authority entity or
## before the first snapshot is displayed, where no tick can be named.
func displayed_authoring_tick() -> int:
	return _strategy.displayed_authoring_tick(self) if _strategy else -1


## Returns the [NetwRingBuffer] for the given [param property], or [code]null[/code]
## if not found.
func get_buffer(property: StringName) -> NetwRingBuffer:
	return _strategy.get_buffer(self, property) if _strategy else null


## Temporarily disables interpolation for [param duration] seconds.
## Returns a [SceneTreeTimer] that can be awaited.
func disable_for(duration: float) -> SceneTreeTimer:
	process_mode = PROCESS_MODE_DISABLED
	reset()
	var self_ref := weakref(self)
	var timer := get_tree().create_timer(duration)
	timer.timeout.connect(
		func():
			var inst = self_ref.get_ref()
			if inst and is_instance_valid(inst) and inst.owner and not inst.owner.is_multiplayer_authority():
				inst.process_mode = PROCESS_MODE_INHERIT
	)
	return timer

#endregion

#region ── Internal State ──────────────────────────────────────────────────────

var _clock: MultiplayerClock
var _states: Array[_PropertyState] = []
var _trace_frame: int = 0

var _expected_interval_ticks: int = 3
var _has_explicit_sync_interval: bool = false

# The single stamped state synchronizer whose authoring __tick keys history, or
# null to fall back to the receive tick. Resolved in _compute_sync_intervals.
var _authoring_sync: StampedSynchronizer = null

# Last playhead tick computed in _update_instance, in the history's tick space
# (authoring ticks when authoring-tick keying is active). Drives
# displayed_authoring_tick(). -1 until the first interpolated frame.
var _display_tick: int = -1

var _peer_batcher: _Batcher
var _strategy: _Strategy
var _strategy_role: DisplayRole = DisplayRole.DISABLED
# Set when the resolved display role may be stale. The role only changes on
# control transfer or an explicit display_role override, so steady-state frames
# skip resolution and reuse the cached _strategy.
var _role_dirty: bool = true
var _entity: NetwEntity
# Saved freeze intent of a RigidBody owner auto-frozen while it is displayed
# remotely, restored on control transfer. Empty when not managing the body.
var _saved_freeze: Dictionary = { }
var _dbg: NetwHandle = Netw.dbg.handle(self)

# Persists the visual_root's design-time local offset so it survives _ready
# re-runs (e.g. after a teleport reparent that calls request_ready). Without
# this, _refresh_property_states/reset would re-capture the visual_root's
# current local position, which already contains the last frame's smoothing
# correction, baking that correction into the new baseline.
var _cached_initial_offsets: Dictionary[StringName, Variant] = { }
var _cached_initial_global_offsets: Dictionary[StringName, Variant] = { }

#endregion

#region ── Lifecycle ───────────────────────────────────────────────────────────

func _ready() -> void:
	if Engine.is_editor_hint():
		return

	if not owner:
		owner = get_parent()
		if owner:
			_dbg.warn(
				"MultiplayerInterpolator: 'owner' property is not set. Falling back to parent node '%s'. " +
				"Assign the owner explicitly for better stability.",
				[owner.name],
			)

	process_priority = 100
	_clock = get_multiplayer_clock()

	assert(owner, "MultiplayerInterpolator: owner is missing.")
	assert(_clock, "MultiplayerInterpolator: Requires a MultiplayerClock on the multiplayer API.")

	_peer_batcher = get_bucket(_Batcher) as _Batcher
	if _peer_batcher:
		_peer_batcher.register(self, _clock)

	_entity = NetwEntity.of(self)
	if _entity and not _entity.control_changed.is_connected(_on_control_changed):
		_entity.control_changed.connect(_on_control_changed)

	_refresh_property_states()
	_resolve_strategy()
	reset()


func _exit_tree() -> void:
	if _peer_batcher:
		_peer_batcher.unregister(self)

	if _strategy:
		_strategy.exit(self)
		_strategy = null
	_strategy_role = DisplayRole.DISABLED
	# Leaving the tree drops the strategy, so re-entry must re-resolve the role.
	_role_dirty = true

	if _entity and _entity.control_changed.is_connected(_on_control_changed):
		_entity.control_changed.disconnect(_on_control_changed)


func _process(delta: float) -> void:
	if _peer_batcher:
		_peer_batcher.update_all(delta)

#endregion

#region ── Internal Logic ──────────────────────────────────────────────────────

func _on_control_changed(_previous_peer: int, _peer: int) -> void:
	_role_dirty = true
	_resolve_strategy(true)


func _record_tick(tick: int) -> void:
	_resolve_strategy()
	if _strategy:
		_strategy.record_tick(self, tick)


func _resolve_strategy(force: bool = false) -> void:
	# Skip the per-frame role derivation (control/authority getters) unless the
	# role was marked stale. _on_control_changed and the display_role setter set
	# _role_dirty; everything else reuses the cached strategy.
	if not force and not _role_dirty:
		return
	_role_dirty = false

	var role := _resolve_display_role()
	if not force and role == _strategy_role:
		return

	if _strategy:
		_strategy.exit(self)
		_strategy = null

	_strategy_role = role
	_apply_body_freeze(role)
	match role:
		DisplayRole.REMOTE:
			_strategy = _RemoteStrategy.new()
		DisplayRole.PREDICTED:
			_strategy = _PredictedStrategy.new()
		_:
			_strategy = null

	if _strategy:
		_strategy.enter(self)


func _resolve_display_role() -> DisplayRole:
	if display_role != DisplayRole.AUTO:
		return display_role
	if _has_prediction_component() and _is_controlled_locally():
		return DisplayRole.PREDICTED
	if owner and owner.is_multiplayer_authority():
		return DisplayRole.DISABLED
	return DisplayRole.REMOTE


func _apply_body_freeze(role: DisplayRole) -> void:
	if not (owner is RigidBody2D or owner is RigidBody3D):
		return
	# A remote dynamic body integrates under gravity regardless of network writes,
	# so its collision pose drifts between snapshots. Freeze it kinematic while it
	# is displayed remotely so network writes place it, and restore the original
	# intent the moment this peer simulates (PREDICTED) or owns (DISABLED) it.
	if role == DisplayRole.REMOTE:
		if _saved_freeze.is_empty():
			_saved_freeze = {
				&"freeze": owner.get(&"freeze"),
				&"mode": owner.get(&"freeze_mode"),
			}
		# FREEZE_MODE_KINEMATIC has the same value on RigidBody2D and RigidBody3D.
		owner.set(&"freeze_mode", RigidBody2D.FREEZE_MODE_KINEMATIC)
		owner.set(&"freeze", true)
	elif not _saved_freeze.is_empty():
		owner.set(&"freeze", _saved_freeze[&"freeze"])
		owner.set(&"freeze_mode", _saved_freeze[&"mode"])
		_saved_freeze = { }


func _has_prediction_component() -> bool:
	if _entity and _entity.prediction:
		return true
	if owner:
		return owner.get_node_or_null("%PredictionComponent") != null
	return false


func _is_controlled_locally() -> bool:
	if _entity:
		return _entity.is_controlled_locally
	return owner and owner.is_multiplayer_authority()


func _should_trace() -> bool:
	if trace_interval <= 0:
		return false
	_trace_frame = (_trace_frame + 1) % trace_interval
	return _trace_frame == 0


func _predicted_effective_smooth_time() -> float:
	if predicted_smooth_time > 0.0:
		return predicted_smooth_time
	if _clock:
		return maxf(_clock.ticktime * 0.85, 0.001)
	return 1.0 / 60.0


func _reset_display_states() -> void:
	for state in _states:
		state.reset()
		state.apply_reset()


func _update_instance(
		global_dt: int,
		global_factor: float,
		frame_ticks: float,
		smooth_weight: float,
		process_delta: float = -1.0,
) -> void:
	if not owner:
		return
	_resolve_strategy()
	if not _strategy:
		return
	_strategy.update(
		self,
		global_dt,
		global_factor,
		frame_ticks,
		smooth_weight,
		process_delta,
	)


func _refresh_property_states() -> void:
	_states.clear()
	if not owner:
		return

	var v_root := get_node_or_null(visual_root) if not visual_root.is_empty() else null

	for prop in property_modes:
		if property_modes[prop] == Mode.NONE:
			continue

		var state := _PropertyState.new()
		state.interpolator = self
		state.name = prop
		state.mode = property_modes[prop]

		var path := NodePath(str(prop)) if ":" in str(prop) else NodePath(":" + str(prop))
		var res := owner.get_node_and_resource(path)
		if not res[0]:
			continue

		state.source_obj = res[0]
		state.source_prop = res[2].get_subname(0) if res[2].get_subname_count() > 0 \
		else StringName(str(res[2]).trim_prefix(":"))

		if v_root:
			state.target_obj = v_root
			state.target_prop = visual_root_property_map.get(prop, state.source_prop)
			state.is_relative = owner.is_ancestor_of(v_root)
			state.output_mode = _resolve_visual_output_mode(state, v_root)
			if state.is_relative:
				if prop in _cached_initial_offsets:
					state.initial_offset = _cached_initial_offsets[prop]
				else:
					state.initial_offset = v_root.get(state.target_prop)
					if typeof(state.initial_offset) == TYPE_NIL:
						state.is_relative = false
						state.target_obj = state.source_obj
						state.target_prop = state.source_prop
					else:
						_cached_initial_offsets[prop] = state.initial_offset
		else:
			state.target_obj = state.source_obj
			state.target_prop = state.source_prop
			state.is_relative = false
			state.output_mode = VisualOutputMode.PROPERTY_VALUE

		var initial_val = state.source_obj.get(state.source_prop)
		state.last_written = initial_val
		state.pending_snapshot = initial_val
		state.last_recorded = initial_val
		if state.output_mode == VisualOutputMode.OWNER_TRANSFORM_COMPENSATED:
			if prop in _cached_initial_global_offsets:
				state.initial_global_offset = _cached_initial_global_offsets[prop]
			else:
				state.initial_global_offset = _calculate_initial_global_offset(
					v_root,
				)
				_cached_initial_global_offsets[prop] = \
				state.initial_global_offset
		state._has_recorded = false
		state.is_sleeping = false
		_states.append(state)

	_compute_sync_intervals()


func _resolve_visual_output_mode(
		state: _PropertyState,
		v_root: Node,
) -> VisualOutputMode:
	if visual_output_mode != VisualOutputMode.AUTO:
		return visual_output_mode
	if (
			state.is_relative
			and state.target_prop == &"position"
			and _is_owner_position_source(state)
			and _can_compensate_owner_transform(v_root)
	):
		return VisualOutputMode.OWNER_TRANSFORM_COMPENSATED
	if state.is_relative:
		return VisualOutputMode.SOURCE_DELTA
	return VisualOutputMode.PROPERTY_VALUE


func _is_owner_position_source(state: _PropertyState) -> bool:
	if state.source_obj != owner:
		return false
	if state.source_prop in [&"position", &"global_position"]:
		return true
	return state.target_prop == &"position"


func _can_compensate_owner_transform(v_root: Node) -> bool:
	return (
			owner is Node2D
			and v_root is Node2D
			and v_root.get_parent() is Node2D
	) or (
			owner is Node3D
			and v_root is Node3D
			and v_root.get_parent() is Node3D
	)


func _calculate_initial_global_offset(v_root: Node) -> Variant:
	if v_root is Node2D and owner is Node2D:
		return v_root.global_position - (owner as Node2D).global_position
	if v_root is Node3D and owner is Node3D:
		return v_root.global_position - (owner as Node3D).global_position
	return null


func _source_to_global_2d(source_prop: StringName, value: Vector2) -> Vector2:
	if source_prop == &"global_position":
		return value
	var parent := owner.get_parent()
	if parent is Node2D:
		return (parent as Node2D).to_global(value)
	return value


func _source_to_global_3d(source_prop: StringName, value: Vector3) -> Vector3:
	if source_prop == &"global_position":
		return value
	var parent := owner.get_parent()
	if parent is Node3D:
		return (parent as Node3D).to_global(value)
	return value


func _compute_sync_intervals() -> void:
	var max_interval := 0.0

	# Reset signal tracking
	for state in _states:
		state.uses_signal = false

	var all_syncs := SynchronizersCache.get_client_synchronizers(owner)
	var synced_props := SynchronizersCache.get_all_synchronized_properties(owner)

	# Opt-in authoring-tick keying: when exactly one stamped StateSynchronizer
	# drives this entity, key history by the packet's authoring __tick so a
	# shooter can name the server tick it actually displayed. Any other shape
	# falls back to the receive tick.
	_authoring_sync = null
	var stamped_count := 0
	for sync in all_syncs:
		if sync is StateSynchronizer:
			stamped_count += 1
			_authoring_sync = sync
	if stamped_count != 1:
		_authoring_sync = null

	for sync in all_syncs:
		if not sync.public_visibility:
			continue
		if sync is SaveComponent or sync is MultiplayerEntity:
			continue

		if not _sync_replicates_tracked_property(sync):
			continue

		max_interval = maxf(max_interval, maxf(sync.replication_interval, sync.delta_interval))

	# Mark which properties are covered by signals
	for state in _states:
		if state.name in synced_props:
			state.uses_signal = true

	_has_explicit_sync_interval = max_interval > 0.0
	if _clock:
		_expected_interval_ticks = maxi(1, ceili(max_interval * _clock.tickrate))


func _wire_sync_signals() -> void:
	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if not sync.public_visibility:
			continue
		if sync is SaveComponent or sync is MultiplayerEntity:
			continue
		if not _sync_replicates_tracked_property(sync):
			continue
		if not sync.synchronized.is_connected(_on_synced):
			sync.synchronized.connect(_on_synced)
		if not sync.delta_synchronized.is_connected(_on_synced):
			sync.delta_synchronized.connect(_on_synced)


func _unwire_sync_signals() -> void:
	for sync in SynchronizersCache.get_client_synchronizers(owner):
		if sync.synchronized.is_connected(_on_synced):
			sync.synchronized.disconnect(_on_synced)
		if sync.delta_synchronized.is_connected(_on_synced):
			sync.delta_synchronized.disconnect(_on_synced)


func _sync_replicates_tracked_property(sync: MultiplayerSynchronizer) -> bool:
	if not sync.replication_config:
		return false
	for path in sync.replication_config.get_properties():
		if path.get_subname_count() == 0:
			continue
		var clean_name := path.get_subname(path.get_subname_count() - 1)
		for state in _states:
			if state.name == clean_name:
				return true
	return false


func _on_synced() -> void:
	if not _strategy:
		return
	_strategy.on_synced(self)


func _record_synced_snapshot() -> void:
	var tick := _clock.tick
	if _authoring_sync and _authoring_sync.last_received_tick >= 0:
		tick = _authoring_sync.last_received_tick
	for state in _states:
		if not state.uses_signal:
			continue

		var value = state.source_obj.get(state.source_prop)

		# If using signals, we trust the signal timing over polling.
		# Always record it into history to ensure the jitter buffer stays
		# aligned with the network stream.
		if trace_interval > 0:
			_dbg.trace("Record (Signal) %s: tick=%d val=%s", [state.name, tick, value])

		state.history.record(tick, value)
		state.last_recorded = value
		state.pending_snapshot = value
		state._has_recorded = true
		state.is_sleeping = false

#endregion

#region ── Inspector & Validation ───────────────────────────────────────────────

func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	if not owner:
		return warnings

	for state in _states:
		if not state.uses_signal:
			warnings.append(
				"No client MultiplayerSynchronizer found for property '%s'. " % state.name +
				"MultiplayerInterpolator will use frame polling, which causes " +
				"one-frame snapshot delays. Add a MultiplayerSynchronizer " +
				"replicating this property.",
			)

	if owner is CharacterBody2D or owner is RigidBody2D or \
			(ClassDB.class_exists("CharacterBody3D") and owner.is_class("CharacterBody3D")) or \
			(ClassDB.class_exists("RigidBody3D") and owner.is_class("RigidBody3D")):
		var has_pos := false
		for state in _states:
			if state.name in [&"position", &"global_position"]:
				has_pos = true
				break

		if has_pos and visual_root.is_empty():
			warnings.append(
				"Parent is a physics body. Setting 'visual_root' to a child " +
				"node separates physics state from visual smoothing. The " +
				"physics body will receive snapped network positions; the " +
				"visual child will be smooth.",
			)

	if not visual_root.is_empty():
		var v_root := get_node_or_null(visual_root)
		if v_root:
			for state in _states:
				if _uses_risky_source_delta(state):
					warnings.append(
						"[code]visual_output_mode[/code] is " +
						"[code]SOURCE_DELTA[/code] for '%s'. " % state.name +
						"If the owner also moves locally, the visual child " +
						"can fight the owner transform. Use " +
						"[code]AUTO[/code] or " +
						"[code]OWNER_TRANSFORM_COMPENSATED[/code] for " +
						"server-authoritative character visuals.",
					)
				if (
						state.output_mode == \
								VisualOutputMode.OWNER_TRANSFORM_COMPENSATED
						and state.initial_global_offset == null
				):
					warnings.append(
						"Property '%s' requested owner-transform " % state.name +
						"compensation, but the source and visual target are " +
						"not compatible. MultiplayerInterpolator will fall back to " +
						"[code]SOURCE_DELTA[/code].",
					)
				var target_name := visual_root_property_map.get(
					state.name,
					state.name,
				)
				var target_val = v_root.get(target_name)
				if typeof(target_val) == TYPE_NIL:
					warnings.append(
						"Property '%s' is interpolated but the visual " % str(state.name) +
						"root '%s' has no matching property '%s'. " % [v_root.name, target_name] +
						"Set a mapping in the 'Visual Root Mappings' section.",
					)

	return warnings


func _uses_risky_source_delta(state: _PropertyState) -> bool:
	return (
			state.output_mode == VisualOutputMode.SOURCE_DELTA
			and state.target_prop == &"position"
			and _is_owner_position_source(state)
			and (
					owner is CharacterBody2D
					or owner is RigidBody2D
					or (
							ClassDB.class_exists("CharacterBody3D")
							and owner.is_class("CharacterBody3D")
					)
					or (
							ClassDB.class_exists("RigidBody3D")
							and owner.is_class("RigidBody3D")
					)
			)
	)


func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	if not Engine.is_editor_hint() or not owner:
		return props

	props.append(
		{
			"name": "Interpolated Properties",
			"type": TYPE_NIL,
			"usage": PROPERTY_USAGE_GROUP,
			"hint_string": "interpolation/",
		},
	)

	for prop_path_str in _get_tracked_properties(owner):
		props.append(
			{
				"name": "interpolation/" + prop_path_str,
				"type": TYPE_INT,
				"usage": PROPERTY_USAGE_EDITOR,
				"hint": PROPERTY_HINT_ENUM,
				"hint_string": "None,Lerp,Angle,Slerp",
			},
		)

	var v_root := get_node_or_null(visual_root) \
	if not visual_root.is_empty() else null
	if v_root:
		props.append(
			{
				"name": "Visual Root Mappings",
				"type": TYPE_NIL,
				"usage": PROPERTY_USAGE_GROUP,
				"hint_string": "mapping/",
			},
		)

		for prop_path_str in _get_tracked_properties(owner):
			if (
					not property_modes.has(prop_path_str)
					or property_modes[prop_path_str] == Mode.NONE
			):
				continue
			var source_type := _get_property_type(prop_path_str)
			var compatible := _get_compatible_target_properties(
				v_root,
				source_type,
				prop_path_str,
			)
			if compatible.is_empty():
				continue
			props.append(
				{
					"name": "mapping/" + prop_path_str,
					"type": TYPE_STRING,
					"usage": PROPERTY_USAGE_EDITOR,
					"hint": PROPERTY_HINT_ENUM,
					"hint_string": ",".join(compatible),
				},
			)

	return props


func _get(property: StringName) -> Variant:
	if property.begins_with("interpolation/"):
		return property_modes.get(StringName(property.trim_prefix("interpolation/")), Mode.NONE)
	if property.begins_with("mapping/"):
		var prop_name := StringName(property.trim_prefix("mapping/"))
		return visual_root_property_map.get(prop_name, prop_name)
	return null


func _set(property: StringName, value: Variant) -> bool:
	if property.begins_with("interpolation/"):
		var prop_name := StringName(property.trim_prefix("interpolation/"))
		if value == Mode.NONE:
			property_modes.erase(prop_name)
		else:
			property_modes[prop_name] = value as Mode
		_refresh_property_states()
		update_configuration_warnings()
		notify_property_list_changed()
		return true
	if property.begins_with("mapping/"):
		var prop_name := StringName(property.trim_prefix("mapping/"))
		if value == prop_name or str(value) == "":
			visual_root_property_map.erase(prop_name)
		else:
			visual_root_property_map[prop_name] = value
		_refresh_property_states()
		update_configuration_warnings()
		return true
	return false


func _validate_property(property: Dictionary) -> void:
	if property.name == "property_modes":
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE
	if property.name == "visual_root_property_map":
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE
	if StringName(property.name) in [&"predicted_smooth_time", &"predicted_mode"]:
		if not _has_prediction_component():
			property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE


func _get_tracked_properties(target: Node) -> Array[StringName]:
	if not target:
		return []

	var result: Array[StringName] = []
	var props := SynchronizersCache.get_all_synchronized_properties(target)
	for clean_name in props:
		var value := SynchronizersCache.resolve_value(target, props[clean_name])
		if value != null and typeof(value) in [
			TYPE_INT,
			TYPE_FLOAT,
			TYPE_VECTOR2,
			TYPE_VECTOR3,
			TYPE_COLOR,
			TYPE_QUATERNION,
		]:
			result.append(clean_name)
	return result


func _get_property_type(prop_name: StringName) -> int:
	if not owner:
		return TYPE_NIL
	var props := SynchronizersCache.get_all_synchronized_properties(owner)
	var path: NodePath = props.get(prop_name, NodePath())
	if path.is_empty():
		return TYPE_NIL
	var value := SynchronizersCache.resolve_value(owner, path)
	return typeof(value)


func _get_compatible_target_properties(
		v_root: Node,
		source_type: int,
		source_name: StringName,
) -> Array[StringName]:
	var result: Array[StringName] = [source_name]
	for prop_dict in v_root.get_property_list():
		if (
				prop_dict["type"] == source_type
				and (prop_dict["usage"] & PROPERTY_USAGE_EDITOR)
				and StringName(prop_dict["name"]) != source_name
		):
			result.append(StringName(prop_dict["name"]))
	return result

#endregion

#region ── Inner Classes ───────────────────────────────────────────────────────

@abstract
class _Strategy extends RefCounted:
	func enter(_host: MultiplayerInterpolator) -> void:
		pass


	func exit(_host: MultiplayerInterpolator) -> void:
		pass


	func record_tick(_host: MultiplayerInterpolator, _tick: int) -> void:
		pass


	func reset(host: MultiplayerInterpolator) -> void:
		host._reset_display_states()


	func snap_property(
			_host: MultiplayerInterpolator,
			_property: StringName,
			_value: Variant,
	) -> void:
		pass


	func displayed_authoring_tick(_host: MultiplayerInterpolator) -> int:
		return -1


	func get_buffer(
			_host: MultiplayerInterpolator,
			_property: StringName,
	) -> NetwRingBuffer:
		return null


	func on_synced(_host: MultiplayerInterpolator) -> void:
		pass


	@abstract func update(
			_host: MultiplayerInterpolator,
			_global_dt: int,
			_global_factor: float,
			_frame_ticks: float,
			_smooth_weight: float,
			_process_delta: float,
	) -> void


class _RemoteStrategy extends _Strategy:
	var _smoothed_floor: float = 0.0
	var _was_starving: bool = false


	func enter(host: MultiplayerInterpolator) -> void:
		host._compute_sync_intervals()
		host._wire_sync_signals()


	func exit(host: MultiplayerInterpolator) -> void:
		host._unwire_sync_signals()


	func record_tick(host: MultiplayerInterpolator, tick: int) -> void:
		for state in host._states:
			state.record_tick(tick)


	func reset(host: MultiplayerInterpolator) -> void:
		host._reset_display_states()
		var target_lag := _calculate_min_lag(host)
		_smoothed_floor = target_lag
		host.display_lag = target_lag
		_was_starving = false
		host.starvation_ticks = 0


	func snap_property(
			host: MultiplayerInterpolator,
			property: StringName,
			value: Variant,
	) -> void:
		for state in host._states:
			if state.name != property:
				continue
			state.target_obj.set(state.target_prop, value)
			state.history.clear()
			state.last_written = value
			state.pending_snapshot = value
			state.last_recorded = value
			state._has_recorded = false
			state.cached_prev_tick = -1
			return


	func displayed_authoring_tick(host: MultiplayerInterpolator) -> int:
		if not host._authoring_sync or host._display_tick < 0:
			return -1
		for state in host._states:
			var prev: int = state.history.bracketing_ticks(host._display_tick).x
			if prev >= 0:
				return prev
		return -1


	func get_buffer(
			host: MultiplayerInterpolator,
			property: StringName,
	) -> NetwRingBuffer:
		for state in host._states:
			if state.name == property:
				return state.history
		return null


	func on_synced(host: MultiplayerInterpolator) -> void:
		host._record_synced_snapshot()


	func update(
			host: MultiplayerInterpolator,
			global_dt: int,
			global_factor: float,
			frame_ticks: float,
			smooth_weight: float,
			_process_delta: float,
	) -> void:
		for state in host._states:
			state.update_snapshot()

		var should_trace := host._should_trace()

		if host.enable_smart_dilation:
			_perform_dilation(host, global_dt, frame_ticks, should_trace)
		else:
			host.display_lag = 0.0

		var time := (float(global_dt) + global_factor) - host.display_lag
		var dt := int(floor(time))
		var factor := time - float(dt)
		host._display_tick = dt

		for state in host._states:
			state.apply(
				dt,
				factor,
				host.max_lerp_distance,
				should_trace,
				host.display_lag,
				smooth_weight,
			)


	func _perform_dilation(
			host: MultiplayerInterpolator,
			global_dt: int,
			frame_ticks: float,
			trace: bool,
	) -> void:
		var raw_floor := _calculate_min_lag(host)

		# Low-pass the measured floor so jittery clock display offsets do not
		# translate one-to-one into playhead shimmer.
		_smoothed_floor += (raw_floor - _smoothed_floor) * host.floor_smoothing

		var effective_dt := int(floor(float(global_dt) - host.display_lag))
		var is_starving := false
		var newest_tick := -1

		for state in host._states:
			if state.history.is_empty() or not state.history.has_tick_after(effective_dt):
				is_starving = true
				newest_tick = state.history.newest_tick()
				break

		if is_starving:
			host.starvation_ticks += 1
			for state in host._states:
				state.is_sleeping = false
		else:
			host.starvation_ticks = 0

		if host.starvation_ticks >= host.starvation_grace_frames:
			host.display_lag = minf(
				host.display_lag + frame_ticks * host.starvation_growth,
				_smoothed_floor + host.max_extra_dilation,
			)
		else:
			host.display_lag += (_smoothed_floor - host.display_lag) \
					* host.lag_adapt_rate

		if trace:
			host._dbg.trace(
				"[Dilation] eff_dt: %d | newest: %d | starving: %s | ticks: %d | floor: %.2f | lag: %.2f",
				[
					effective_dt,
					newest_tick,
					str(is_starving),
					host.starvation_ticks,
					_smoothed_floor,
					host.display_lag,
				],
			)

		_was_starving = is_starving


	func _calculate_min_lag(host: MultiplayerInterpolator) -> float:
		if not host._clock:
			return 0.0

		var needed := float(host._expected_interval_ticks + 1)
		var network_padding := float(
			maxi(
				0,
				host._clock.recommended_display_offset - host._clock.display_offset,
			),
		)
		return maxf(
			0.0,
			needed - float(host._clock.display_offset) + network_padding,
		)


class _PredictedStrategy extends _Strategy:
	func enter(host: MultiplayerInterpolator) -> void:
		reset(host)


	func snap_property(
			host: MultiplayerInterpolator,
			property: StringName,
			value: Variant,
	) -> void:
		for state in host._states:
			if state.name == property:
				state.apply_live(value, 1.0, 0.0, false)
				return


	func record_tick(host: MultiplayerInterpolator, tick: int) -> void:
		if host.predicted_mode != PredictedMode.BRACKETED:
			return
		for state in host._states:
			var value = state.source_obj.get(state.source_prop)
			state.history.record(tick, value)
			state.last_recorded = value
			state.pending_snapshot = value
			state._has_recorded = true
			state.is_sleeping = false


	func update(
			host: MultiplayerInterpolator,
			global_dt: int,
			global_factor: float,
			_frame_ticks: float,
			_smooth_weight: float,
			process_delta: float,
	) -> void:
		var trace := host._should_trace()
		match host.predicted_mode:
			PredictedMode.BRACKETED:
				_update_bracketed(host, global_dt, global_factor, trace)
			_:
				_update_chase(host, trace, process_delta)


	func _update_chase(
			host: MultiplayerInterpolator,
			trace: bool,
			process_delta: float,
	) -> void:
		var smooth_time := host._predicted_effective_smooth_time()
		var weight := 1.0
		if smooth_time > 0.0:
			var delta := process_delta
			if delta < 0.0:
				delta = host.get_process_delta_time()
			weight = 1.0 - exp(-delta / smooth_time)
		for state in host._states:
			var value = state.source_obj.get(state.source_prop)
			state.apply_live(value, weight, host.max_lerp_distance, trace)


	func _update_bracketed(
			host: MultiplayerInterpolator,
			global_dt: int,
			global_factor: float,
			trace: bool,
	) -> void:
		var dt := maxi(0, global_dt - 1)
		host._display_tick = dt
		for state in host._states:
			state.apply(dt, global_factor, host.max_lerp_distance, trace, 0.0, 1.0)


class _Batcher extends RefCounted:
	var instances: Array[MultiplayerInterpolator] = []
	var clock: MultiplayerClock
	var _last_update_frame: int = -1


	func register(inst: MultiplayerInterpolator, c: MultiplayerClock) -> void:
		instances.append(inst)
		if not clock:
			clock = c
			clock.after_tick.connect(_on_clock_tick)


	func unregister(inst: MultiplayerInterpolator) -> void:
		instances.erase(inst)
		if instances.is_empty():
			shutdown()


	func shutdown() -> void:
		if clock and clock.after_tick.is_connected(_on_clock_tick):
			clock.after_tick.disconnect(_on_clock_tick)
		clock = null
		instances.clear()


	func _on_clock_tick(_delta: float, tick: int) -> void:
		for inst in instances:
			inst._record_tick(tick)


	func update_all(delta: float) -> void:
		var frame := Engine.get_process_frames()
		if delta > 0.0 and frame == _last_update_frame:
			return
		_last_update_frame = frame

		if not clock:
			return

		var global_dt := clock.display_tick
		var global_factor := clock.tick_factor
		var frame_ticks := delta * clock.tickrate

		for inst in instances:
			var weight := 1.0 - exp(-delta / inst.smoothing_time) if inst.smoothing_time > 0.0 else 1.0
			inst._update_instance(global_dt, global_factor, frame_ticks, weight, delta)


class _PropertyState:
	var interpolator: MultiplayerInterpolator
	var name: StringName
	var mode: Mode
	var history := NetwRingBuffer.new(16)

	var source_obj: Object
	var source_prop: StringName
	var target_obj: Object
	var target_prop: StringName

	var uses_signal: bool = false
	var is_relative: bool = false
	var output_mode: VisualOutputMode = VisualOutputMode.PROPERTY_VALUE
	var initial_offset: Variant
	var initial_global_offset: Variant

	var last_written: Variant
	var pending_snapshot: Variant
	var last_recorded: Variant

	var cached_prev_tick: int = -1
	var is_sleeping: bool = false
	var _has_recorded: bool = false
	var _search_results := PackedInt32Array([-1, -1])


	func reset() -> void:
		if not source_obj:
			return
		var current = source_obj.get(source_prop)
		history.clear()
		last_written = current
		pending_snapshot = current
		last_recorded = current
		_has_recorded = false
		cached_prev_tick = -1
		is_sleeping = false

		if is_relative and target_obj and interpolator \
				and name in interpolator._cached_initial_offsets:
			initial_offset = interpolator._cached_initial_offsets[name]


	func apply_reset() -> void:
		if not target_obj or not source_obj:
			return
		var current = source_obj.get(source_prop)
		match output_mode:
			VisualOutputMode.OWNER_TRANSFORM_COMPENSATED:
				if not _apply_reset_owner_transform(current):
					_apply_reset_source_delta()
			VisualOutputMode.SOURCE_DELTA:
				_apply_reset_source_delta()
			_:
				target_obj.set(target_prop, current)
		last_written = current


	func _apply_reset_source_delta() -> void:
		if initial_offset == null:
			return
		target_obj.set(target_prop, initial_offset)


	func _apply_reset_owner_transform(current: Variant) -> bool:
		if target_obj is Node2D and interpolator.owner is Node2D:
			return _apply_owner_transform_compensated_2d(current)
		if target_obj is Node3D and interpolator.owner is Node3D:
			return _apply_owner_transform_compensated_3d(current)
		return false


	func update_snapshot() -> void:
		var current_val = source_obj.get(source_prop)
		if target_obj != source_obj or not _is_close(current_val, last_written):
			if not _has_recorded:
				last_written = current_val
			pending_snapshot = current_val
			is_sleeping = false


	func record_tick(tick: int) -> void:
		if _has_recorded and pending_snapshot == last_recorded:
			return

		if interpolator.trace_interval > 0:
			interpolator._dbg.trace("Record %s: tick=%d val=%s", [name, tick, pending_snapshot])

		history.record(tick, pending_snapshot)
		if not _has_recorded:
			last_written = pending_snapshot
		last_recorded = pending_snapshot
		_has_recorded = true
		is_sleeping = false


	func apply(
			dt: int,
			factor: float,
			snap_dist: float,
			trace: bool,
			lag: float,
			weight: float,
	) -> void:
		if is_sleeping:
			return

		var result := _resolve_value(dt, factor, snap_dist)
		var snapped := snap_dist > 0.0 and _snap(last_written, result, snap_dist)

		# Apply additional smoothing if weight < 1.0
		if weight < 1.0 and not snapped:
			result = _interpolate(last_written, result, weight)

		if trace:
			interpolator._dbg.trace(
				"Interp %s: dt=%d lag=%.2f val=%s",
				[name, dt, lag, result],
			)

		match output_mode:
			VisualOutputMode.OWNER_TRANSFORM_COMPENSATED:
				if not _apply_owner_transform_compensated(result):
					_apply_source_delta(result)
			VisualOutputMode.SOURCE_DELTA:
				_apply_source_delta(result)
			_:
				target_obj.set(target_prop, result)

		last_written = result


	func apply_live(
			target: Variant,
			weight: float,
			snap_dist: float,
			trace: bool,
	) -> void:
		if not target_obj or not source_obj:
			return

		var result = target
		var snapped := snap_dist > 0.0 and _snap(last_written, result, snap_dist)
		if weight < 1.0 and not snapped:
			result = _interpolate(last_written, result, weight)

		if trace:
			interpolator._dbg.trace(
				"PredictDisplay %s: target=%s val=%s",
				[name, target, result],
			)

		_has_recorded = true
		match output_mode:
			VisualOutputMode.OWNER_TRANSFORM_COMPENSATED:
				if not _apply_owner_transform_compensated(result):
					_apply_source_delta(result)
			VisualOutputMode.SOURCE_DELTA:
				_apply_source_delta(result)
			_:
				target_obj.set(target_prop, result)

		last_written = result


	func _apply_source_delta(result: Variant) -> void:
		var current_raw = source_obj.get(source_prop)
		var type = typeof(result)
		if (
				type in [TYPE_VECTOR2, TYPE_VECTOR3, TYPE_FLOAT, TYPE_INT]
				and not initial_offset == null
				and typeof(initial_offset) == type
		):
			target_obj.set(target_prop, initial_offset + (result - current_raw))
		else:
			target_obj.set(target_prop, result)


	func _apply_owner_transform_compensated(result: Variant) -> bool:
		if not _has_recorded:
			return true
		if target_prop != &"position":
			return false
		if target_obj is Node2D and interpolator.owner is Node2D:
			return _apply_owner_transform_compensated_2d(result)
		if target_obj is Node3D and interpolator.owner is Node3D:
			return _apply_owner_transform_compensated_3d(result)
		return false


	func _apply_owner_transform_compensated_2d(result: Variant) -> bool:
		if typeof(result) != TYPE_VECTOR2:
			return false
		var visual := target_obj as Node2D
		var visual_parent := visual.get_parent() as Node2D
		if not visual_parent or typeof(initial_global_offset) != TYPE_VECTOR2:
			return false
		var desired_global := interpolator._source_to_global_2d(
			source_prop,
			result,
		)
		visual.global_position = desired_global + initial_global_offset
		return true


	func _apply_owner_transform_compensated_3d(result: Variant) -> bool:
		if typeof(result) != TYPE_VECTOR3:
			return false
		var visual := target_obj as Node3D
		var visual_parent := visual.get_parent() as Node3D
		if not visual_parent or typeof(initial_global_offset) != TYPE_VECTOR3:
			return false
		var desired_global := interpolator._source_to_global_3d(
			source_prop,
			result,
		)
		visual.global_position = desired_global + initial_global_offset
		return true


	func _resolve_value(dt: int, factor: float, snap_dist: float) -> Variant:
		history.find_bracketing_ticks(dt, cached_prev_tick, _search_results)
		var prev_tick := _search_results[0]
		var next_tick := _search_results[1]

		cached_prev_tick = prev_tick

		if prev_tick == -1:
			return last_written

		if next_tick == -1:
			var result = history.get_at(prev_tick)

			if pending_snapshot == last_recorded:
				# Only sleep if we've reached the target visually AND it's the 
				# absolute newest snapshot we have.
				var at_rest := _is_close(last_written, result)
				if not history.has_tick_after(prev_tick) and at_rest:
					is_sleeping = true
			return result

		var p_val = history.get_at(prev_tick)
		var n_val = history.get_at(next_tick)

		if snap_dist > 0.0 and _snap(p_val, n_val, snap_dist):
			return n_val

		return _lerp_bracketed(p_val, n_val, prev_tick, next_tick, dt, factor)


	func _lerp_bracketed(
			p_val: Variant,
			n_val: Variant,
			p_tick: int,
			n_tick: int,
			dt: int,
			factor: float,
	) -> Variant:
		var gap := n_tick - p_tick
		var threshold := interpolator._expected_interval_ticks * 2

		# Stationary period protection: if the gap is huge, stay at P0 
		# until we are close to P1.
		if gap > threshold:
			var start_lerp_tick := n_tick - interpolator._expected_interval_ticks
			if dt < start_lerp_tick:
				return p_val

			# Lerp over exactly one expected interval
			var t := clampf(
				(float(dt - start_lerp_tick) + factor) / \
						float(interpolator._expected_interval_ticks),
				0.0,
				1.0,
			)
			return _interpolate(p_val, n_val, t)

		# Standard interpolation
		var t := clampf((float(dt - p_tick) + factor) / float(gap), 0.0, 1.0)
		return _interpolate(p_val, n_val, t)


	func _interpolate(a: Variant, b: Variant, t: float) -> Variant:
		if mode == Mode.ANGLE:
			return lerp_angle(a, b, t)
		if mode == Mode.SLERP:
			return (a as Quaternion).slerp(b, t)
		return lerp(a, b, t)


	func _snap(v1: Variant, v2: Variant, dist: float) -> bool:
		if typeof(v1) != typeof(v2):
			return true
		match typeof(v1):
			TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
				return v1.distance_to(v2) > dist
			TYPE_QUATERNION:
				return absf(v1.angle_to(v2)) > dist
			TYPE_FLOAT, TYPE_INT:
				var diff := abs(angle_difference(v1, v2)) if mode == Mode.ANGLE else abs(v1 - v2)
				return diff > dist
		return false


	func _is_close(v1: Variant, v2: Variant) -> bool:
		if typeof(v1) != typeof(v2):
			return false
		match typeof(v1):
			TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
				return v1.is_equal_approx(v2)
			TYPE_QUATERNION:
				return absf(v1.angle_to(v2)) < 0.001
			TYPE_FLOAT, TYPE_INT:
				var diff := abs(angle_difference(v1, v2)) if mode == Mode.ANGLE else abs(v1 - v2)
				return diff < 0.001
		return true

#endregion
