## [WebRTCBackend] whose signaler is an in-process [PairedWebRTCSignaler].
##
## It runs the real [WebRTCSession] over loopback ICE with signaling shortcut
## in process, so a test proves the session is signaling-independent with no
## WebTorrent traffic. Tests clear [member WebRTCBackend.ice_servers] to keep
## the handshake off the network.
## [codeblock]
## var backend := PairedWebRTCBackend.new()
## backend.ice_servers = []
## tree.backend = backend
## [/codeblock]
class_name PairedWebRTCBackend
extends WebRTCBackend


func _make_signaler() -> WebRTCSignaler:
	return PairedWebRTCSignaler.new()
