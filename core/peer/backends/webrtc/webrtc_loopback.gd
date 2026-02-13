class_name WebRTCLoopbackBackend
extends BackendPeer

var session: WebRTCLoopbackSession = preload("uid://d2u1yyaikw2sh")

func host() -> Error:
	api.multiplayer_peer = session.get_server_peer()
	print("WebRTC loopback server ready.")
	return OK

func join(_server_address: String, _username: String = "") -> Error:
	api.multiplayer_peer = session.get_client_peer()
	print("WebRTC loopback client ready.")
	return OK

func poll(dt: float) -> void:
	if session:
		session.poll()
		
	# Call the base class poll() to handle the api.poll() logic
	super.poll(dt)
