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


## If set, smooth output will be written to this child node instead of the
## parent node.
## [br][br]
## This is recommended for physics bodies ([code]CharacterBody2D[/code], etc.)
## to prevent the interpolator from fighting with the physics engine.
@export var visual_root: NodePath = NodePath(""):
	set(v):
		visual_root = v
		_refresh_property_states()
		update_configuration_warnings()


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
@export var trace_interval: int = 0

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
## # Update the node's properties manually
## player.position = spawn_point
## player.rotation = spawn_rotation
## 
## # Tell the interpolator to anchor to this new reality
## interpolator.teleport()
## [/codeblock]
func teleport() -> void:
	reset()


## Clears all recorded history and resets the visual state to match 
## the current raw positions. 
##
## Call this after manual teleports or significant state changes 
## to prevent the interpolator from "sliding" the node across the map.
func reset() -> void:
	if not _clock: _clock = get_network_clock()
	
	for state in _states:
		state.reset()
	display_lag = _calculate_min_lag()
	_was_starving = false
	starvation_ticks = 0


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
		if is_instance_valid(self) and owner and not owner.is_multiplayer_authority():
			process_mode = PROCESS_MODE_INHERIT
	)
	return timer

#endregion


#region ── Internal State ──────────────────────────────────────────────────────

const _CATCHUP_SPEED := 1.1
const _DILATION_STRENGTH := 0.95
const _STARVATION_GRACE_FRAMES := 3

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
	
	process_priority = 100
	_clock = get_network_clock()

	assert(owner, "TickInterpolator: owner is missing.")
	assert(_clock, "TickInterpolator: Requires a NetworkClock on the multiplayer API.")

	if owner.is_multiplayer_authority():
		process_mode = PROCESS_MODE_DISABLED
		return

	_peer_batcher = get_bucket(_Batcher) as _Batcher
	if _peer_batcher:
		_peer_batcher.register(self, _clock)
	
	_refresh_property_states()
	reset()


func _exit_tree() -> void:
	if _peer_batcher:
		_peer_batcher.unregister(self)


func _process(delta: float) -> void:
	if _peer_batcher:
		_peer_batcher.update_all(delta)

#endregion


#region ── Internal Logic ──────────────────────────────────────────────────────

func _update_instance(
	global_dt: int, 
	global_factor: float, 
	frame_ticks: float, 
	smooth_weight: float
) -> void:
	if not owner or owner.is_multiplayer_authority():
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

	var time := (float(global_dt) + global_factor) - display_lag
	var dt := int(floor(time))
	var factor := time - float(dt)

	for state in _states:
		state.apply(dt, factor, max_lerp_distance, should_trace, display_lag, smooth_weight)


func _perform_dilation(global_dt: int, frame_ticks: float, trace: bool) -> void:
	var current_floor := _calculate_min_lag()
	var effective_dt := int(floor(float(global_dt) - display_lag))
	var is_starving := false
	var newest_tick := -1

	for state in _states:
		if not state.history.has_tick_after(effective_dt):
			is_starving = true
			newest_tick = state.history.newest_tick()
			break
	
	if is_starving:
		starvation_ticks += 1
		for state in _states:
			state.is_sleeping = false
			
		if starvation_ticks >= _STARVATION_GRACE_FRAMES:
			display_lag = minf(
				display_lag + (frame_ticks * _DILATION_STRENGTH),
				current_floor + max_extra_dilation
			)
	else:
		starvation_ticks = 0
		display_lag = maxf(current_floor, display_lag - (frame_ticks * (_CATCHUP_SPEED - 1.0)))

	if trace:
		_dbg.trace(
			"[Dilation] eff_dt: %d | newest: %d | starving: %s | ticks: %d | lag: %.2f", 
			[effective_dt, newest_tick, str(is_starving), starvation_ticks, display_lag]
		)

	_was_starving = is_starving


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
			state.target_prop = state.source_prop
			state.is_relative = owner.is_ancestor_of(v_root)
			if state.is_relative:
				state.initial_offset = v_root.get(state.target_prop)
		else:
			state.target_obj = state.source_obj
			state.target_prop = state.source_prop
			state.is_relative = false
			
		var initial_val = state.source_obj.get(state.source_prop)
		state.last_written = initial_val
		state.pending_snapshot = initial_val
		state.last_recorded = initial_val
		state._has_recorded = false
		state.is_sleeping = false
		_states.append(state)
	
	_cache_sync_intervals()


func _calculate_min_lag() -> float:
	if not _clock:
		return 0.0
		
	var needed := float(_expected_interval_ticks + 1)
	var network_padding := float(maxi(0, _clock.recommended_display_offset - _clock.display_offset))
	return maxf(0.0, needed - float(_clock.display_offset) + network_padding)


