## A [WebRTCTransport] that signals through an in-process [PairedWebRTCSignaler].
##
## The real [WebRTCSession] and its native ICE run unchanged, only the signaling
## channel is shortcut, so a green run proves the session reaches a native link
## behind any [WebRTCSignaler] without a tracker or socket. Empty ICE servers
## keep the loopback handshake off the network.
class_name PairedWebRTCTransport
extends WebRTCTransport

func _init() -> void:
	ice_servers = []


func _make_signaler() -> WebRTCSignaler:
	return PairedWebRTCSignaler.new()
