## Shared in-process session that links [LocalMultiplayerPeer] instances without real sockets.
##
## One process-wide singleton is maintained via [method get_shared_session].
class_name LocalLoopbackSession
extends Resource

static var shared: LocalLoopbackSession = null

var server_peer: LocalMultiplayerPeer
var client_peers: Array[LocalMultiplayerPeer] = []

## Returns the process-wide shared session, creating it on first access.
static func get_shared_session() -> LocalLoopbackSession:
	if not shared:
		shared = LocalLoopbackSession.new()
	return shared

## Returns [code]true[/code] if the server peer exists and is connected.
func has_live_server() -> bool:
	return server_peer != null and server_peer.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED

func init_server_side() -> void:
	if has_live_server():
		return

	if server_peer:
		server_peer.close()

	server_peer = LocalMultiplayerPeer.new()
	var err := server_peer.create_server()
	if err != OK:
		push_warning("Loopback: server create_server failed")

## Creates and links a new client peer to the server. Returns the new [LocalMultiplayerPeer].
func create_client_peer() -> LocalMultiplayerPeer:
	init_server_side()

	var client := LocalMultiplayerPeer.new()
	var client_id := randi_range(2, 2147483647)
	var err := client.create_client(client_id)
	if err != OK:
		push_warning("Loopback: client create_client failed")
		return client

	server_peer.force_connect_peer(client_id, client)
	client.force_connect_peer(1, server_peer)
	client_peers.append(client)
	NetLog.info("Local loopback handshake complete for client %d." % client_id)
	return client

## Returns the server peer, initializing it first if necessary.
func get_server_peer() -> LocalMultiplayerPeer:
	init_server_side()
	return server_peer

## Convenience wrapper around [method create_client_peer] for symmetry with [WebRTCLoopbackSession].
func get_client_peer() -> LocalMultiplayerPeer:
	return create_client_peer()

## Polls the server and all active client peers each frame.
func poll() -> void:
	if server_peer:
		server_peer.poll()
	for client in client_peers:
		if client and not client._closed:
			client.poll()

## Closes all peers and resets the session so a new server can be hosted.
func reset() -> void:
	if server_peer: server_peer.close()
	for client in client_peers:
		if client: client.close()
	server_peer = null
	client_peers.clear()
