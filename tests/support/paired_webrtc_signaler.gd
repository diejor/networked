## In-process [WebRTCSignaler] test double that shortcuts signaling.
##
## Two instances sharing a room id hand each [method WebRTCSignaler._send] call
## straight to the other's [signal WebRTCSignaler.received]. No trackers or
## sockets exist, so a real [WebRTCSession] handshake runs over loopback ICE
## with signaling offline. Routing reads only [param to_multiplayer_id], which
## the session always fills even before it has learned an address.
## [codeblock]
## host:   _open("", 1)           # generates a room id
## client: _open(room, client_id)  # joins the same room
## _send(to_multiplayer_id, addr, kind, payload) -> peer.received(...)
## [/codeblock]
class_name PairedWebRTCSignaler
extends WebRTCSignaler

const WebRTCSignaler := preload("res://addons/networked/transport/webrtc/signaler/webrtc_signaler.gd")

# room_id -> Array[PairedWebRTCSignaler] sharing it.
static var _rooms: Dictionary = { }

var _room := ""
var _local_id := 0


func _open(p_room_id: String, local_multiplayer_id: int) -> Error:
	_local_id = local_multiplayer_id
	_room = p_room_id if not p_room_id.is_empty() else _generate_room()
	if not _rooms.has(_room):
		_rooms[_room] = []
	(_rooms[_room] as Array).append(self)
	ready.emit()
	return OK


func _room_id() -> String:
	return _room


func _local_signaler_id() -> String:
	return str(_local_id)


func _poll(_dt: float) -> void:
	pass


func _close() -> void:
	if _rooms.has(_room):
		var peers: Array = _rooms[_room]
		peers.erase(self)
		if peers.is_empty():
			_rooms.erase(_room)
	_room = ""


func _send(
		to_multiplayer_id: int,
		_to_signaler_id: String,
		kind: String,
		payload: Dictionary,
) -> void:
	for other in _rooms.get(_room, []):
		if other != self and other._local_id == to_multiplayer_id:
			other.received.emit(_local_id, _local_signaler_id(), kind, payload)


func _generate_room() -> String:
	return "paired_%d" % Time.get_ticks_usec()
