class_name LocalLoopbackSession
extends Resource

static var shared: LocalLoopbackSession = null

var server_peer: LocalMultiplayerPeer
var client_peers: Array[LocalMultiplayerPeer] = []

static func get_shared_session() -> LocalLoopbackSession:
	if not shared:
		shared = LocalLoopbackSession.new()
	return shared

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

func get_server_peer() -> LocalMultiplayerPeer:
	init_server_side()
	return server_peer

func get_client_peer() -> LocalMultiplayerPeer:
	return create_client_peer()

func poll() -> void:
	if server_peer:
		server_peer.poll()
	for client in client_peers:
		if client and not client._closed:
			client.poll()

func reset() -> void:
	if server_peer: server_peer.close()
	for client in client_peers:
		if client: client.close()
	server_peer = null
	client_peers.clear()
