## Smoothly interpolates a node's properties between network snapshots.
@tool
class_name TickInterpolator
extends NetComponent


#region ── Enums ───────────────────────────────────────────────────────────────

## Defines the interpolation algorithm for a property.
enum Mode {
	NONE = 0, ## No interpolation.
	LERP = 1, ## Linear interpolation.
	ANGLE = 2, ## Angular interpolation (shortest path).
}

#endregion


#region ── Configuration ───────────────────────────────────────────────────────

## Dictionary mapping property names to their [enum TickInterpolator.Mode].
@export var property_modes: Dictionary[StringName, Mode] = {}:
	set(v):
		property_modes = v
		_refresh_property_states()
		update_configuration_warnings()
		notify_property_list_changed()


## If [code]true[/code], the interpolator will locally slow down its playhead
## when snapshots are missing to prevent visual jitter.
@export var enable_smart_dilation: bool = true


## Controls the "softness" or "floatiness" of the interpolation.
## [br][br]
## [br]- [code]0.0[/code]: Crisp and instant (pure time-based interpolation).
## [br]- [code]>0.0[/code]: Adds exponential smoothing, making motion feel
## heavier/fluid but adding visual lag.
@export_range(0.0, 0.99) var smoothing: float = 0.0


## Maximum distance allowed before the interpolator snaps to the target instead
## of lerping.
## [br][br]
## Useful for teleports. Set to [code]0.0[/code] to disable.
@export var max_lerp_distance: float = 0.0


## The maximum number of extra ticks the interpolator can dilate beyond its
## floor.
@export var max_extra_dilation: float = 4.0


## If greater than [code]0[/code], the interpolator will log its internal state
## every N frames.
@export var trace_interval: int = 30

#endregion


#region ── Public Metrics ──────────────────────────────────────────────────────

## The current extra delay (in ticks) added by Smart Dilation.
var display_lag: float = 0.0

## How many consecutive frames the interpolator has been starving for snapshots.
var starvation_ticks: int = 0

#endregion


#region ── Public API ──────────────────────────────────────────────────────────

## Instantly snaps [param property] to [param value], bypassing interpolation.
## [br][br]
## [codeblock]
## # Snap a base property
## interpolator.snap_property(&"position", Vector2(200, 100))
## 
## # Snap a nested child property (must match the exact key in property_modes)
## interpolator.snap_property(&"Sprite2D:position", Vector2.ZERO)
## [/codeblock]
func snap_property(property: StringName, value: Variant) -> void:
	for state in _states:
		if state.name == property:
			state.target_obj.set(state.target_prop, value)
			state.history.clear()
			state.last_written = value
			state.pending_snapshot = value
			state.last_recorded = value
			state._has_recorded = false
			state.cached_prev_tick = -1
			return


## Instantly accepts the target node's current physical state as the absolute
## truth.
## [br][br]
## [codeblock]
## # 1. Update the node's properties manually
## player.position = spawn_point
## player.rotation = spawn_rotation
## 
## # 2. Tell the interpolator to anchor to this new reality
## interpolator.teleport()
## [/codeblock]
func teleport() -> void:
	display_lag = 0.0
	starvation_ticks = 0
	
	for state in _states:
		state.history.clear()
		var current = state.target_obj.get(state.target_prop)
		state.last_written = current
		state.pending_snapshot = current
		state.last_recorded = current
		state._has_recorded = false
		state.cached_prev_tick = -1
		state.is_sleeping = false


## Clears all history buffers and resets internal state to match the target's
## current values.
func reset() -> void:
	display_lag = 0.0
	starvation_ticks = 0
	for state in _states:
		state.reset()


## Returns the [HistoryBuffer] for the given [param property], or [code]null[/code]
## if not found.
func get_buffer(property: StringName) -> HistoryBuffer:
	for state in _states:
		if state.name == property:
			return state.history
	return null


## Temporarily disables interpolation for [param duration] seconds.
## [br][br]
## Returns a [SceneTreeTimer] that can be awaited.
func disable_for(duration: float) -> SceneTreeTimer:
	process_mode = PROCESS_MODE_DISABLED
	reset()
	var timer := get_tree().create_timer(duration)
	timer.timeout.connect(func():
		if is_instance_valid(self) and not _target.is_multiplayer_authority():
			process_mode = PROCESS_MODE_INHERIT
	)
	return timer

