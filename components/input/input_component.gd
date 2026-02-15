class_name InputComponent
extends Node

signal action_changed(action: StringName, pressed: bool)

@export var _actions: ActionsResource
@onready var state: Dictionary[StringName, bool] = build_state_dict_from_actions()


func _enter_tree() -> void:
	if not is_multiplayer_authority():
		process_mode = Node.PROCESS_MODE_DISABLED
		return


func _ready() -> void:
	if not is_multiplayer_authority():
		process_mode = Node.PROCESS_MODE_DISABLED
		return

func build_state_dict_from_actions() -> Dictionary[StringName, bool]:
	var _state: Dictionary[StringName, bool]
	
	for action in _actions.get_actions():
		_state[action] = false
	
	assert(not _state.is_empty(),
		"`state` dictionary is empty when it's expected to have actions. \
Probably because the action properties are not marked with \
`action` through the `hint_string` of `@export_custom`.")
	return _state


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.is_echo():
		return

	for action: StringName in state.keys():
		if event.is_action_pressed(action):
			if state[action] != true:
				state[action] = true
				action_changed.emit(action, true)
		elif event.is_action_released(action):
			if state[action] != false:
				state[action] = false
				action_changed.emit(action, false)


func is_down(action: StringName) -> bool:
	assert(InputMap.has_action(action), "Input action `%s` doen't exist in \
`InputMap`." % action)
	return state.get(action, false)


func get_axis(negative_action: StringName, positive_action: StringName) -> float:
	var p_action := 1.0 if is_down(positive_action) else 0.0
	var n_action := 1.0 if is_down(negative_action) else 0.0
	return p_action - n_action


func get_vector2(
		left: StringName,
		right: StringName,
		up: StringName,
		down: StringName,
) -> Vector2:
	var v := Vector2(get_axis(left, right), get_axis(up, down))
	return v if v.is_zero_approx() else v.normalized()
