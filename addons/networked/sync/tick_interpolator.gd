## Smoothly interpolates a node's properties between network snapshots.
@tool
class_name TickInterpolator
extends NetwComponent


#region ── Enums ───────────────────────────────────────────────────────────────

## Defines the interpolation algorithm for a property.
enum Mode {
	NONE = 0, ## No interpolation.
	LERP = 1, ## Linear interpolation.
	ANGLE = 2, ## Angular interpolation (shortest path).
}


## Defines how interpolated values are written to [member visual_root].
enum VisualOutputMode {
	AUTO = 0, ## Choose the safest mode from source and target properties.
	PROPERTY_VALUE = 1, ## Write the interpolated value directly.
	SOURCE_DELTA = 2, ## Apply the smooth source delta to the initial value.
	OWNER_TRANSFORM_COMPENSATED = 3, ## Keep the visual at the smooth owner pose.
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
@export var visual_root_property_map: Dictionary[StringName, StringName] = {}:
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


## Controls the "softness" or "floatiness" of the interpolation.
## [br]- [code]0.0[/code]: Crisp and instant (pure time-based interpolation).
## [br]- [code]> 0.0[/code]: Adds exponential smoothing, making motion feel
## heavier/fluid but adding visual lag.
@export_range(0.0, 0.99) var smoothing: float = 0.0


## Maximum distance allowed before the interpolator snaps to the target instead
## of lerping.
## Useful for teleports. Set to [code]0.0[/code] to disable.
@export_custom(0, "suffix:px") var max_lerp_distance: float = 0.0


## The maximum number of extra ticks the interpolator can dilate beyond its
## floor. Increasing this value might help when network jitters.
@export_custom(0, "suffix:ticks") var max_extra_dilation: float = 0.0


## If greater than [code]0[/code], the interpolator will log its internal state
## every N frames.
@export_custom(0, "suffix:frames")  var trace_interval: int = 0

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


## Clears all recorded history and resets the visual state to match 
## the current raw positions. 
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
## Returns a [SceneTreeTimer] that can be awaited.
func disable_for(duration: float) -> SceneTreeTimer:
	process_mode = PROCESS_MODE_DISABLED
	reset()
	var self_ref := weakref(self)
	var timer := get_tree().create_timer(duration)
	timer.timeout.connect(func():
		var inst = self_ref.get_ref()
		if inst and is_instance_valid(inst) and inst.owner and not inst.owner.is_multiplayer_authority():
			inst.process_mode = PROCESS_MODE_INHERIT
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

var _expected_interval_ticks: int = 3
var _has_explicit_sync_interval: bool = false

var _peer_batcher: _Batcher
var _was_starving: bool = false
var _dbg: NetwHandle = Netw.dbg.handle(self)

# Persists the visual_root's design-time local offset so it survives _ready
# re-runs (e.g. after a teleport reparent that calls request_ready). Without
# this, _refresh_property_states/reset would re-capture the visual_root's
# current local position, which already contains the last frame's smoothing
# correction, baking that correction into the new baseline.
var _cached_initial_offsets: Dictionary[StringName, Variant] = {}
var _cached_initial_global_offsets: Dictionary[StringName, Variant] = {}

#endregion


#region ── Lifecycle ───────────────────────────────────────────────────────────

func _ready() -> void:
	if Engine.is_editor_hint(): return
	
	if not owner:
		owner = get_parent()
		if owner:
			_dbg.warn(
				"TickInterpolator: 'owner' property is not set. Falling back to parent node '%s'. " + \
				"Assign the owner explicitly for better stability.", 
				[owner.name]
			)
	
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

	if owner:
		for sync in SynchronizersCache.get_client_synchronizers(owner):
			if sync.synchronized.is_connected(_on_synced):
				sync.synchronized.disconnect(_on_synced)
			if sync.delta_synchronized.is_connected(_on_synced):
				sync.delta_synchronized.disconnect(_on_synced)


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
					v_root
				)
				_cached_initial_global_offsets[prop] = \
						state.initial_global_offset
		state._has_recorded = false
		state.is_sleeping = false
		_states.append(state)
	
