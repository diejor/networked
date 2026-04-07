## Smoothly interpolates a node's properties between network snapshots.
##
## Attach as a child of the node to interpolate. This node acts as a proxy
## that forward-interpolates between the snapshot just before [member NetworkClock.display_tick]
## and the snapshot just after it.
##
## [b]Note:[/b] Requires a [NetworkClock] to be registered on the [SceneMultiplayer] API.
@tool
class_name TickInterpolator
extends Node


#region ── Configuration ───────────────────────────────────────────────────────

## List of property paths relative to the parent node to be interpolated.
## [br]Format: Simple identifiers (e.g. [code]&"position"[/code]) or nested paths
## (e.g. [code]&"Visuals/Sprite:modulate"[/code]).
## [br]These are managed via the [b]Interpolated Properties[/b] group in the inspector.
@export var properties: Array[StringName] = []:
	set(v):
		properties = v
		_refresh_path_cache()
		update_configuration_warnings()
		notify_property_list_changed()

## If [code]true[/code], the interpolator will locally slow down its playhead when snapshots
## are missing. This prevents jitter by adding minimal local latency during starvation.
@export var enable_smart_dilation: bool = true

## Maximum distance allowed before the interpolator snaps to the target instead of lerping.
## Useful for teleports. Set to [code]0.0[/code] to disable.
@export var max_lerp_distance: float = 0.0

## The maximum number of extra ticks the interpolator can dilate 
## beyond its calculated floor. Lower values (e.g. 2-4) favor 
## reaction time/freshness; higher values (e.g. 8-16) favor visual smoothness.
@export var max_extra_dilation: float = 4.0

#endregion


#region ── Internal State ──────────────────────────────────────────────────────

const _LERP_TYPES: Array[int] = [
	TYPE_INT, TYPE_FLOAT,
	TYPE_VECTOR2, TYPE_VECTOR2I,
	TYPE_VECTOR3, TYPE_VECTOR3I,
	TYPE_VECTOR4, TYPE_VECTOR4I,
	TYPE_COLOR,
	TYPE_QUATERNION,
	TYPE_BASIS,
	TYPE_TRANSFORM2D,
	TYPE_TRANSFORM3D,
]

const _TRACE_EVERY_N_FRAMES := 30
const _CATCHUP_SPEED := 1.1
const _DILATION_STRENGTH := 0.95
const _STARVATION_GRACE_FRAMES := 3

var _starvation_frames: int = 0

var _buffers: Dictionary[StringName, HistoryBuffer] = {}
var _clock: NetworkClock
var _target: Node
var _trace_frame: int = 0
var _local_delay: float = 0.0

var _path_cache: Dictionary[StringName, NodePath] = {}
var _network_snapshots: Dictionary[StringName, Variant] = {}
var _last_written_values: Dictionary[StringName, Variant] = {}
var _last_recorded_values: Dictionary[StringName, Variant] = {}

## Cached from synchronizer config at ready time. Used by smart dilation to distinguish
## genuine starvation from the absence of packets caused by a stationary on_change property.
var _expected_interval_ticks: int = 1
var _has_explicit_sync_interval: bool = false

#endregion


#region ── Lifecycle ───────────────────────────────────────────────────────────

func _ready() -> void:
	if Engine.is_editor_hint():
		return

	_target = get_parent()
	_clock = NetworkClock.for_node(self)

	assert(_target, "TickInterpolator: Parent node is missing.")
	assert(_clock, "TickInterpolator: Requires a NetworkClock on the multiplayer API.")

	if _target.is_multiplayer_authority():
		process_mode = PROCESS_MODE_DISABLED
		return

	_refresh_path_cache()
	_initialize_buffers()
	_cache_expected_interval_ticks()
	
	_local_delay = _min_local_delay()
	_clock.after_tick.connect(_on_clock_after_tick)
	
	
	
	NetLog.info("Ready: target=%s, props=%s, offset=%d" % [
		_target.name, properties, _clock.display_offset
	])
	_check_runtime_performance_config()


func _process(_delta: float) -> void:
	if not _target or _target.is_multiplayer_authority():
		return

	_update_network_snapshots()
	_apply_interpolations()

#endregion


#region ── Public API ──────────────────────────────────────────────────────────

## Clears all history buffers and resets internal caches.
## Call this after a manual teleport to prevent lerping from the old position.
func reset() -> void:
	_local_delay = 0.0
	_starvation_frames = 0
	_last_written_values.clear()
	_last_recorded_values.clear()
	_network_snapshots.clear()
	_initialize_buffers()


