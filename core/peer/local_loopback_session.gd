class_name LocalLoopbackSession
extends Resource

# Static reference guarantees both backends talk to the same memory
static var shared: LocalLoopbackSession = null

var server_peer: LocalMultiplayerPeer
var client_peer: LocalMultiplayerPeer

static func get_shared_session() -> LocalLoopbackSession:
	if not shared:
		shared = LocalLoopbackSession.new()
	return shared

func init_server_side() -> void:
	if server_peer: return

	server_peer = LocalMultiplayerPeer.new()
	var err := server_peer.create_server()
	if err != OK:
		push_warning("Loopback: server create_server failed")

func init_client_side() -> void:
	if client_peer: return
	
	if not server_peer:
		init_server_side()

	client_peer = LocalMultiplayerPeer.new()
	var client_id := randi_range(2, 2147483647)
	var err := client_peer.create_client(client_id)
	
	if err != OK:
		push_warning("Loopback: client create_client failed")
		return

	server_peer.force_connect_peer(client_id, client_peer)
	client_peer.force_connect_peer(1, server_peer)
	
	print("Local loopback handshake complete.")

func get_server_peer() -> LocalMultiplayerPeer:
	init_server_side()
	return server_peer

func get_client_peer() -> LocalMultiplayerPeer:
	init_client_side()
	return client_peer

func poll() -> void:
	if server_peer:
		server_peer.poll()
	if client_peer:
		client_peer.poll()

func reset() -> void:
	if server_peer: server_peer.close()
	if client_peer: client_peer.close()
	server_peer = null
	client_peer = null
