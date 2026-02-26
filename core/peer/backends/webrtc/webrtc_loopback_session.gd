extends Resource
class_name WebRTCLoopbackSession

var server_peer: WebRTCMultiplayerPeer
var client_peer: WebRTCMultiplayerPeer

var pc_server: WebRTCPeerConnection
var pc_client: WebRTCPeerConnection

func has_live_server() -> bool:
	return server_peer and server_peer.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED

func init_server_side() -> void:
	if server_peer: return

	server_peer = WebRTCMultiplayerPeer.new()
	var err := server_peer.create_server()
	if err != OK:
		push_warning("Loopback: server create_server failed: %s" % error_string(err))
		return

	pc_server = WebRTCPeerConnection.new()
	var config := {"iceServers": []} 
	pc_server.initialize(config)


	server_peer.add_peer(pc_server, 2)

# init Client side AND trigger handshake
func init_client_side() -> void:
	if client_peer: return
	
	if not server_peer:
		init_server_side()

	client_peer = WebRTCMultiplayerPeer.new()
	var err := client_peer.create_client(2)
	if err != OK:
		push_warning("Loopback: client create_client failed: %s" % error_string(err))
		return

	pc_client = WebRTCPeerConnection.new()
	var config := {"iceServers": []} 
	pc_client.initialize(config)
	
	client_peer.add_peer(pc_client, 1)

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

	pc_server.ice_candidate_created.connect(pc_client.add_ice_candidate)
	pc_client.ice_candidate_created.connect(pc_server.add_ice_candidate)

	pc_server.create_offer()
	print("WebRTC loopback handshake started.")

func get_server_peer() -> WebRTCMultiplayerPeer:
	init_server_side()
	return server_peer

func get_client_peer() -> WebRTCMultiplayerPeer:
	init_client_side()
	return client_peer

func poll() -> void:
	if server_peer:
		server_peer.poll()
	if client_peer:
		client_peer.poll()

func reset() -> void:
	if server_peer:
		server_peer.close()
	if client_peer:
		client_peer.close()
	if pc_server:
		pc_server.close()
	if pc_client:
		pc_client.close()

	server_peer = null
	client_peer = null
	pc_server = null
	pc_client = null