#endregion


#region ── Internal State ──────────────────────────────────────────────────────

const _CATCHUP_SPEED := 1.1
const _DILATION_STRENGTH := 0.95
const _STARVATION_GRACE_FRAMES := 3

var _target: Node
var _clock: NetworkClock
var _states: Array[_PropertyState] = []
var _trace_frame: int = 0

var _expected_interval_ticks: int = 1
var _has_explicit_sync_interval: bool = false

var _peer_batcher: _Batcher
var _was_starving: bool = false
var _dbg: NetwHandle = Netw.dbg.handle(self)

#endregion


#region ── Lifecycle ───────────────────────────────────────────────────────────

func _ready() -> void:
	if Engine.is_editor_hint(): return

	_target = get_parent()
	_clock = get_network_clock()

	assert(_target, "TickInterpolator: Parent node is missing.")
	assert(_clock, "TickInterpolator: Requires a NetworkClock on the multiplayer API.")

	if _target.is_multiplayer_authority():
		process_mode = PROCESS_MODE_DISABLED
		return

	_peer_batcher = get_bucket(_Batcher) as _Batcher
	if _peer_batcher:
		_peer_batcher.register(self, _clock)
	
	_refresh_property_states()
	_cache_sync_intervals()
	
	display_lag = _calculate_min_lag()


func _exit_tree() -> void:
	if _peer_batcher:
		_peer_batcher.unregister(self)


func _process(delta: float) -> void:
	if _peer_batcher:
		_peer_batcher.update_all(delta)

#endregion


#region ── Internal Logic ──────────────────────────────────────────────────────

func _update_instance(global_dt: int, global_factor: float, frame_ticks: float, smooth_weight: float) -> void:
	if _target.is_multiplayer_authority():
		return
	
	for state in _states:
		state.update_snapshot()
	
	var should_trace := false
	if trace_interval > 0:
		_trace_frame = (_trace_frame + 1) % trace_interval
		should_trace = _trace_frame == 0

	if enable_smart_dilation:
		_perform_dilation(global_dt, frame_ticks, should_trace)
	else:
		display_lag = 0.0

	var effective_time := (float(global_dt) + global_factor) - display_lag
	var dt := int(floor(effective_time))
	var factor := effective_time - float(dt)

	for state in _states:
		state.apply(dt, factor, max_lerp_distance, should_trace, display_lag, smooth_weight)


func _perform_dilation(global_dt: int, frame_ticks: float, trace: bool) -> void:
	var effective_dt := int(floor(float(global_dt) - display_lag))
	var is_starving := false
	var debug_newest := -1
	var debug_window := 0

	for state in _states:
		if not state.history.has_tick_after(effective_dt):
			var newest := state.history.newest_tick()
			debug_newest = newest
			
			var max_starvation_window := \
				_expected_interval_ticks + _STARVATION_GRACE_FRAMES
			debug_window = max_starvation_window
			if newest != -1 and (effective_dt - newest) <= max_starvation_window:
				is_starving = true
				break
	
	if trace:
		_dbg.trace(
			"[Dilation] eff_dt: %d | newest: %d | gap: %d | " + \
			"window: %d | starving: %s | ticks: %d | lag: %.2f" % [
				effective_dt, debug_newest, (effective_dt - debug_newest),
				debug_window, str(is_starving), starvation_ticks, display_lag
			]
		)
	
	var current_floor := _calculate_min_lag()
	if is_starving:
		starvation_ticks += 1
		if starvation_ticks >= _STARVATION_GRACE_FRAMES:
			display_lag = minf(
				display_lag + (frame_ticks * _DILATION_STRENGTH),
				current_floor + max_extra_dilation
			)
	else:
		starvation_ticks = 0
		display_lag = maxf(
			current_floor,
			display_lag - (frame_ticks * (_CATCHUP_SPEED - 1.0))
		)

	_was_starving = is_starving