	_cache_sync_intervals()


func _resolve_visual_output_mode(
	state: _PropertyState,
	v_root: Node
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


func _calculate_min_lag() -> float:
	if not _clock:
		return 0.0
		
	var needed := float(_expected_interval_ticks + 1)
	var network_padding := float(maxi(0, _clock.recommended_display_offset - _clock.display_offset))
	return maxf(0.0, needed - float(_clock.display_offset) + network_padding)


func _cache_sync_intervals() -> void:
	var max_interval := 0.0
	
	# Reset signal tracking
	for state in _states:
		state.uses_signal = false
		
	var all_syncs := SynchronizersCache.get_client_synchronizers(owner)
	var synced_props := SynchronizersCache.get_all_synchronized_properties(owner)

	for sync in all_syncs:
		max_interval = maxf(max_interval, maxf(sync.replication_interval, sync.delta_interval))
		if not sync.synchronized.is_connected(_on_synced):
			sync.synchronized.connect(_on_synced)
		if not sync.delta_synchronized.is_connected(_on_synced):
			sync.delta_synchronized.connect(_on_synced)

	# Mark which properties are covered by signals
	for state in _states:
		if state.name in synced_props:
			state.uses_signal = true
				
	_has_explicit_sync_interval = max_interval > 0.0
	if _clock:
		_expected_interval_ticks = maxi(1, ceili(max_interval * _clock.tickrate))


func _on_synced() -> void:
	var tick := _clock.tick
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

	if not visual_root.is_empty():
		var v_root := get_node_or_null(visual_root)
		if v_root:
			for state in _states:
				if _uses_risky_source_delta(state):
					warnings.append(
						"[code]visual_output_mode[/code] is " + \
						"[code]SOURCE_DELTA[/code] for '%s'. " % state.name + \
						"If the owner also moves locally, the visual child " + \
						"can fight the owner transform. Use " + \
						"[code]AUTO[/code] or " + \
						"[code]OWNER_TRANSFORM_COMPENSATED[/code] for " + \
						"server-authoritative character visuals."
					)
				if (
					state.output_mode == \
							VisualOutputMode.OWNER_TRANSFORM_COMPENSATED
					and state.initial_global_offset == null
				):
					warnings.append(
						"Property '%s' requested owner-transform " % state.name + \
						"compensation, but the source and visual target are " + \
						"not compatible. TickInterpolator will fall back to " + \
						"[code]SOURCE_DELTA[/code]."
					)
				var target_name := visual_root_property_map.get(
					state.name, state.name
				)
				var target_val = v_root.get(target_name)
				if typeof(target_val) == TYPE_NIL:
					warnings.append(
						"Property '%s' is interpolated but the visual " % str(state.name) + \
						"root '%s' has no matching property '%s'. " % [v_root.name, target_name] + \
						"Set a mapping in the 'Visual Root Mappings' section."
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

	props.append({"name": "Interpolated Properties", "type": TYPE_NIL, "usage": PROPERTY_USAGE_GROUP, "hint_string": "interpolation/"})

	for prop_path_str in _get_tracked_properties(owner):
		props.append({
			"name": "interpolation/" + prop_path_str,
			"type": TYPE_INT,
			"usage": PROPERTY_USAGE_EDITOR,
			"hint": PROPERTY_HINT_ENUM,
			"hint_string": "None,Lerp,Angle"
		})

	var v_root := get_node_or_null(visual_root) \
		if not visual_root.is_empty() else null
	if v_root:
		props.append({"name": "Visual Root Mappings", "type": TYPE_NIL, "usage": PROPERTY_USAGE_GROUP, "hint_string": "mapping/"})

		for prop_path_str in _get_tracked_properties(owner):
			if (
				not property_modes.has(prop_path_str)
				or property_modes[prop_path_str] == Mode.NONE
			):
				continue
			var source_type := _get_property_type(prop_path_str)
			var compatible := _get_compatible_target_properties(
				v_root, source_type, prop_path_str
			)
			if compatible.is_empty():
				continue
			props.append({
				"name": "mapping/" + prop_path_str,
				"type": TYPE_STRING,
				"usage": PROPERTY_USAGE_EDITOR,
				"hint": PROPERTY_HINT_ENUM,
				"hint_string": ",".join(compatible)
			})

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
		if value == Mode.NONE: property_modes.erase(prop_name)
		else: property_modes[prop_name] = value as Mode
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
	v_root: Node, source_type: int, source_name: StringName
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
		if instances.is_empty():
			shutdown()

	func shutdown() -> void:
		if clock and clock.after_tick.is_connected(_on_clock_tick):
			clock.after_tick.disconnect(_on_clock_tick)
		clock = null
		instances.clear()

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
		if not source_obj: return
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
			result
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
			result
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
