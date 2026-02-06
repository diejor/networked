extends Resource
class_name WebRTCLoopbackSession

## Shared WebRTC loopback for one server+client pair in the same process.
## Both WebRTCLoopbackServerBackend and WebRTCLoopbackClientBackend
## will point to the same Session resource.

var server_peer: WebRTCMultiplayerPeer
var client_peer: WebRTCMultiplayerPeer

var pc_server: WebRTCPeerConnection
var pc_client: WebRTCPeerConnection

var _initialized := false

func ensure_initialized() -> void:
	if _initialized:
		return

	server_peer = WebRTCMultiplayerPeer.new()
	client_peer = WebRTCMultiplayerPeer.new()

	var err := server_peer.create_server() # server id = 1
	if err != OK:
		push_warning("Loopback: server create_server failed: %s" % error_string(err))
		return

	err = client_peer.create_client(2)     # client id = 2
	if err != OK:
		push_warning("Loopback: client create_client failed: %s" % error_string(err))
		return

	pc_server = WebRTCPeerConnection.new()
	pc_client = WebRTCPeerConnection.new()

	var config := {"iceServers": []} 
	err = pc_server.initialize(config)
	if err != OK:
		push_warning("Loopback: pc_server.initialize failed: %s" % error_string(err))
		return

	err = pc_client.initialize(config)
	if err != OK:
		push_warning("Loopback: pc_client.initialize failed: %s" % error_string(err))
		return

	# Attach to WebRTCMultiplayerPeers
	err = server_peer.add_peer(pc_server, 2)
	if err != OK:
		push_warning("Loopback: server_peer.add_peer failed: %s" % error_string(err))
		return

	err = client_peer.add_peer(pc_client, 1)
	if err != OK:
		push_warning("Loopback: client_peer.add_peer failed: %s" % error_string(err))
		return

	pc_server.session_description_created.connect(
		func(t: String, sdp: String) -> void:
			pc_server.set_local_description(t, sdp)
			pc_client.set_remote_description(t, sdp)
	)

	pc_client.session_description_created.connect(
		func(t: String, sdp: String) -> void:
			pc_client.set_local_description(t, sdp)
			pc_server.set_remote_description(t, sdp)
	)

	# ICE wiring.
	pc_server.ice_candidate_created.connect(pc_client.add_ice_candidate)
	pc_client.ice_candidate_created.connect(pc_server.add_ice_candidate)

	err = pc_server.create_offer()
	if err != OK:
		push_warning("Loopback: pc_server.create_offer failed: %s" % error_string(err))
		return

	print("WebRTC loopback session created.")
	_initialized = true

func get_server_peer() -> WebRTCMultiplayerPeer:
	ensure_initialized()
	return server_peer

func get_client_peer() -> WebRTCMultiplayerPeer:
	ensure_initialized()
	return client_peer

func poll() -> void:
	if server_peer:
		server_peer.poll()
	if client_peer:
		client_peer.poll()

func reset() -> void:
	server_peer = null
	client_peer = null
	pc_server = null
	pc_client = null
	_initialized = false