func _refresh_property_states() -> void:
	_states.clear()
	if not _target:
		return
	
	for prop in property_modes:
		if property_modes[prop] == Mode.NONE:
			continue
		
		var state := _PropertyState.new()
		state.interpolator = self
		state.name = prop
		state.mode = property_modes[prop]
		
		var path_str := str(prop)
		var path: NodePath = NodePath(path_str) if ":" in path_str else \
			NodePath(":" + path_str)
		var res := _target.get_node_and_resource(path)
		
		if res[0]:
			state.target_obj = res[0]
			state.target_prop = res[2].get_subname(0) if \
				res[2].get_subname_count() > 0 else \
				StringName(str(res[2]).trim_prefix(":"))
			
			var initial_val = state.target_obj.get(state.target_prop)
			state.last_written = initial_val
			state.pending_snapshot = initial_val
			state.last_recorded = initial_val
			state._has_recorded = false
			_states.append(state)


func _calculate_min_lag() -> float:
	var needed := float(_expected_interval_ticks + 1)
	var network_padding := float(maxi(0, _clock.recommended_display_offset - _clock.display_offset))
	return maxf(0.0, needed - float(_clock.display_offset) + network_padding)


func _cache_sync_intervals() -> void:
	var max_interval := 0.0
	for sync in SynchronizersCache.get_client_synchronizers(_target):
		max_interval = maxf(max_interval, maxf(sync.replication_interval, sync.delta_interval))
	_has_explicit_sync_interval = max_interval > 0.0
	_expected_interval_ticks = maxi(1, ceili(max_interval * _clock.tickrate))

#endregion


#region ── Inspector & Validation ───────────────────────────────────────────────

func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	var target = get_parent()
	if not Engine.is_editor_hint() or not target: return props

	props.append({"name": "Interpolated Properties", "type": TYPE_NIL, "usage": PROPERTY_USAGE_GROUP, "hint_string": "interpolation/"})

	for prop_path_str in _get_tracked_properties(target):
		props.append({
			"name": "interpolation/" + prop_path_str,
			"type": TYPE_INT,
			"usage": PROPERTY_USAGE_EDITOR,
			"hint": PROPERTY_HINT_ENUM,
			"hint_string": "None,Lerp,Angle"
		})
	return props


func _get(property: StringName) -> Variant:
	if property.begins_with("interpolation/"):
		return property_modes.get(StringName(property.trim_prefix("interpolation/")), Mode.NONE)
	return null


func _set(property: StringName, value: Variant) -> bool:
	if property.begins_with("interpolation/"):
		var prop_name := StringName(property.trim_prefix("interpolation/"))
		if value == Mode.NONE: property_modes.erase(prop_name)
		else: property_modes[prop_name] = value as Mode
		_refresh_property_states()
		update_configuration_warnings()
		notify_property_list_changed()
		return true
	return false


func _validate_property(property: Dictionary) -> void:
	if property.name == "property_modes": property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE


func _get_tracked_properties(target: Node) -> Array[StringName]:
	var result: Array[StringName] = []
	var props := SynchronizersCache.get_all_synchronized_properties(target)
	for clean_name in props:
		var value := SynchronizersCache.resolve_value(target, props[clean_name])
		if value != null and typeof(value) in [TYPE_INT, TYPE_FLOAT, TYPE_VECTOR2, TYPE_VECTOR3, TYPE_COLOR, TYPE_QUATERNION]:
			result.append(clean_name)
	return result

#endregion


#region ── Inner Classes ───────────────────────────────────────────────────────

class _Batcher extends RefCounted:
	var instances: Array[TickInterpolator] = []
	var clock: NetworkClock
	var _last_update_frame: int = -1

	func register(inst: TickInterpolator, c: NetworkClock) -> void:
		instances.append(inst)
		if not clock:
			clock = c
			clock.after_tick.connect(_on_clock_tick)

	func unregister(inst: TickInterpolator) -> void:
		instances.erase(inst)

	func _on_clock_tick(_delta: float, tick: int) -> void:
		for inst in instances:
			for state in inst._states:
				state.record_tick(tick)

	func update_all(delta: float) -> void:
		var frame := Engine.get_frames_drawn()
		if delta > 0.0 and frame == _last_update_frame:
			return
		_last_update_frame = frame
		
		if not clock:
			return
		
		var global_dt := clock.display_tick
		var global_factor := clock.tick_factor
		var frame_ticks := delta * clock.tickrate

		for inst in instances:
			var smooth_weight := 1.0
			if inst.smoothing > 0.0:
				smooth_weight = 1.0 - pow(inst.smoothing, delta * 60.0)
				
			inst._update_instance(
				global_dt,
				global_factor,
				frame_ticks,
				smooth_weight
			)


