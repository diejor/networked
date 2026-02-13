class_name WebRTCLoopbackClientBackend
extends MultiplayerClientBackend


var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")

func create_connection(_server_address: String, _username: String) -> Error:
	api.multiplayer_peer =  session.get_client_peer()
	print("WebRTC loopback client ready.")

	return OK

func poll(_dt: float) -> void:
	assert(api)
	assert(api.has_multiplayer_peer())
	
	if session:
		session.poll()
	
	api.poll()