## Temporarily disables interpolation for [param duration] seconds.
## Returns a [SceneTreeTimer] that can be awaited.
## [codeblock]
## await interpolator.disable_for(0.5).timeout
## # Interpolation is now paused for 0.5s and history is cleared.
## [/codeblock]
func disable_for(duration: float) -> SceneTreeTimer:
	process_mode = PROCESS_MODE_DISABLED
	reset()

	var timer := get_tree().create_timer(duration)
	timer.timeout.connect(_on_disable_timeout)
	return timer

#endregion


#region ── Interpolation ───────────────────────────────────────────────────────

func _refresh_path_cache() -> void:
	_path_cache.clear()
	for prop in properties:
		var s := str(prop)
		if not ":" in s:
			_path_cache[prop] = NodePath(":" + s)
		else:
			_path_cache[prop] = NodePath(s)


func _initialize_buffers() -> void:
	_buffers.clear()
	for prop in properties:
		_buffers[prop] = HistoryBuffer.new(16)
		_network_snapshots[prop] = SynchronizersCache.resolve_value(_target, _path_cache[prop])


func _update_network_snapshots() -> void:
	for prop in properties:
		var current_val: Variant = SynchronizersCache.resolve_value(_target, _path_cache[prop])
		if current_val != _last_written_values.get(prop):
			_network_snapshots[prop] = current_val


func _on_clock_after_tick(_delta: float, tick: int) -> void:
	for prop in properties:
		var snapshot: Variant = _network_snapshots.get(prop)
		if snapshot == _last_recorded_values.get(prop):
			continue

		_buffers[prop].record(tick, snapshot)
		_last_recorded_values[prop] = snapshot


func _apply_interpolations() -> void:
	var global_dt := _clock.display_tick
	var global_factor := _clock.tick_factor

	if enable_smart_dilation:
		_update_local_dilation(global_dt)
	else:
		_local_delay = 0.0

	var global_time := float(global_dt) + global_factor
	var effective_time := global_time - _local_delay

	var dt := int(floor(effective_time))
	var factor := effective_time - float(dt)

	_trace_frame = (_trace_frame + 1) % _TRACE_EVERY_N_FRAMES
	var should_trace := _trace_frame == 0

	for prop in properties:
		_interpolate_property(prop, dt, factor, should_trace)


func _interpolate_property(prop: StringName, dt: int, factor: float, trace: bool) -> void:
	var buffer := _buffers[prop]
	var prev_tick := buffer.get_latest_tick_at_or_before(dt)
	var next_tick := buffer.get_earliest_tick_after(dt)

	if prev_tick == -1:
		return

	var result: Variant
	if next_tick == -1:
		result = buffer.get_at(prev_tick)
	else:
		var prev_val := buffer.get_at(prev_tick)
		var next_val := buffer.get_at(next_tick)

		if max_lerp_distance > 0.0 and _should_snap(prev_val, next_val):
			result = next_val
		else:
			var span := float(next_tick - prev_tick)
			var elapsed := float(dt - prev_tick) + factor
			var t := clampf(elapsed / span, 0.0, 1.0)
			result = lerp(prev_val, next_val, t)

	SynchronizersCache.assign_value(_target, _path_cache[prop], result)
	_last_written_values[prop] = result

	if trace:
		NetLog.trace("Interpolate %s: dt=%d local_delay=%.2f val=%s" % [prop, dt, _local_delay, result])


func _should_snap(v1: Variant, v2: Variant) -> bool:
	var type := typeof(v1)
	if type != typeof(v2): return true

	match type:
		TYPE_VECTOR2, TYPE_VECTOR2I, TYPE_VECTOR3, TYPE_VECTOR3I:
			return v1.distance_to(v2) > max_lerp_distance
		TYPE_FLOAT, TYPE_INT:
			return abs(v1 - v2) > max_lerp_distance
	return false


func _on_disable_timeout() -> void:
	if is_instance_valid(self) and _target and not _target.is_multiplayer_authority():
		process_mode = PROCESS_MODE_INHERIT

#endregion


#region ── Smart Dilation ──────────────────────────────────────────────────────

