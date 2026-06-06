## Isolated viewport tree for one local participant.
##
## [member tree], [member peer_id], and [member username] identify the
## participant mounted in this slot.
class_name ParticipantSlot
extends SubViewport

## [MultiplayerTree] mounted inside this slot.
var tree: MultiplayerTree

## Network peer id for the mounted participant.
var peer_id: int = 0

## Username used by the mounted participant.
var username: StringName = &""

var _pending_input: Array[InputEvent] = []
var _input_flush_queued := false


func _ready() -> void:
	render_target_update_mode = SubViewport.UPDATE_DISABLED


## Sends [param event] into this slot.
##
## Listen server hosts rely on [HostSceneView] to forward unhandled input into
## the active nested [MultiplayerScene] viewport.
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
	for event in events:
		push_input(event, true)
