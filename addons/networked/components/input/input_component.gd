## Abstract base component for multiplayer-aware input handling.
##
## Subclass this and implement [method get_inputs] to return the list of action names your
## component tracks. Processing is automatically disabled on non-authoritative peers.
## [codeblock]
## class_name MoveInputComponent
## extends InputComponent
##
## @export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
## var move_left: StringName = "move_left"
##
## func get_inputs() -> Array:
##     return [move_left, ...]
## [/codeblock]
@abstract
class_name InputComponent
extends NetComponent

## Emitted when a tracked action's pressed state changes.
signal action_changed(action: StringName, pressed: bool)

## Emitted once per simulation tick (when [member tick_mode] is [code]true[/code] and this peer
## is the multiplayer authority). Carries the tick number and a snapshot of the current state.
signal tick_snapshot(tick: int, state: Dictionary)

## When [code]true[/code], connects to [NetworkClock.on_tick] and emits [signal tick_snapshot]
## each tick. Requires a [NetworkClock] registered on this node's multiplayer API.
@export var tick_mode: bool = false

## Current pressed state for each tracked action, keyed by action name.
@onready var state: Dictionary[StringName, bool] = build_state_dict_from_actions()

## Returns the list of action name strings this component should track.
@abstract func get_inputs() -> Array


func _enter_tree() -> void:
	if not is_multiplayer_authority():
		process_mode = Node.PROCESS_MODE_DISABLED
		return


func _ready() -> void:
	if not is_multiplayer_authority():
		process_mode = Node.PROCESS_MODE_DISABLED
		return
	if tick_mode:
		var clock := NetworkClock.for_node(self)
		if clock:
			clock.on_tick.connect(_on_tick)
		else:
			NetLog.warn("InputComponent: tick_mode=true but no NetworkClock found on this node's multiplayer API.", [], func(m): push_warning(m))

## Builds the initial [member state] dictionary from [method get_inputs].
func build_state_dict_from_actions() -> Dictionary[StringName, bool]:
	var _state: Dictionary[StringName, bool]
	
	for action in get_inputs():
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
				log_trace("InputComponent: Action %s Pressed" % action)
				action_changed.emit(action, true)
		elif event.is_action_released(action):
			if state[action] != false:
				state[action] = false
				log_trace("InputComponent: Action %s Released" % action)
				action_changed.emit(action, false)


func _on_tick(_delta: float, t: int) -> void:
	tick_snapshot.emit(t, state.duplicate())


## Returns [code]true[/code] if [param action] is currently held down.
func is_down(action: StringName) -> bool:
	assert(InputMap.has_action(action), "Input action `%s` doen't exist in \
`InputMap`." % action)
	return state.get(action, false)


## Returns a [-1, 1] float from two opposing actions: [param negative_action] and [param positive_action].
func get_axis(negative_action: StringName, positive_action: StringName) -> float:
	var p_action := 1.0 if is_down(positive_action) else 0.0
	var n_action := 1.0 if is_down(negative_action) else 0.0
	return p_action - n_action


## Returns a normalized [Vector2] from four directional actions, or [code]Vector2.ZERO[/code] when idle.
func get_vector2(
		left: StringName,
		right: StringName,
		up: StringName,
		down: StringName,
) -> Vector2:
	var v := Vector2(get_axis(left, right), get_axis(up, down))
	return v if v.is_zero_approx() else v.normalized()
