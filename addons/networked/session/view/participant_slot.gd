## Isolated viewport tree for one local participant.
##
## [member tree], [member peer_id], and [member username] identify the
## participant mounted in this slot.
class_name ParticipantSlot
extends SubViewport

## Emitted when [method display_source] resolves to a new viewport.
signal display_source_changed(viewport: SubViewport)

## [MultiplayerTree] mounted inside this slot.
var tree: MultiplayerTree:
	get:
		return _tree
	set(value):
		_set_tree(value)

## Network peer id for the mounted participant.
var peer_id: int = 0

## Username used by the mounted participant.
var username: StringName = &""

var _pending_input: Array[InputEvent] = []
var _input_flush_queued := false
var _display_source := ParticipantDisplaySource.new()
var _tree: MultiplayerTree = null


func _ready() -> void:
	render_target_update_mode = SubViewport.UPDATE_DISABLED


func _exit_tree() -> void:
	_display_source.dispose()


## Returns the [SubViewport] that should be drawn for this participant.
func display_source() -> SubViewport:
	return _display_source.current


## Returns the [SubViewport] that should receive simulated slot input.
func input_target() -> SubViewport:
	var target := display_source()
	return target if is_instance_valid(target) else self


## Sends [param event] into this slot.
##
## Listen server hosts route input into the active nested
## [MultiplayerScene] viewport when one is available.
func send_input(event: InputEvent) -> void:
	_pending_input.append(event)
	if _input_flush_queued:
		return
	_input_flush_queued = true
	_flush_input.call_deferred()


func _flush_input() -> void:
	_input_flush_queued = false
	var events := _pending_input.duplicate()
	_pending_input.clear()
	var target := input_target()
	for event in events:
		target.push_input(event, true)


func _set_tree(value: MultiplayerTree) -> void:
	if _tree == value:
		return
	_tree = value
	if not _display_source.changed.is_connected(_on_display_source_changed):
		_display_source.changed.connect(_on_display_source_changed)
	_display_source.configure(_tree, self)


func _on_display_source_changed(viewport: SubViewport) -> void:
	display_source_changed.emit(viewport)
