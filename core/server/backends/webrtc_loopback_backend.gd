class_name WebRTCLoopbackServerBackend
extends MultiplayerServerBackend


var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")

func create_server() -> Error:
	api.multiplayer_peer = session.get_server_peer()
	print("WebRTC loopback server ready.")
	
	return OK

func poll(_dt: float) -> void:
	assert(api)
	assert(api.has_multiplayer_peer())
	
	if session:
		session.poll()
	
	api.poll()
