extends MultiplayerServerBackend
class_name WebRTCLoopbackServerBackend

var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")

func create_server() -> Error:
	session.ensure_initialized()
	multiplayer_peer = session.get_server_peer()
	multiplayer_api.multiplayer_peer = multiplayer_peer

	# No real network server, so just return OK.
	return OK

func poll(_dt: float) -> void:
	if session:
		session.poll()

	if multiplayer_api and multiplayer_api.has_multiplayer_peer():
		multiplayer_api.poll()

func peer_reset_state() -> void:
	# For loopback, it's usually safest NOT to reset the shared session from one side,
	# unless you are coordinating it. For now, we just clear local pointers.
	multiplayer_peer = null
	multiplayer_api.multiplayer_peer = null