class _PropertyState:
	var interpolator: TickInterpolator
	var name: StringName
	var mode: Mode
	var history := HistoryBuffer.new(16)
	var target_obj: Object
	var target_prop: StringName
	
	var last_written: Variant
	var pending_snapshot: Variant
	var last_recorded: Variant
	
	var cached_prev_tick: int = -1
	var is_sleeping: bool = false
	var _has_recorded: bool = false
	var _search_results := PackedInt32Array([-1, -1])

	func reset() -> void:
		var current = target_obj.get(target_prop)
		last_written = current
		pending_snapshot = current
		last_recorded = current
		_has_recorded = false
		cached_prev_tick = -1
		is_sleeping = false

	func update_snapshot() -> void:
		var current_val = target_obj.get(target_prop)
		if current_val != last_written:
			pending_snapshot = current_val
			is_sleeping = false

	func record_tick(tick: int) -> void:
		if not _has_recorded or pending_snapshot != last_recorded:
			history.record(tick, pending_snapshot)
			last_recorded = pending_snapshot
			_has_recorded = true
			is_sleeping = false

	func apply(
		dt: int,
		factor: float,
		snap_dist: float,
		trace: bool,
		lag: float,
		weight: float
	) -> void:
		if is_sleeping:
			return
		
		history.find_bracketing_ticks(dt, cached_prev_tick, _search_results)
		var prev_tick := _search_results[0]
		var next_tick := _search_results[1]
		
		cached_prev_tick = prev_tick

		if prev_tick == -1:
			return

		var result: Variant
		if next_tick == -1:
			result = history.get_at(prev_tick)
			if pending_snapshot == last_recorded:
				# Only sleep if we've reached the target visually
				if weight >= 1.0 or _is_close(last_written, result):
					is_sleeping = true
		else:
			var p_val = history.get_at(prev_tick)
			var n_val = history.get_at(next_tick)

			if snap_dist > 0.0 and _should_snap(p_val, n_val, snap_dist):
				result = n_val
			else:
				var t := clampf(
					(float(dt - prev_tick) + factor) / \
					float(next_tick - prev_tick),
					0.0,
					1.0
				)
				if mode == Mode.ANGLE:
					result = lerp_angle(p_val, n_val, t)
				else:
					result = lerp(p_val, n_val, t)

		# Apply additional smoothing if weight < 1.0
		if weight < 1.0:
			if mode == Mode.ANGLE:
				result = lerp_angle(last_written, result, weight)
			else:
				result = lerp(last_written, result, weight)

		target_obj.set(target_prop, result)
		last_written = result
		
		if trace:
			interpolator._dbg.trace(
				"Interp %s: dt=%d lag=%.2f val=%s",
				[name, dt, lag, result]
			)

	func _should_snap(v1: Variant, v2: Variant, dist: float) -> bool:
		if typeof(v1) != typeof(v2):
			return true
		match typeof(v1):
			TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
				return v1.distance_to(v2) > dist
			TYPE_FLOAT, TYPE_INT:
				if mode == Mode.ANGLE:
					return abs(angle_difference(v1, v2)) > dist
				return abs(v1 - v2) > dist
		return false

	func _is_close(v1: Variant, v2: Variant) -> bool:
		if typeof(v1) != typeof(v2):
			return false
		match typeof(v1):
			TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
				return v1.is_equal_approx(v2)
			TYPE_FLOAT, TYPE_INT:
				if mode == Mode.ANGLE:
					return abs(angle_difference(v1, v2)) < 0.001
				return is_equal_approx(v1, v2)
		return true

#endregion
