@tool
## Editor authoring shell for [NetwInterpolationInterface].
##
## [MultiplayerInterpolator] writes entity display settings into
## [member NetwEntity.interpolation] and per property [NetwInterpolate]
## resources into [method Netw.configure_property]. Runtime interpolation is
## owned by [NetwInterpolationInterface].
## [codeblock]
## # Inspector:
## # interp/position = NetwInterpolate.new().lerp()
##
## # Equivalent code:
## Netw.configure_property(self, &"position").interpolate(
##     NetwInterpolate.new().lerp()
## )
## NetwEntity.of(self).interpolation.visual_root = NodePath("Visual")
## [/codeblock]
class_name MultiplayerInterpolator
extends NetwComponent

@export_group("Display")

## Display role written to [member NetwEntity.interpolation].
@export var display_role: NetwInterpolationInterface.DisplayRole = (
		NetwInterpolationInterface.DisplayRole.AUTO
):
	set(value):
		display_role = value
		_apply_handle()

## Visual child written to [member NetwEntity.interpolation].
@export var visual_root: NodePath = NodePath(""):
	set(value):
		visual_root = value
		_apply_handle()
		update_configuration_warnings()
		notify_property_list_changed()

## Per property interpolation resources.
@export var property_interpolators: Dictionary = { }:
	set(value):
		property_interpolators = value
		_apply_property_interpolators()
		update_configuration_warnings()
		notify_property_list_changed()

@export_group("Predicted Display")

## Exponential smoothing time for predicted display chase mode.
@export_custom(0, "suffix:s") var predicted_smooth_time: float = 0.0:
	set(value):
		predicted_smooth_time = value
		_apply_handle()

## Predicted display filter written to [member NetwEntity.interpolation].
@export var predicted_mode: NetwInterpolationInterface.PredictedMode = (
		NetwInterpolationInterface.PredictedMode.CHASE
):
	set(value):
		predicted_mode = value
		_apply_handle()

@export_group("Advanced Dilation")

## Enables display lag adaptation for remote interpolation.
@export var enable_smart_dilation: bool = true:
	set(value):
		enable_smart_dilation = value
		_apply_handle()

## Maximum extra ticks that display lag can grow while starving.
@export_custom(0, "suffix:ticks") var max_extra_dilation: float = 0.0:
	set(value):
		max_extra_dilation = value
		_apply_handle()

## Per frame fraction used to track the measured lag floor.
@export_range(0.0, 1.0) var lag_adapt_rate: float = 0.05:
	set(value):
		lag_adapt_rate = value
		_apply_handle()

## Ticks per frame added after starvation is sustained.
@export_range(0.0, 1.0) var starvation_growth: float = 0.95:
	set(value):
		starvation_growth = value
		_apply_handle()

## Per frame fraction used to low pass the lag floor.
@export_range(0.0, 1.0) var floor_smoothing: float = 0.05:
	set(value):
		floor_smoothing = value
		_apply_handle()

## Starving frames tolerated before lag starts growing.
@export_custom(0, "suffix:frames") var starvation_grace_frames: int = 3:
	set(value):
		starvation_grace_frames = value
		_apply_handle()

## Frames between interpolation trace logs. [code]0[/code] disables logs.
@export_custom(0, "suffix:frames") var trace_interval: int = 0:
	set(value):
		trace_interval = value
		_apply_handle()

var _entity: NetwEntity
var _applying := false


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_bind_entity()
	_apply_handle()
	_apply_property_interpolators()


func _bind_entity() -> void:
	var target_owner := _entity_owner()
	if not target_owner:
		return
	_entity = NetwEntity.of(target_owner)
	if _entity and not _entity.reparented.is_connected(_on_reparented):
		_entity.reparented.connect(_on_reparented)


func _on_reparented(_reparent: NetwEntity.ReparentOpts) -> void:
	_bind_entity()
	_apply_handle()
	_apply_property_interpolators()


func _handle() -> NetwInterpolationInterface.Handle:
	if not _entity:
		_bind_entity()
	return _entity.interpolation if _entity else null


func _entity_owner() -> Node:
	return owner if owner else get_parent()


func _apply_handle() -> void:
	if _applying or Engine.is_editor_hint():
		return
	var handle := _handle()
	if not handle:
		return
	_applying = true
	handle.visual_root = _owner_relative_visual_root()
	handle.display_role = display_role
	handle.predicted_smooth_time = predicted_smooth_time
	handle.predicted_mode = predicted_mode
	handle.enable_smart_dilation = enable_smart_dilation
	handle.max_extra_dilation = max_extra_dilation
	handle.lag_adapt_rate = lag_adapt_rate
	handle.starvation_growth = starvation_growth
	handle.floor_smoothing = floor_smoothing
	handle.starvation_grace_frames = starvation_grace_frames
	handle.trace_interval = trace_interval
	_applying = false