func _cache_sync_intervals() -> void:
	var max_interval := 0.0
	for sync in SynchronizersCache.get_client_synchronizers(owner):
		max_interval = maxf(max_interval, maxf(sync.replication_interval, sync.delta_interval))
		
		# Connect signals for immediate snapshot injection
		if not sync.delta_synchronized.is_connected(_on_synced):
			sync.delta_synchronized.connect(_on_synced)
		if not sync.synchronized.is_connected(_on_synced):
			sync.synchronized.connect(_on_synced)
		
		# Mark which properties are covered by signals
		var synced_props := SynchronizersCache.get_all_synchronized_properties(owner)
		for state in _states:
			if state.name in synced_props:
				state.uses_signal = true
				
	_has_explicit_sync_interval = max_interval > 0.0
	_expected_interval_ticks = maxi(1, ceili(max_interval * _clock.tickrate))


func _on_synced() -> void:
	var tick := _clock.tick
	for state in _states:
		if not state.uses_signal:
			continue
			
		var value = state.source_obj.get(state.source_prop)
		if not state._has_recorded or value != state.last_recorded:
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
				"No client MultiplayerSynchronizer found for property '%s'. " % state.name + \
				"TickInterpolator will use frame polling, which causes " + \
				"one-frame snapshot delays. Add a MultiplayerSynchronizer " + \
				"replicating this property."
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
				"Parent is a physics body. Setting 'visual_root' to a child " + \
				"node separates physics state from visual smoothing. The " + \
				"physics body will receive snapped network positions; the " + \
				"visual child will be smooth."
			)

	return warnings


func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	if not Engine.is_editor_hint() or not owner: 
		return props

	props.append({"name": "Interpolated Properties", "type": TYPE_NIL, "usage": PROPERTY_USAGE_GROUP, "hint_string": "interpolation/"})

	for prop_path_str in _get_tracked_properties(owner):
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
	if not target: 
		return []
	
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
			var weight := 1.0 - pow(inst.smoothing, delta * 60.0) if inst.smoothing > 0.0 else 1.0
			inst._update_instance(global_dt, global_factor, frame_ticks, weight)


class _PropertyState:
	var interpolator: TickInterpolator
	var name: StringName
	var mode: Mode
	var history := HistoryBuffer.new(16)
	
	var source_obj: Object
	var source_prop: StringName
	var target_obj: Object
	var target_prop: StringName
	
	var uses_signal: bool = false
	var is_relative: bool = false
	var initial_offset: Variant
	
	var last_written: Variant
	var pending_snapshot: Variant
	var last_recorded: Variant
	
	var cached_prev_tick: int = -1
	var is_sleeping: bool = false
	var _has_recorded: bool = false
	var _search_results := PackedInt32Array([-1, -1])

	func reset() -> void:
		if not source_obj: return
		var current = source_obj.get(source_prop)
		history.clear()
		last_written = current
		pending_snapshot = current
		last_recorded = current
		_has_recorded = false
		cached_prev_tick = -1
		is_sleeping = false
		
		if is_relative and target_obj:
			initial_offset = target_obj.get(target_prop)

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
		weight: float
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
				[name, dt, lag, result]
			)

		if is_relative:
			var current_raw = source_obj.get(source_prop)
			var type = typeof(result)
			# Apply the smooth-raw difference as an offset to the initial local state
			if type in [TYPE_VECTOR2, TYPE_VECTOR3, TYPE_FLOAT, TYPE_INT]:
				target_obj.set(target_prop, initial_offset + (result - current_raw))
			else:
				target_obj.set(target_prop, result)
		else:
			target_obj.set(target_prop, result)
			
		last_written = result


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
		factor: float
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
				1.0
			)
			return _interpolate(p_val, n_val, t)
		
		# Standard interpolation
		var t := clampf((float(dt - p_tick) + factor) / float(gap), 0.0, 1.0)
		return _interpolate(p_val, n_val, t)


	func _interpolate(a: Variant, b: Variant, t: float) -> Variant:
		if mode == Mode.ANGLE:
			return lerp_angle(a, b, t)
		return lerp(a, b, t)


	func _snap(v1: Variant, v2: Variant, dist: float) -> bool:
		if typeof(v1) != typeof(v2):
			return true
		match typeof(v1):
			TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
				return v1.distance_to(v2) > dist
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
			TYPE_FLOAT, TYPE_INT:
				var diff := abs(angle_difference(v1, v2)) if mode == Mode.ANGLE else abs(v1 - v2)
				return diff < 0.001
		return true

#endregion
