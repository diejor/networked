## In-process WebRTC loopback session used by [WebRTCLoopbackBackend].
##
## Performs the full WebRTC offer/answer/ICE handshake in-memory between a server
## [WebRTCMultiplayerPeer] and a client [WebRTCMultiplayerPeer], with no real network.
extends Resource
class_name WebRTCLoopbackSession

var server_peer: WebRTCMultiplayerPeer
var client_peer: WebRTCMultiplayerPeer

var pc_server: WebRTCPeerConnection
var pc_client: WebRTCPeerConnection

## Returns [code]true[/code] if the server peer exists and is not disconnected.
func has_live_server() -> bool:
	return server_peer and server_peer.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED

func init_server_side() -> void:
	if server_peer: return

	server_peer = WebRTCMultiplayerPeer.new()
	var err := server_peer.create_server()
	if err != OK:
		Netw.dbg.warn("Loopback: server create_server failed: %s" % [error_string(err)], func(m): push_warning(m))
		return

	pc_server = WebRTCPeerConnection.new()
	var config := {"iceServers": []} 
	pc_server.initialize(config)


	server_peer.add_peer(pc_server, 2)

func init_client_side() -> void:
	if client_peer: return
	
	if not server_peer:
		init_server_side()

	client_peer = WebRTCMultiplayerPeer.new()
	var err := client_peer.create_client(2)
	if err != OK:
		Netw.dbg.warn("Loopback: client create_client failed: %s" % [error_string(err)], func(m): push_warning(m))
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

## Returns the server peer, initializing the server side first if necessary.
func get_server_peer() -> WebRTCMultiplayerPeer:
	init_server_side()
	return server_peer

## Returns a new client peer fully connected to the server, triggering the handshake if needed.
func get_client_peer() -> WebRTCMultiplayerPeer:
	init_client_side()
	return client_peer

## Polls both server and client peers each frame.
func poll() -> void:
	if server_peer:
		server_peer.poll()
	if client_peer:
		client_peer.poll()

## Closes all peer connections and resets the session to its initial state.
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
