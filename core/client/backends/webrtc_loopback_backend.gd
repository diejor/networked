extends MultiplayerClientBackend
class_name WebRTCLoopbackClientBackend

var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")

func _init() -> void:
	# multiplayer_api is created in base; we set multiplayer_peer in create_connection.
	multiplayer_peer = null

func create_connection(_server_address: String, _username: String) -> Error:
	session.ensure_initialized()
	multiplayer_peer = session.get_client_peer()
	multiplayer_api.multiplayer_peer = multiplayer_peer

	return OK

func poll(_dt: float) -> void:
	if session:
		session.poll()
	if multiplayer_api and multiplayer_api.has_multiplayer_peer():
		multiplayer_api.poll()

func peer_reset_state() -> void:
	# Same as server: don't reset the shared session silently from one side.
	multiplayer_peer = null
	multiplayer_api.multiplayer_peer = null