func _update_local_dilation(global_dt: int) -> void:
	var effective_dt := int(floor(float(global_dt) - _local_delay))
	var is_starving := false

	for prop in properties:
		var buffer := _buffers[prop]
		if buffer.get_earliest_tick_after(effective_dt) != -1:
			continue

		var newest := buffer.newest_tick()
		if newest != -1 and effective_dt - newest < _expected_interval_ticks:
			is_starving = true
			break

	var frame_ticks := get_process_delta_time() * _clock.tickrate
	var current_floor := _min_local_delay()

	if is_starving:
		_starvation_frames += 1
		if _starvation_frames >= _STARVATION_GRACE_FRAMES:
			var dynamic_ceiling := current_floor + max_extra_dilation
			_local_delay = minf(_local_delay + (frame_ticks * _DILATION_STRENGTH), dynamic_ceiling)
	else:
		_starvation_frames = 0
		_local_delay = maxf(current_floor, _local_delay - (frame_ticks * (_CATCHUP_SPEED - 1.0)))


func _min_local_delay() -> float:
	var needed_delay := float(_expected_interval_ticks + 1)
	
	var provided_delay := float(_clock.display_offset)
	
	var network_padding := float(maxi(0, _clock.recommended_display_offset - _clock.display_offset))
	
	return maxf(0.0, needed_delay - provided_delay + network_padding)

#endregion


#region ── Editor & Warnings ───────────────────────────────────────────────────

func _cache_expected_interval_ticks() -> void:
	var max_interval := 0.0
	for sync in SynchronizersCache.get_client_synchronizers(_target):
		max_interval = maxf(max_interval, maxf(sync.replication_interval, sync.delta_interval))
	_has_explicit_sync_interval = max_interval > 0.0
	_expected_interval_ticks = maxi(1, ceili(max_interval * _clock.tickrate))


func _check_runtime_performance_config() -> void:
	if not _has_explicit_sync_interval:
		return
	if _clock.display_offset < _expected_interval_ticks:
		var msg := "TickInterpolator on '%s': display_offset (%d) is below the recommended %d." % [
			_target.name, _clock.display_offset, _expected_interval_ticks
		]
		msg += " Smart Dilation will add local latency." if enable_smart_dilation else " Motion will be choppy."
		push_warning(msg)


func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	var target = get_parent()
	if not Engine.is_editor_hint() or not target: return props

	props.append({
		"name": "Interpolated Properties",
		"type": TYPE_NIL,
		"usage": PROPERTY_USAGE_GROUP,
		"hint_string": "interpolation/"
	})

	for prop_path_str in _get_tracked_interpolable_properties(target):
		props.append({
			"name": "interpolation/" + prop_path_str,
			"type": TYPE_BOOL,
			"usage": PROPERTY_USAGE_EDITOR
		})
	return props


func _get(property: StringName) -> Variant:
	if property.begins_with("interpolation/"):
		var prop_path_str := StringName(property.trim_prefix("interpolation/"))
		return prop_path_str in properties
	return null


func _set(property: StringName, value: Variant) -> bool:
	if property.begins_with("interpolation/"):
		var prop_path_str := StringName(property.trim_prefix("interpolation/"))
		if value:
			if not prop_path_str in properties: properties.append(prop_path_str)
		else:
			properties.erase(prop_path_str)
		_refresh_path_cache()
		update_configuration_warnings()
		notify_property_list_changed()
		return true
	return false


func _validate_property(property: Dictionary) -> void:
	if property.name == "properties":
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	var target = get_parent()
	if not target: return warnings

	var tracked := SynchronizersCache.get_all_synchronized_properties(target)
	for prop in properties:
		if not tracked.has(prop):
			warnings.append("'%s' is not tracked by any synchronizer on the parent." % prop)
			continue
		var val = SynchronizersCache.resolve_value(target, _path_cache[prop])
		if val == null:
			warnings.append("'%s' does not exist on the parent hierarchy." % prop)
		elif not (typeof(val) in _LERP_TYPES):
			warnings.append("'%s' type (%s) cannot be lerped." % [prop, type_string(typeof(val))])
	return warnings


func _get_tracked_interpolable_properties(target: Node) -> Array[StringName]:
	var result: Array[StringName] = []
	var props := SynchronizersCache.get_all_synchronized_properties(target)

	for clean_name in props:
		var path := props[clean_name]
		var value := SynchronizersCache.resolve_value(target, path)

		if value != null and typeof(value) in _LERP_TYPES:
			result.append(clean_name)
	return result

#endregion
