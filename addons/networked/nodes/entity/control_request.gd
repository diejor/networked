class_name ControlRequest
extends RefCounted
## Server decision object for a controller request.
##
## [MultiplayerEntity] emits [signal MultiplayerEntity.control_requested]
## with one [ControlRequest] per request. Gameplay code may inspect
## [member requester] and call [method deny] before the default grant path
## runs.
## [codeblock]
## func _on_control_requested(peer_id: int, request: ControlRequest) -> void:
##     if not can_carry(peer_id):
##         request.deny()
## [/codeblock]

## Peer id reported by [method MultiplayerAPI.get_remote_sender_id].
var requester: int = 0

## Whether the request should be rejected.
var denied := false


## Rejects this request.
func deny() -> void:
	denied = true