func _apply_property_interpolators() -> void:
	if _applying or Engine.is_editor_hint():
		return
	var target_owner := _entity_owner()
	if not target_owner:
		return
	_applying = true
	for prop: StringName in property_interpolators:
		var spec: NetwInterpolate = (
				property_interpolators[prop] as NetwInterpolate
		)
		if not spec:
			continue
		var source := _resolve_source(prop)
		if source.is_empty():
			continue
		var node := source[0] as Node
		var property := source[1] as StringName
		Netw.configure_property(node, property, false).interpolate(spec)
	_applying = false
	var iface := NetwInterpolationInterface.for_node(target_owner)
	if iface and _entity:
		iface._mark_runtime_dirty(_entity.interpolation)


func set_property_interpolator(
		property: StringName,
		interpolator: NetwInterpolate,
) -> void:
	if interpolator == null:
		property_interpolators.erase(property)
	else:
		property_interpolators[property] = interpolator
	_apply_property_interpolators()


func _owner_relative_visual_root() -> NodePath:
	var target_owner := _entity_owner()
	if visual_root.is_empty() or not target_owner:
		return visual_root
	var target := get_node_or_null(visual_root)
	if target and target_owner.is_ancestor_of(target):
		return target_owner.get_path_to(target)
	return visual_root


func _resolve_source(prop: StringName) -> Array:
	var target_owner := _entity_owner()
	if not target_owner:
		return []
	var path := (
			NodePath(str(prop))
			if ":" in str(prop)
			else NodePath(":" + str(prop))
	)
	var res := target_owner.get_node_and_resource(path)
	var node := res[0] as Node
	if not node:
		return []
	var subpath: NodePath = res[2]
	if subpath.get_subname_count() <= 0:
		return []
	return [node, StringName(subpath.get_subname(0))]


func _get_property_list() -> Array[Dictionary]:
	var props: Array[Dictionary] = []
	var target_owner := _entity_owner()
	if not Engine.is_editor_hint() or not target_owner:
		return props
	props.append(
		{
			"name": "Interpolators",
			"type": TYPE_NIL,
			"usage": PROPERTY_USAGE_GROUP,
			"hint_string": "interp/",
		},
	)
	for prop in _editor_interpolator_keys():
		props.append(
			{
				"name": "interp/" + prop,
				"type": TYPE_OBJECT,
				"usage": PROPERTY_USAGE_EDITOR,
				"hint": PROPERTY_HINT_RESOURCE_TYPE,
				"hint_string": "NetwInterpolate",
			},
		)
	return props


func _get(property: StringName) -> Variant:
	if property.begins_with("interp/"):
		return property_interpolators.get(
			StringName(property.trim_prefix("interp/")),
			null,
		)
	return null


func _set(property: StringName, value: Variant) -> bool:
	if property.begins_with("interp/"):
		set_property_interpolator(
			StringName(property.trim_prefix("interp/")),
			value as NetwInterpolate,
		)
		notify_property_list_changed()
		return true
	return false


func _validate_property(property: Dictionary) -> void:
	if property.name == "property_interpolators":
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE
	if StringName(property.name) in [&"predicted_smooth_time", &"predicted_mode"]:
		if not _has_prediction_component():
			property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	var target_owner := _entity_owner()
	if not target_owner:
		return warnings
	if _is_physics_body(target_owner) and visual_root.is_empty():
		for prop: StringName in property_interpolators:
			if prop in [&"position", &"global_position"]:
				warnings.append(
					"Physics body interpolation should write to a visual child.",
				)
				break
	return warnings


func _editor_interpolator_keys() -> Array[StringName]:
	var out: Array[StringName] = []
	var target_owner := _entity_owner()
	if not target_owner:
		return out
	for prop in _get_tracked_properties(target_owner):
		out.append(prop)
	for prop in property_interpolators:
		if not out.has(prop):
			out.append(prop)
	return out


func _get_tracked_properties(target: Node) -> Array[StringName]:
	if not target:
		return []
	var result: Array[StringName] = []
	var props := SynchronizersCache.get_all_synchronized_properties(target)
	for clean_name in props:
		var value := SynchronizersCache.resolve_value(target, props[clean_name])
		if value != null and typeof(value) in [
			TYPE_FLOAT,
			TYPE_VECTOR2,
			TYPE_VECTOR3,
			TYPE_COLOR,
			TYPE_QUATERNION,
		]:
			result.append(clean_name)
	return result


func _has_prediction_component() -> bool:
	if _entity and _entity.prediction.is_registered():
		return true
	var target_owner := _entity_owner()
	return (
			target_owner
			and target_owner.get_node_or_null("%PredictionComponent") != null
	)


func _is_physics_body(node: Node) -> bool:
	return (
			node is CharacterBody2D
			or node is RigidBody2D
			or (
					ClassDB.class_exists("CharacterBody3D")
					and node.is_class("CharacterBody3D")
			)
			or (
					ClassDB.class_exists("RigidBody3D")
					and node.is_class("RigidBody3D")
			)
	)
