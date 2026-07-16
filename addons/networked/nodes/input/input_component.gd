## Abstract base component for multiplayer-aware input handling.
##
## Subclass this and implement [method _get_inputs] to return the list of action names your
## component tracks. Processing is automatically disabled on non-authoritative peers.
## [codeblock]
## class_name MoveInputComponent
## extends InputComponent
##
## @export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
## var move_left: StringName = "move_left"
##
## func _get_inputs() -> Array:
##     return [move_left, ...]
## [/codeblock]
@abstract
class_name InputComponent
extends NetwComponent

## Emitted when a tracked action's pressed state changes.
signal action_changed(action: StringName, pressed: bool)

## Emitted once per simulation tick (when [member tick_mode] is [code]true[/code] and this peer
## is the multiplayer authority). Carries the tick number and a snapshot of the current state.
signal tick_snapshot(tick: int, state: Dictionary)

## When [code]true[/code], connects to [signal NetwClockInterface.on_tick] and
## emits [signal tick_snapshot] each tick. Requires a [MultiplayerClock]
## registered on this node's multiplayer API.
@export var tick_mode: bool = false

## Current pressed state for each tracked action, keyed by action name.
@onready var state: Dictionary[StringName, bool] = build_state_dict_from_actions()

var _dbg: NetwHandle = Netw.dbg.handle(self)


## Returns the list of action name strings this component should track.
@abstract func _get_inputs() -> Array


# The controlling peer keeps latching input under PROCESS_MODE_ALWAYS so an
# ancestor held in PROCESS_MODE_DISABLED (the teleport reparent guard) never
# deafens _unhandled_input mid-teleport, which would strand a released key as
# pressed in state. Non-authority peers never latch local input.
func _sync_process_mode_to_authority() -> void:
	process_mode = (
		Node.PROCESS_MODE_ALWAYS
		if is_multiplayer_authority()
		else Node.PROCESS_MODE_DISABLED
	)


func _enter_tree() -> void:
	_sync_process_mode_to_authority()


func _ready() -> void:
	_sync_process_mode_to_authority()
	if not is_multiplayer_authority():
		return
	if tick_mode:
		var api := NetwMultiplayer.of(self)
		var clock := api.clock if api and api.clock.is_configured() else null
		if clock:
			clock.before_tick.connect(_on_before_tick)
			clock.on_tick.connect(_on_tick)
		else:
			_dbg.warn("tick_mode=true but no MultiplayerClock found on this node's multiplayer API.", func(m): push_warning(m))


## Override to refresh this component's replicated input exports from the current
## [member state] before each tick is simulated.
##
## Runs at [signal NetwClockInterface.before_tick] on the controlling peer only, so
## the values the input set ships and a [PredictionComponent] snapshots are
## the tick's gathered input, never a stale poll. The base is a no-op, so a
## subclass that only emits [signal tick_snapshot] needs no override.
## [codeblock]
## func _gather() -> void:
##     motion = get_vector2(move_left, move_right, move_up, move_down)
##     bombing = is_down(set_bomb)
## [/codeblock]
func _gather() -> void:
	pass


## Builds the initial [member state] dictionary from [method _get_inputs].
func build_state_dict_from_actions() -> Dictionary[StringName, bool]:
	var _state: Dictionary[StringName, bool]

	for action in _get_inputs():
		_state[action] = false

	assert(
		not _state.is_empty(),
		"`state` dictionary is empty when it's expected to have actions. \
Probably because the action properties are not marked with \
`action` through the `hint_string` of `@export_custom`.",
	)
	return _state


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.is_echo():
		return

	for action: StringName in state.keys():
		if event.is_action_pressed(action):
			if state[action] != true:
				state[action] = true
				_dbg.trace("Action %s Pressed", [action])
				action_changed.emit(action, true)
		elif event.is_action_released(action):
			if state[action] != false:
				state[action] = false
				_dbg.trace("Action %s Released", [action])
				action_changed.emit(action, false)


func _on_before_tick(_delta: float, _t: int) -> void:
	_gather()


func _on_tick(_delta: float, t: int) -> void:
	tick_snapshot.emit(t, state.duplicate())


## Returns [code]true[/code] if [param action] is currently held down.
func is_down(action: StringName) -> bool:
	assert(
		InputMap.has_action(action),
		"Input action `%s` doen't exist in \
`InputMap`." % action,
	)
	return state.get(action, false)


## Returns a value from -1 to 1 from two opposing actions:
## [param negative_action] and [param positive_action].
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
