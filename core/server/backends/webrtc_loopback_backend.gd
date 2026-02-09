extends MultiplayerServerBackend
class_name WebRTCLoopbackServerBackend

var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")

func create_server() -> Error:
	api.multiplayer_peer = session.get_server_peer()
	
	return OK

func poll(_dt: float) -> void:
	assert(api)
	assert(api.has_multiplayer_peer())
	
	if session:
		session.poll()
	
	api.poll()
